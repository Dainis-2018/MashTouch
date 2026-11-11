// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Process flow adapted from Open ArdBir 2.8.3-RC auto_mode, stage_loop,
// hop_add, add_malt, Iodine_Test, remove_malt, WaitStart, Cooling and
// Whirlpool (see NOTICE.md), rewritten as a non-blocking state machine.

#include "brew.h"

#include "events.h"

namespace brew {

using settings::PumpMode;

namespace {
constexpr uint32_t kMin = 60000UL;
constexpr int kMaxAdditions = 32;  // bits in Process::fired_
// Boil stall detection: above this temperature, within this band, this long.
constexpr float kStallMinC = 90.0f;
// The boil timer can be started by hand from this temperature.
constexpr float kManualBoilMinC = 85.0f;
constexpr float kStallBandC = 0.3f;
constexpr uint32_t kStallMs = 4 * 60000UL;
}  // namespace

bool Process::start(const settings::Recipe& r, const settings::Settings& s, uint16_t delayMin) {
  init(r, s, delayMin);
  enter(Phase::WaterCheck);
  return true;
}

void Process::init(const settings::Recipe& r, const settings::Settings& s, uint16_t delayMin) {
  recipe_ = r;
  // The mash as one sequence: mash in, rests..., mash out.
  seq_.clear();
  if (r.mash) {
    seq_.push_back(r.mashIn);
    seq_.insert(seq_.end(), r.rests.begin(), r.rests.end());
    seq_.push_back(r.mashOut);
  }
  cfg_ = s.process;
  autoPrime_ = s.pump.autoPrime;
  pumpInBoil_ = s.pump.inBoil;
  pumpCooling_ = s.pump.duringCooling;
  mashPowerPct_ = s.heater.mashPowerPct;
  delayMin_ = delayMin;
  boilTempC_ = cfg_.boilTempC;
  coolTargetC_ = cfg_.coolingTargetC;
  paused_ = false;
  iodineDone_ = false;
  fired_ = 0;
  lastAddition_ = -1;
  step_ = 0;
}

bool Process::restore(const settings::Recipe& r, const settings::Settings& s,
                      const Snapshot& snap) {
  init(r, s, 0);
  fired_ = snap.additionsFired;
  switch (snap.phase) {
    case Phase::Idle:
    case Phase::Finished:
      stop();
      return false;
    case Phase::WaterCheck:
    case Phase::Prime:
    case Phase::DelayedStart:
      startMash();
      break;
    case Phase::Mash:
    case Phase::IodineTest:
      if (snap.step < 0 || snap.step >= stepCount()) {
        afterMash();
        break;
      }
      iodineDone_ = true;
      step_ = snap.step;
      enter(snap.phase);
      maltAdded_ = snap.elapsedMs > 0;  // the rest had started: malt is in
      break;
    default:
      enter(snap.phase);
      break;
  }
  // The temperature has to be reached again; time already spent is kept.
  timer_.setElapsed(snap.elapsedMs);
  return true;
}

void Process::stop() {
  phase_ = Phase::Idle;
  prompt_ = Prompt::None;
  paused_ = false;
  reached_ = false;
  counting_ = false;
}

const char* Process::stepName() const {
  switch (phase_) {
    case Phase::Mash:
      return step_ < stepCount() ? seq_[step_].name.c_str() : "";
    case Phase::IodineTest: return "Iodine test";
    default: return phaseName(phase_);
  }
}

Snapshot Process::snapshot() const {
  Snapshot s;
  s.phase = phase_;
  s.step = (int8_t)step_;
  s.elapsedMs = timer_.elapsedMs();
  s.additionsFired = fired_;
  return s;
}

void Process::setPrompt(Prompt p) {
  prompt_ = p;
  if (p != Prompt::None) events::post(events::Type::PromptOpened, (int16_t)p);
}

void Process::enter(Phase p) {
  phase_ = p;
  reached_ = false;
  counting_ = false;
  newStep_ = true;
  prompt_ = Prompt::None;
  timer_.reset(0);

  switch (p) {
    case Phase::Idle:
      return;
    case Phase::WaterCheck:
      setPrompt(Prompt::WaterAdded);
      break;
    case Phase::Prime:
      primeRequested_ = true;
      break;
    case Phase::DelayedStart:
      timer_.reset(delayMin_ * kMin);
      break;
    case Phase::Mash:
      maltAdded_ = false;
      setpoint_ = seq_[step_].tempC;
      timer_.reset(seq_[step_].minutes * kMin);
      break;
    case Phase::IodineTest:
      // Hold the previous step's temperature while the user tests.
      setpoint_ = seq_[step_ > 0 ? step_ - 1 : step_].tempC;
      timer_.reset(cfg_.iodineTimeoutMin * kMin);
      setPrompt(Prompt::IodineTest);
      break;
    case Phase::RemoveMalt:
      setPrompt(Prompt::RemoveMalt);
      break;
    case Phase::Boil:
      setpoint_ = boilTempC_;
      timer_.reset(recipe_.boilMinutes * kMin);
      boilWarned_ = stallWarned_ = false;
      stallMs_ = 0;
      stallRefC_ = 0;
      break;
    case Phase::HotWhirlpool:
    case Phase::ColdWhirlpool:
      timer_.reset(cfg_.whirlpoolMin * kMin);
      setPrompt(Prompt::StartWhirlpool);
      break;
    case Phase::Cooling:
      setpoint_ = coolTargetC_;
      setPrompt(Prompt::StartCooling);
      break;
    case Phase::Finished:
      events::post(events::Type::BrewFinished);
      setPrompt(Prompt::Finished);
      break;
  }
  events::post(events::Type::StepStarted, (int16_t)p);
}

void Process::startMash() { startStep(0); }

void Process::startStep(int i) {
  if (i >= stepCount()) {
    afterMash();
    return;
  }
  step_ = i;
  if (isMashOut(i) && cfg_.iodineTest && !iodineDone_) {
    iodineDone_ = true;
    enter(Phase::IodineTest);
    return;
  }
  enter(Phase::Mash);
}

void Process::afterMash() {
  if (!cfg_.skipRemoveMalt && stepCount() > 0)
    enter(Phase::RemoveMalt);
  else
    afterRemoveMalt();
}

void Process::afterRemoveMalt() {
  if (recipe_.boilMinutes > 0) enter(Phase::Boil);
  else afterBoil();
}

void Process::afterBoil() {
  if (cfg_.whirlpool == settings::Whirlpool::Hot) enter(Phase::HotWhirlpool);
  else enter(Phase::Cooling);
}

void Process::afterCooling() {
  if (cfg_.whirlpool == settings::Whirlpool::Cold) enter(Phase::ColdWhirlpool);
  else enter(Phase::Finished);
}

void Process::advance() {
  switch (phase_) {
    case Phase::Idle: break;
    case Phase::WaterCheck:
      if (autoPrime_) enter(Phase::Prime);
      else if (delayMin_) enter(Phase::DelayedStart);
      else startMash();
      break;
    case Phase::Prime:
      primeRequested_ = false;
      if (delayMin_) enter(Phase::DelayedStart);
      else startMash();
      break;
    case Phase::DelayedStart: startMash(); break;
    case Phase::Mash: startStep(step_ + 1); break;
    case Phase::IodineTest: startStep(step_); break;
    case Phase::RemoveMalt: afterRemoveMalt(); break;
    case Phase::Boil:
      // Flameout additions that did not fire yet.
      for (int i = 0; i < (int)recipe_.additions.size() && i < kMaxAdditions; i++) {
        if (!(fired_ & (1UL << i))) {
          fired_ |= 1UL << i;
          lastAddition_ = i;
          events::post(events::Type::Addition, (int16_t)i);
        }
      }
      afterBoil();
      break;
    case Phase::HotWhirlpool: enter(Phase::Cooling); break;
    case Phase::Cooling: afterCooling(); break;
    case Phase::ColdWhirlpool: enter(Phase::Finished); break;
    case Phase::Finished: stop(); break;
  }
}

void Process::skip() {
  if (phase_ == Phase::Idle || phase_ == Phase::Finished) return;
  advance();
}

void Process::answer(bool yes) {
  switch (prompt_) {
    case Prompt::None: return;
    case Prompt::AddMalt:
      if (!yes) {
        stop();
        break;
      }
      maltAdded_ = true;  // the step rest starts now
      prompt_ = Prompt::None;
      newStep_ = true;    // pump back to the step mode
      break;
    case Prompt::WaterAdded:
    case Prompt::RemoveMalt:
      if (yes) advance();
      else stop();
      break;
    case Prompt::IodineTest:
      if (yes) advance();  // no: keep holding and testing
      break;
    case Prompt::StartWhirlpool:
    case Prompt::StartCooling:
      if (yes) {
        prompt_ = Prompt::None;
        newStep_ = true;
      } else {
        advance();
      }
      break;
    case Prompt::Finished:
      stop();
      break;
  }
}

void Process::setStepTemp(float c) {
  switch (phase_) {
    case Phase::Mash: seq_[step_].tempC = c; break;
    case Phase::Boil: boilTempC_ = c; break;
    case Phase::Cooling: coolTargetC_ = c; break;
    default: return;
  }
  setpoint_ = c;
}

void Process::startTimerNow(float tempC) {
  if (reached_ || (phase_ != Phase::Mash && phase_ != Phase::Boil)) return;
  if (phase_ == Phase::Boil && tempC < kManualBoilMinC) return;  // not a boil yet
  if (phase_ == Phase::Boil && tempC < setpoint_) {
    boilTempC_ = tempC;  // this kettle boils here: hold the boil power from now on
    setpoint_ = tempC;
  }
  reached_ = true;
  events::post(events::Type::TempReached);
}

void Process::setRemainingMin(uint16_t min) {
  switch (phase_) {
    case Phase::DelayedStart:
    case Phase::Mash:
    case Phase::IodineTest:
    case Phase::Boil:
    case Phase::HotWhirlpool:
    case Phase::ColdWhirlpool:
      timer_.setDuration(timer_.elapsedMs() + min * kMin);
      break;
    default:
      break;
  }
}

void Process::checkAdditions() {
  uint32_t remaining = timer_.remainingMs();
  for (int i = 0; i < (int)recipe_.additions.size() && i < kMaxAdditions; i++) {
    if (fired_ & (1UL << i)) continue;
    uint16_t at = recipe_.additions[i].atMinutes;
    if (at >= recipe_.boilMinutes || remaining <= at * kMin) {
      fired_ |= 1UL << i;
      lastAddition_ = i;
      events::post(events::Type::Addition, (int16_t)i);
    }
  }
}

Output Process::tick(float tempC, uint32_t dtMs, bool pumpPriming) {
  Output out;
  if (phase_ == Phase::Idle) return out;

  out.newStep = newStep_;
  newStep_ = false;
  if (paused_) return out;  // heater and pump off, timers stopped

  // Temperature reached: latched for the step, as in Open ArdBir.
  auto checkReached = [&](float sp) {
    if (!reached_ && tempC >= sp - cfg_.reachedTolC) {
      reached_ = true;
      events::post(events::Type::TempReached);
    }
  };

  auto regulate = [&](float sp) {
    out.heat.mode = HeatMode::Regulate;
    out.heat.setpointC = sp;
  };

  switch (phase_) {
    case Phase::Idle:
    case Phase::WaterCheck:
    case Phase::Finished:
      break;

    case Phase::Prime:
      if (primeRequested_) {
        out.startPrime = true;
        primeRequested_ = false;
      } else if (!pumpPriming) {
        advance();
      }
      break;

    case Phase::DelayedStart:
      counting_ = true;
      timer_.tick(dtMs);
      if (timer_.done()) advance();
      break;

    case Phase::Mash: {
      const settings::MashStep& st = seq_[step_];
      regulate(setpoint_);
      checkReached(setpoint_);
      // Mash in: once reached, hold the temperature with the pump off
      // until the user confirms; the rest timer starts after that.
      bool waitMalt = step_ == 0 && !maltAdded_ && reached_;
      // Strike water heats at full power; with the grain in, the limit applies.
      if (step_ > 0 || maltAdded_ || waitMalt) out.heat.maxPct = mashPowerPct_;
      if (waitMalt && prompt_ != Prompt::AddMalt) setPrompt(Prompt::AddMalt);
      out.pump.mode = waitMalt ? PumpMode::Off : st.pump;
      out.pump.stepTempC = setpoint_;
      out.pump.reached = reached_;
      counting_ = reached_ && !waitMalt;
      if (counting_) timer_.tick(dtMs);
      if (counting_ && timer_.done()) {
        events::post(events::Type::StepDone);
        advance();
      }
      break;
    }

    case Phase::IodineTest: {
      const settings::MashStep& prev = seq_[step_ > 0 ? step_ - 1 : step_];
      regulate(setpoint_);
      out.heat.maxPct = mashPowerPct_;
      out.pump.mode = prev.pump;
      out.pump.stepTempC = setpoint_;
      out.pump.reached = true;
      if (cfg_.iodineTimeoutMin) {
        counting_ = true;
        timer_.tick(dtMs);
        if (timer_.done()) advance();
      }
      break;
    }

    case Phase::RemoveMalt:
      if (cfg_.heatDuringRemoveMalt && stepCount() > 0) {
        regulate(seq_.back().tempC);
        out.heat.maxPct = mashPowerPct_;
      }
      break;

    case Phase::Boil:
      regulate(setpoint_);
      out.heat.boil = true;
      out.pump.mode = pumpInBoil_ ? PumpMode::On : PumpMode::Off;
      out.pump.reached = reached_;
      checkReached(setpoint_);
      if (!reached_) {
        // Boil-over watch shortly before the boil.
        if (!boilWarned_ && cfg_.boilWarnC > 0 && tempC >= setpoint_ - cfg_.boilWarnC) {
          boilWarned_ = true;
          events::post(events::Type::BoilSoon);
        }
        // Hot but not getting hotter for 4 minutes: it probably boils below
        // the boil setting (altitude, sensor offset). The user can start the
        // timer with startTimerNow().
        if (tempC > kStallMinC) {
          float diff = tempC - stallRefC_;
          if (diff > kStallBandC || diff < -kStallBandC) {
            stallRefC_ = tempC;
            stallMs_ = 0;
          } else {
            stallMs_ += dtMs;
            if (stallMs_ >= kStallMs && !stallWarned_) {
              stallWarned_ = true;
              events::post(events::Type::BoilStalled);
            }
          }
        }
      }
      counting_ = reached_;
      if (counting_) {
        timer_.tick(dtMs);
        checkAdditions();
        if (timer_.done()) {
          events::post(events::Type::StepDone);
          advance();
        }
      }
      break;

    case Phase::HotWhirlpool:
    case Phase::ColdWhirlpool:
      if (prompt_ != Prompt::None) break;
      out.pump.mode = PumpMode::On;
      out.pump.stopTempApplies = false;
      counting_ = true;
      timer_.tick(dtMs);
      if (timer_.done()) {
        events::post(events::Type::StepDone);
        advance();
      }
      break;

    case Phase::Cooling:
      if (prompt_ != Prompt::None) break;
      out.pump.mode = pumpCooling_ ? PumpMode::On : PumpMode::Off;
      out.pump.stopTempApplies = false;
      counting_ = true;
      timer_.tick(dtMs);  // count-up only
      if (tempC <= coolTargetC_) {
        events::post(events::Type::TempReached);
        advance();
      }
      break;
  }
  return out;
}

const char* phaseName(Phase p) {
  switch (p) {
    case Phase::Idle: return "Idle";
    case Phase::WaterCheck: return "Water check";
    case Phase::Prime: return "Pump prime";
    case Phase::DelayedStart: return "Delayed start";
    case Phase::Mash: return "Mash";
    case Phase::IodineTest: return "Iodine test";
    case Phase::RemoveMalt: return "Remove malt";
    case Phase::Boil: return "Boil";
    case Phase::HotWhirlpool: return "Hot whirlpool";
    case Phase::Cooling: return "Cooling";
    case Phase::ColdWhirlpool: return "Whirlpool";
    case Phase::Finished: return "Finished";
  }
  return "?";
}

const char* promptName(Prompt p) {
  switch (p) {
    case Prompt::None: return "";
    case Prompt::WaterAdded: return "Water added?";
    case Prompt::AddMalt: return "Add malt, then confirm";
    case Prompt::IodineTest: return "Iodine test: conversion complete?";
    case Prompt::RemoveMalt: return "Remove malt, then confirm";
    case Prompt::StartWhirlpool: return "Start whirlpool?";
    case Prompt::StartCooling: return "Start cooling?";
    case Prompt::Finished: return "Brew finished";
  }
  return "";
}

}  // namespace brew
