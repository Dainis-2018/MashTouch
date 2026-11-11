// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "controller.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "app/history.h"
#include "core/manual.h"
#include "core/pump.h"
#include "core/settings.h"
#include "core/sim.h"
#include "hal/audio.h"
#include "hal/io.h"
#include "hal/outputs.h"
#include "hal/sensors.h"
#include "storage/brewlog.h"
#include "storage/storage.h"

namespace controller {
namespace {

using settings::OutputRole;
using settings::PumpMode;
using settings::SensorRole;

constexpr uint32_t kConfirmTimeoutMs = 10000;
constexpr uint32_t kPromptReminderMs = 30000;
constexpr uint32_t kFaultReminderMs = 10000;

Mode mode = Mode::Idle;
brew::Process process;
Manual manual;
Heater heater;
Pump pump;
KettleSim sim;
bool simOn = false;
float simSpeed = 1;

Status st;
EventListener listeners[4] = {};
const char* error = "";

// Virtual clock: runs faster than real time in fake-heating mode.
uint32_t lastReal = 0;
uint32_t vnow = 0;
float vfrac = 0;

Confirm confirm = Confirm::None;
uint32_t confirmAt = 0;
bool holdingPump = false;
uint8_t faults = 0;
uint32_t lastHeatAt = 0;
bool heatedOnce = false;
uint32_t promptSince = 0;
uint32_t faultBeepAt = 0;

// Runaway detection: the kettle heats up although the heater is off.
constexpr uint32_t kRunawaySettleMs = 120000;  // residual heat of the element
bool offTracking = false;
uint32_t offSince = 0;
float offMinC = 0;
bool runaway = false;  // latched until the mode ends

// Sparge water heater
bool spargeOn = false;
bool spargeAuto = false;  // switched on by the brew: off again at the boil
bool spargeHeating = false;
float spargeSetC = 76;

// Brew log
constexpr uint32_t kLogSampleMs = 30000;
uint32_t logStartV = 0;
uint32_t lastLogV = 0;

uint32_t logT() { return (vnow - logStartV) / 1000; }

// Power-loss resume
constexpr uint32_t kResumeSaveMs = 60000;
bool resumeOffered = false;
brew::Snapshot resumeSnap;
settings::Recipe resumeRecipe;
uint32_t lastResumeSave = 0;
brew::Phase savedPhase = brew::Phase::Idle;
int savedStep = -1;

void dropResumeOffer() {
  if (!resumeOffered) return;
  resumeOffered = false;
  storage::clearResume();
}

// Save the brew position on every phase or step change and once a minute.
void saveResumePoint(uint32_t now) {
  if (simOn || !process.running()) return;
  brew::Snapshot snap = process.snapshot();
  bool changed = snap.phase != savedPhase || snap.step != savedStep;
  if (!changed && now - lastResumeSave < kResumeSaveMs) return;
  savedPhase = snap.phase;
  savedStep = snap.step;
  lastResumeSave = now;
  storage::saveResume(snap, process.recipe());
}

SensorRole controlRole() { return settings::current().heater.controlSensor; }

bool refuse(const char* why) {
  error = why;
  return false;
}

bool canStart() {
  if (mode != Mode::Idle) return refuse("already running");
  float t;
  if (!sensors::read(controlRole(), t)) return refuse("no control sensor reading");
  if (sensors::criticalFault()) return refuse("sensor fault");
  return true;
}

void prepare() {
  const auto& s = settings::current();
  heater.configure(s.heater, s.process.boilTempC);
  heater.setStages(outputs::heaterCount());
  runaway = false;
  offTracking = false;
  pump.configure(s.pump);
  pump.clearOverride();
  pump.newStep();
  confirm = Confirm::None;
  outputs::setTestAllowed(false);
  dropResumeOffer();
  savedPhase = brew::Phase::Idle;
  savedStep = -1;
  error = "";
}

void toIdle() {
  if (mode == Mode::Auto && !simOn) storage::clearResume();  // finished or stopped
  if (mode == Mode::Auto && brewlog::active())
    brewlog::end(logT(), st.phase == brew::Phase::Finished ? "finished" : "stopped");
  if (spargeAuto) spargeOn = spargeAuto = false;
  runaway = false;
  mode = Mode::Idle;
  process.stop();
  manual.stop();
  heater.autotune().cancel();
  heater.off();
  pump.off();
  pump.stopPrime();
  pump.clearOverride();
  confirm = Confirm::None;
  outputs::setTestAllowed(true);
}

void handleEvent(const events::Event& e) {
  using events::Type;
  switch (e.type) {
    case Type::TempReached: audio::play(audio::Sound::TempReached); break;
    case Type::StepDone: audio::play(audio::Sound::StepDone); break;
    case Type::PromptOpened: audio::play(audio::Sound::Prompt); break;
    case Type::Addition: audio::play(audio::Sound::Addition); break;
    case Type::TimerDone: audio::play(audio::Sound::TimerDone); break;
    case Type::BrewFinished: audio::play(audio::Sound::Finished); break;
    case Type::BoilSoon: audio::play(audio::Sound::BoilSoon); break;
    case Type::BoilStalled: audio::play(audio::Sound::Prompt); break;
    case Type::SensorFault:
    case Type::SafetyCut:
    case Type::AutotuneFailed: audio::play(audio::Sound::Fault); break;
    case Type::AutotuneDone: {
      audio::play(audio::Sound::Finished);
      std::string err;
      storage::saveSettings(err);  // keep the new PID gains
      break;
    }
    default: break;
  }
  if (mode == Mode::Auto && brewlog::active() && e.type != events::Type::PumpRest) {
    std::string text = eventText(e);
    if (!text.empty()) brewlog::event(logT(), text);
  }
  for (EventListener l : listeners)
    if (l) l(e);
}

void handleButtons(uint32_t now) {
  io::Press s = io::takePress(io::Button::Start);
  if (s == io::Press::Short) {
    if (confirm != Confirm::None || st.prompt != brew::Prompt::None) answer(true);
    else if (mode == Mode::Idle && resumeOffered) resume();
    else if (mode != Mode::Idle) pauseResume();
  } else if (s == io::Press::Long) {
    requestSkipOrStop();
  }

  io::Press p = io::takePress(io::Button::Pump);
  if (p == io::Press::Short) {
    pumpToggle();
  } else if (p == io::Press::Long) {
    holdingPump = true;
    pumpHold(true);
  }
  if (holdingPump && !io::isHeld(io::Button::Pump)) {
    holdingPump = false;
    pumpHold(false);
  }

  if (confirm != Confirm::None && now - confirmAt > kConfirmTimeoutMs) confirm = Confirm::None;
}

void updateLeds(uint32_t now) {
  bool blink = (now / 400) % 2;
  bool waiting = confirm != Confirm::None || st.prompt != brew::Prompt::None ||
                 st.paused || faults;
  bool startLed = mode != Mode::Idle ? (!waiting || blink) : (resumeOffered && blink);
  io::setLed(io::Button::Start, startLed);
  bool fastBlink = (now / 150) % 2;
  io::setLed(io::Button::Pump, pump.priming() ? fastBlink : pump.on());
}

uint8_t fanDemand(uint32_t now) {
  const auto& f = settings::current().fan;
  if (sensors::assigned(SensorRole::Heatsink)) {
    float hs;
    if (!sensors::read(SensorRole::Heatsink, hs)) return 100;
    if (hs < f.onTempC) return 0;
    if (hs >= f.fullTempC) return 100;
    float span = f.fullTempC - f.onTempC;
    return f.minPct + (uint8_t)((100 - f.minPct) * (hs - f.onTempC) / (span > 0 ? span : 1));
  }
  if (heater.dutyPct() > 0) {
    lastHeatAt = now;
    heatedOnce = true;
  }
  bool runOn = heatedOnce && now - lastHeatAt < f.runOnSec * 1000UL;
  return (heater.dutyPct() > 0 || runOn) ? 100 : 0;
}

}  // namespace

void begin() {
  sensors::begin();
  outputs::begin();
  outputs::setTestAllowed(true);
  lastReal = millis();
  resumeOffered = storage::loadResume(resumeSnap, resumeRecipe);
}

void addEventListener(EventListener fn) {
  for (EventListener& l : listeners) {
    if (!l) {
      l = fn;
      return;
    }
  }
}

void loop() {
  const auto& s = settings::current();
  audio::setVolume(s.sound.volume);
  uint32_t now = millis();
  uint32_t dtReal = now - lastReal;
  lastReal = now;
  float vdtF = dtReal * (simOn ? simSpeed : 1.0f) + vfrac;
  uint32_t vdt = (uint32_t)vdtF;
  vfrac = vdtF - vdt;
  vnow += vdt;

  // ---- Sensors ----------------------------------------------------------
  sensors::loop();
  if (simOn) {
    sim.step(vdt, heater.onFraction());
    sensors::setOverride(controlRole(), sim.sensorC());
  }
  float temp = 0;
  bool tempOk = sensors::read(controlRole(), temp);

  // ---- Safety -----------------------------------------------------------
  uint8_t newFaults = 0;
  if (sensors::criticalFault() || (mode != Mode::Idle && !tempOk)) newFaults |= kFaultSensor;
  if (tempOk && temp > s.safety.maxTempC) newFaults |= kFaultOverTemp;
  float hs;
  if (sensors::read(SensorRole::Heatsink, hs) && hs >= s.fan.alarmTempC) newFaults |= kFaultHeatsink;
  bool level = io::levelSensorClosed();
  if ((s.safety.level == settings::LevelMode::RequireClosed && !level) ||
      (s.safety.level == settings::LevelMode::RequireOpen && level))
    newFaults |= kFaultLevel;
  // Runaway: hotter and hotter with the heater off for more than 2 minutes.
  if (mode != Mode::Idle && !simOn && tempOk && s.safety.runawayC > 0 && heater.dutyPct() <= 0) {
    if (!offTracking) {
      offTracking = true;
      offSince = vnow;
      offMinC = temp;
    } else if (vnow - offSince < kRunawaySettleMs || temp < offMinC) {
      offMinC = temp;
    } else if (temp - offMinC > s.safety.runawayC) {
      runaway = true;
    }
  } else {
    offTracking = false;
  }
  if (runaway) newFaults |= kFaultRunaway;

  uint8_t raised = newFaults & ~faults;
  faults = newFaults;
  if (mode != Mode::Idle && raised) {
    events::post(raised & kFaultSensor ? events::Type::SensorFault : events::Type::SafetyCut, raised);
    faultBeepAt = now;
    if (raised & kFaultSensor) {
      // Stop driving until the user deliberately continues.
      if (mode == Mode::Auto) process.setPaused(true);
      if (mode == Mode::Manual) {
        manual.setHeater(false);
        manual.setPump(false);
      }
      if (mode == Mode::Autotune) {
        events::post(events::Type::AutotuneFailed);
        toIdle();
      }
    }
  }
  if (mode != Mode::Idle && faults && now - faultBeepAt > kFaultReminderMs) {
    faultBeepAt = now;
    audio::play(audio::Sound::Fault);
  }
  bool sensorCut = faults & kFaultSensor;
  bool heatCut = faults != 0;

  handleButtons(now);

  // ---- Mode -------------------------------------------------------------
  HeatDemand hd;
  PumpRequest pr;
  switch (mode) {
    case Mode::Idle:
      break;
    case Mode::Auto: {
      brew::Output o = process.tick(temp, vdt, pump.priming());
      if (o.newStep) pump.newStep();
      if (o.startPrime) pump.startPrime(vnow);
      hd = o.heat;
      pr = o.pump;
      if (!process.running()) toIdle();
      else saveResumePoint(now);
      break;
    }
    case Mode::Manual: {
      Manual::Output o = manual.tick(temp, vdt);
      hd = o.heat;
      pr = o.pump;
      break;
    }
    case Mode::Autotune: {
      Autotune& t = heater.autotune();
      hd.mode = HeatMode::Autotune;
      pr.mode = s.autotune.pumpOn ? PumpMode::On : PumpMode::Off;
      if (t.state() == Autotune::State::Done) {
        auto& h = settings::current().heater;
        h.kp = t.kp();
        h.ki = t.ki();
        h.kd = t.kd();
        events::post(events::Type::AutotuneDone);
        toIdle();
        hd.mode = HeatMode::Off;
      } else if (t.state() != Autotune::State::Running) {
        events::post(events::Type::AutotuneFailed);
        toIdle();
        hd.mode = HeatMode::Off;
      }
      break;
    }
  }

  heater.setStages(outputs::heaterCount());
  bool paused = mode == Mode::Auto && process.paused();
  if (heatCut || paused || pump.resting() || hd.mode == HeatMode::Off) heater.off();
  else heater.update(hd, temp, vnow);

  if (sensorCut || (paused && !holdingPump)) pump.off();
  else pump.update(pr, temp, vnow);

  // ---- Sparge water heater: thermostat on its own sensor ------------------
  float spargeT = 0;
  bool spargeOk = sensors::read(SensorRole::Sparge, spargeT);
  bool spargeHw = outputs::hasRole(OutputRole::Sparge) && sensors::assigned(SensorRole::Sparge);
  if (!spargeHw) spargeOn = spargeAuto = false;
  if (spargeAuto && mode == Mode::Auto && process.phase() == brew::Phase::Boil && process.reached())
    spargeOn = spargeAuto = false;  // sparging is over once it boils
  if (spargeOn && spargeOk && spargeT < s.safety.maxTempC) {
    if (spargeT <= spargeSetC - s.sparge.hystC) spargeHeating = true;
    else if (spargeT >= spargeSetC) spargeHeating = false;
  } else {
    spargeHeating = false;  // off, or no reading: never heat blind
  }

  // ---- Outputs ----------------------------------------------------------
  outputs::Demand d;
  bool drive = !simOn || s.sim.driveOutputs;
  bool attention = confirm != Confirm::None || process.prompt() != brew::Prompt::None;
  if (drive) {
    for (int i = 0; i < settings::kMaxHeaters; i++) d.heater[i] = heater.stageOn(i);
    d.pump = pump.on();
    d.sparge = spargeHeating;
    // Safety relay: closed only while something may heat and nothing is wrong.
    d.contactor = faults == 0 && (mode != Mode::Idle || spargeOn);
  }
  d.alarm = faults ? true : (attention && (now / 500) % 2);
  d.fanPct = fanDemand(now);
  d.safetyCut = sensorCut;
  outputs::apply(d);

  // ---- Events and status ------------------------------------------------
  events::Event e;
  while (events::take(e)) handleEvent(e);

  st.mode = mode;
  st.paused = paused;
  st.faults = faults;
  st.confirm = confirm;
  st.tempOk = tempOk;
  st.tempC = temp;
  st.heaterZone = heater.zone();
  st.heaterDuty = heater.dutyPct();
  st.heaterOn = heater.on();
  st.boilPowerPct = heater.boilPowerPct();
  st.pumpOn = pump.on();
  st.pumpResting = pump.resting();
  st.pumpOverride = pump.overridden();
  st.priming = pump.priming();
  st.fanPct = d.fanPct;
  st.heaters = heater.stages();
  st.heatersOn = 0;
  for (int i = 0; i < heater.stages(); i++)
    if (heater.stageOn(i)) st.heatersOn |= 1 << i;
  st.spargeAvailable = spargeHw;
  st.spargeOn = spargeOn;
  st.spargeHeating = spargeHeating;
  st.spargeTempOk = spargeOk;
  st.spargeTempC = spargeT;
  st.spargeSetC = spargeSetC;
  st.clockS = vnow / 1000;
  st.epoch = epochNow();
  st.sim = simOn;
  st.resumeOffered = resumeOffered;
  st.resumePhase = resumeSnap.phase;
  st.resumeStep = resumeSnap.step;
  st.simSpeed = simSpeed;

  st.phase = process.phase();
  st.prompt = process.prompt();
  st.step = process.step();
  st.stepCount = process.stepCount();
  st.stepName = process.stepName();
  st.lastAddition = process.lastAddition();
  st.manualHeater = manual.heater();
  st.manualPump = manual.pump();
  st.manualCountdown = manual.countdownActive();
  st.tuneCycles = heater.autotune().cyclesDone();
  st.tuneCyclesWanted = heater.autotune().cyclesWanted();

  switch (mode) {
    case Mode::Auto:
      st.setpointC = process.setpointC();
      st.reached = process.reached();
      st.timerCounting = process.timerCounting();
      st.remainingMs = process.remainingMs();
      st.elapsedMs = process.elapsedMs();
      break;
    case Mode::Manual:
      st.setpointC = manual.setpointC();
      st.reached = manual.reached();
      st.timerCounting = manual.reached();
      st.remainingMs = manual.remainingMs();
      st.elapsedMs = manual.elapsedMs();
      break;
    case Mode::Autotune:
      st.setpointC = heater.autotune().setpoint();
      st.reached = false;
      st.timerCounting = false;
      st.remainingMs = st.elapsedMs = 0;
      break;
    case Mode::Idle:
      st.setpointC = 0;
      st.reached = st.timerCounting = false;
      st.remainingMs = st.elapsedMs = 0;
      break;
  }

  // ---- History and brew log -----------------------------------------------
  history::update(st.clockS, tempOk, temp, mode != Mode::Idle, st.setpointC, (uint8_t)st.heaterDuty, st.pumpOn);
  if (mode == Mode::Auto && brewlog::active() && tempOk && vnow - lastLogV >= kLogSampleMs) {
    lastLogV = vnow;
    brewlog::sample(logT(), temp, st.setpointC, (uint8_t)st.heaterDuty, st.pumpOn);
  }

  // Remind about an unanswered prompt.
  if (st.prompt == brew::Prompt::None) promptSince = now;
  else if (now - promptSince > kPromptReminderMs) {
    promptSince = now;
    audio::play(audio::Sound::Reminder);
  }

  updateLeds(now);
}

const Status& status() { return st; }

bool startAuto(uint16_t delayMin) {
  if (!canStart()) return false;
  prepare();
  process.start(settings::recipe(), settings::current(), delayMin);
  mode = Mode::Auto;
  logStartV = lastLogV = vnow;
  brewlog::begin(settings::recipe().name, epochNow(), simOn);
  brewlog::event(0, "Brew started");
  // Sparge water: start heating now, so it is ready when the bag comes out.
  const settings::Recipe& r = settings::recipe();
  if (r.spargeTempC > 0 && settings::current().sparge.autoWithBrew && setSparge(true)) {
    spargeSetC = r.spargeTempC;
    spargeAuto = true;
  }
  error = "";
  return true;
}

bool resume() {
  if (!resumeOffered) return refuse("nothing to resume");
  if (!canStart()) return false;
  resumeOffered = false;  // keep the file: it is rewritten as the brew goes on
  prepare();
  if (!process.restore(resumeRecipe, settings::current(), resumeSnap)) {
    storage::clearResume();
    return refuse("resume point not usable");
  }
  mode = Mode::Auto;
  logStartV = lastLogV = vnow;
  if (!brewlog::resume()) brewlog::begin(resumeRecipe.name, epochNow(), false);
  brewlog::event(0, "Resumed after power loss");
  return true;
}

void discardResume() { dropResumeOffer(); }

bool startManual() {
  if (!canStart()) return false;
  prepare();
  manual.start(settings::current());
  mode = Mode::Manual;
  return true;
}

bool startAutotune(float setpointC) {
  if (!canStart()) return false;
  prepare();
  const auto& a = settings::current().autotune;
  heater.autotune().start(setpointC, a.outputHighPct, a.outputLowPct, a.hysteresisC, a.cycles,
                          a.timeoutMin * 60000UL, vnow);
  mode = Mode::Autotune;
  return true;
}

void stop() { toIdle(); }

void pauseResume() {
  if (mode == Mode::Auto) {
    if (process.paused() && (faults & kFaultSensor)) {
      error = "sensor fault";
      audio::play(audio::Sound::Fault);
      return;
    }
    process.setPaused(!process.paused());
    if (brewlog::active()) brewlog::event(logT(), process.paused() ? "Paused" : "Resumed");
  } else if (mode == Mode::Manual) {
    // Manual mode: pause/resume heating.
    if (!manual.heater() && (faults & kFaultSensor)) {
      error = "sensor fault";
      return;
    }
    manual.setHeater(!manual.heater());
  }
}

void skip() {
  if (mode == Mode::Auto) process.skip();
}

void answer(bool yes) {
  if (confirm != Confirm::None) {
    Confirm c = confirm;
    confirm = Confirm::None;
    if (!yes) return;
    if (c == Confirm::Skip) skip();
    else stop();
    return;
  }
  if (mode == Mode::Auto) process.answer(yes);
}

void requestSkipOrStop() {
  if (mode == Mode::Idle) return;
  if (mode == Mode::Auto && confirm != Confirm::Skip) confirm = Confirm::Skip;
  else confirm = Confirm::Stop;
  confirmAt = millis();
  audio::play(audio::Sound::Click);
}

void pumpToggle() {
  if (mode == Mode::Manual) manual.setPump(!manual.pump());
  else if (mode == Mode::Auto || mode == Mode::Autotune) pump.toggleOverride();
}

void pumpHold(bool held) { pump.setHoldPrime(held); }

void pumpPrime() {
  pump.configure(settings::current().pump);
  pump.startPrime(vnow);
}

void heaterToggle() {
  if (mode == Mode::Manual) pauseResume();
}

void setSetpoint(float c) {
  if (mode == Mode::Manual) manual.setSetpoint(c);
  else if (mode == Mode::Auto) process.setStepTemp(c);
}

void setBoilPower(uint8_t pct) { heater.setBoilPowerPct(pct); }

void setTimerMin(uint16_t min) {
  if (mode == Mode::Manual) manual.setCountdownMin(min);
  else if (mode == Mode::Auto) process.setRemainingMin(min);
}

void startTimerNow() {
  if (mode == Mode::Auto && st.tempOk) process.startTimerNow(st.tempC);
}

bool setSparge(bool on) {
  if (!on) {
    spargeOn = spargeAuto = false;
    return true;
  }
  if (!outputs::hasRole(OutputRole::Sparge)) return refuse("no output has the sparge heater role");
  float t;
  if (!sensors::read(SensorRole::Sparge, t)) return refuse("no sparge sensor reading");
  if (!spargeOn) spargeSetC = settings::current().sparge.tempC;
  spargeOn = true;
  spargeAuto = false;
  return true;
}

void setSpargeTemp(float c) {
  if (c < 20) c = 20;
  if (c > 99) c = 99;
  spargeSetC = c;
}

uint32_t epochNow() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;  // before that: the clock was never set
}

void setClock(uint32_t epoch) {
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
}

std::string eventText(const events::Event& e) {
  using events::Type;
  char buf[64];
  switch (e.type) {
    case Type::Addition: {
      const auto& adds = process.recipe().additions;
      if (e.arg < 0 || e.arg >= (int)adds.size()) return "Addition";
      const auto& a = adds[e.arg];
      if (a.amount > 0) snprintf(buf, sizeof(buf), ": %g %s", a.amount, a.unit.c_str());
      else buf[0] = 0;
      return "Add: " + a.name + buf;
    }
    case Type::PromptOpened: return brew::promptName((brew::Prompt)e.arg);
    case Type::StepStarted:
      if ((brew::Phase)e.arg == brew::Phase::Mash) return std::string("Mash: ") + process.stepName();
      return brew::phaseName((brew::Phase)e.arg);
    case Type::TempReached: return "Temperature reached";
    case Type::StepDone: return "Step done";
    case Type::TimerDone: return "Countdown finished";
    case Type::BrewFinished: return "Brew finished";
    case Type::BoilSoon: return "Close to the boil";
    case Type::BoilStalled: return "Temperature stopped rising";
    case Type::SensorFault: return "Sensor fault: heater and pump off";
    case Type::SafetyCut:
      return e.arg & kFaultRunaway ? "Runaway: heating with the heater off" : "Safety cut: heater off";
    case Type::AutotuneDone: {
      const auto& h = settings::current().heater;
      snprintf(buf, sizeof(buf), "kp %.2f ki %.4f kd %.1f (saved)", h.kp, h.ki, h.kd);
      return buf;
    }
    case Type::AutotuneFailed: return "Autotune did not finish";
    case Type::PumpRest: return e.arg ? "Pump rest" : "Pump rest over";
  }
  return "";
}

void setSim(bool on, float speed) {
  if (speed < 1) speed = 1;
  if (speed > 100) speed = 100;
  simSpeed = speed;
  if (on && !simOn) sim.reset(settings::current().sim);
  sim.configure(settings::current().sim);
  simOn = on;
  if (on) {
    sensors::setOverride(controlRole(), sim.sensorC());
  } else {
    sensors::clearOverride();
    if (mode != Mode::Idle) toIdle();  // the real sensor reads something else
  }
}

void setSimTemp(float c) { sim.setWaterC(c); }

const char* lastError() { return error; }

const settings::Recipe& brewRecipe() { return process.recipe(); }

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Idle: return "idle";
    case Mode::Auto: return "auto";
    case Mode::Manual: return "manual";
    case Mode::Autotune: return "autotune";
  }
  return "?";
}

const char* zoneName(Heater::Zone z) {
  switch (z) {
    case Heater::Zone::Off: return "off";
    case Heater::Zone::Full: return "full";
    case Heater::Zone::Pid: return "pid";
    case Heater::Zone::Boil: return "boil";
    case Heater::Zone::Fixed: return "fixed";
    case Heater::Zone::Autotune: return "tune";
  }
  return "?";
}

}  // namespace controller
