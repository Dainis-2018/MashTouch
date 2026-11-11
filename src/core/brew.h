// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Automatic brewing process as a non-blocking state machine, adapted from
// Open ArdBir auto_mode / stage_loop and the phases around them: water
// check, pump prime, delayed start, mash-in, add malt, mash steps, iodine
// test, remove malt, boil with addition alarms, whirlpool, cooling.
//
// The process only decides what should happen (setpoint, heater mode, pump
// mode, prompts). The controller applies it to the hardware.
#pragma once
#include <stdint.h>

#include "heater.h"
#include "pump.h"
#include "settings.h"
#include "timer.h"

namespace brew {

enum class Phase : uint8_t {
  Idle,
  WaterCheck,
  Prime,
  DelayedStart,
  Mash,
  IodineTest,
  RemoveMalt,
  Boil,
  HotWhirlpool,
  Cooling,
  ColdWhirlpool,
  Finished,
};

// Questions waiting for the user. Yes = continue. No = stop the brew for
// WaterAdded/AddMalt/RemoveMalt, skip that part for whirlpool/cooling.
enum class Prompt : uint8_t {
  None,
  WaterAdded,
  AddMalt,
  IodineTest,  // yes = conversion complete; the test also ends on its timeout
  RemoveMalt,
  StartWhirlpool,
  StartCooling,
  Finished,
};

struct Output {
  HeatDemand heat;
  PumpRequest pump;
  bool newStep = false;     // the controller resets the pump override/cycle
  bool startPrime = false;  // start the priming pulses
};

// What to save so a brew can continue after a power loss.
struct Snapshot {
  Phase phase = Phase::Idle;
  int8_t step = 0;
  uint32_t elapsedMs = 0;
  uint32_t additionsFired = 0;
};

class Process {
 public:
  bool start(const settings::Recipe& r, const settings::Settings& s, uint16_t delayMin);
  bool restore(const settings::Recipe& r, const settings::Settings& s, const Snapshot& snap);
  void stop();

  bool running() const { return phase_ != Phase::Idle; }
  void setPaused(bool p) { paused_ = p; }
  bool paused() const { return paused_; }

  void skip();
  void answer(bool yes);

  // Adjust the current step: temperature (mash, boil, cooling target) and
  // remaining minutes (anything with a countdown).
  void setStepTemp(float c);
  void setRemainingMin(uint16_t min);
  // "It is at temperature now": start the rest or boil timer although the
  // setpoint was not reached (a kettle that boils below the boil setting,
  // a sensor that reads low). In the boil the setpoint drops to tempC.
  void startTimerNow(float tempC);

  // pumpPriming: the pump is still running the prime pulses.
  Output tick(float tempC, uint32_t dtMs, bool pumpPriming);

  Phase phase() const { return phase_; }
  Prompt prompt() const { return prompt_; }
  int step() const { return step_; }
  // Mash sequence: step 0 = mash in, 1..n = rests, last = mash out.
  int stepCount() const { return (int)seq_.size(); }
  bool isMashIn(int i) const { return i == 0 && !seq_.empty(); }
  bool isMashOut(int i) const { return i > 0 && i == (int)seq_.size() - 1; }
  const char* stepName() const;
  float setpointC() const { return setpoint_; }
  bool reached() const { return reached_; }
  bool timerCounting() const { return counting_; }
  uint32_t remainingMs() const { return timer_.remainingMs(); }
  uint32_t elapsedMs() const { return timer_.elapsedMs(); }
  const settings::Recipe& recipe() const { return recipe_; }
  // Most recent addition alarm, -1 if none yet.
  int lastAddition() const { return lastAddition_; }
  Snapshot snapshot() const;

 private:
  void init(const settings::Recipe& r, const settings::Settings& s, uint16_t delayMin);
  void enter(Phase p);
  void advance();
  void startMash();
  void startStep(int i);
  void afterMash();
  void afterRemoveMalt();
  void afterBoil();
  void afterCooling();
  void checkAdditions();
  void setPrompt(Prompt p);

  settings::Recipe recipe_;
  std::vector<settings::MashStep> seq_;
  settings::Process cfg_;
  bool autoPrime_ = true;
  bool pumpInBoil_ = false;
  bool pumpCooling_ = false;
  uint8_t mashPowerPct_ = 100;
  uint16_t delayMin_ = 0;

  Phase phase_ = Phase::Idle;
  Prompt prompt_ = Prompt::None;
  bool paused_ = false;
  int step_ = 0;
  bool iodineDone_ = false;
  bool maltAdded_ = false;
  bool boilWarned_ = false;
  bool stallWarned_ = false;
  float stallRefC_ = 0;
  uint32_t stallMs_ = 0;
  float setpoint_ = 0;
  float boilTempC_ = 99;
  float coolTargetC_ = 25;
  bool reached_ = false;
  bool counting_ = false;
  bool newStep_ = false;
  bool primeRequested_ = false;
  StepTimer timer_;
  uint32_t fired_ = 0;
  int lastAddition_ = -1;
};

const char* phaseName(Phase p);
const char* promptName(Prompt p);

}  // namespace brew
