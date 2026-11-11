// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ties the brewing core to the hardware: reads sensors, runs the active
// mode (automatic, manual, autotune), enforces the safety rules, drives
// heater/pump/fan outputs, LED rings and buzzer.
//
// The action functions below are the single action layer shared by the
// front buttons, the touch UI, the serial console and (later) the web API.
#pragma once
#include <stdint.h>

#include <string>

#include "core/brew.h"
#include "core/events.h"
#include "core/heater.h"

namespace controller {

enum class Mode : uint8_t { Idle, Auto, Manual, Autotune };

// Long-press confirmation: what Start (yes) would do.
enum class Confirm : uint8_t { None, Skip, Stop };

enum Fault : uint8_t {
  kFaultSensor = 1,    // control or other critical sensor has no reading
  kFaultOverTemp = 2,  // control sensor above the safety limit
  kFaultHeatsink = 4,  // heatsink too hot
  kFaultLevel = 8,     // level switch says no water
  kFaultRunaway = 16,  // kettle heats although the heater is off (SSR failed closed?)
};

struct Status {
  Mode mode = Mode::Idle;
  bool paused = false;
  uint8_t faults = 0;
  Confirm confirm = Confirm::None;

  bool tempOk = false;
  float tempC = 0;
  float setpointC = 0;

  Heater::Zone heaterZone = Heater::Zone::Off;
  float heaterDuty = 0;
  bool heaterOn = false;
  uint8_t boilPowerPct = 0;
  bool pumpOn = false;
  bool pumpResting = false;
  bool pumpOverride = false;
  bool priming = false;
  uint8_t fanPct = 0;
  uint8_t heaters = 1;    // kettle heater outputs (stages)
  uint8_t heatersOn = 0;  // bit i set: heater i+1 is on

  // Sparge water heater (needs an output and a sensor with the sparge role)
  bool spargeAvailable = false;
  bool spargeOn = false;       // thermostat enabled
  bool spargeHeating = false;  // heater on right now
  bool spargeTempOk = false;
  float spargeTempC = 0;
  float spargeSetC = 0;

  uint32_t clockS = 0;  // controller clock (history time base), seconds
  uint32_t epoch = 0;   // Unix time, 0 = clock not set

  // Automatic mode
  brew::Phase phase = brew::Phase::Idle;
  brew::Prompt prompt = brew::Prompt::None;
  int step = 0;
  int stepCount = 0;
  const char* stepName = "";
  bool reached = false;
  bool timerCounting = false;
  uint32_t remainingMs = 0;
  uint32_t elapsedMs = 0;
  int lastAddition = -1;

  // Manual mode
  bool manualHeater = false;
  bool manualPump = false;
  bool manualCountdown = false;

  // Autotune
  uint8_t tuneCycles = 0;
  uint8_t tuneCyclesWanted = 0;

  // Unfinished brew found at boot
  bool resumeOffered = false;
  brew::Phase resumePhase = brew::Phase::Idle;
  int resumeStep = 0;

  bool sim = false;
  float simSpeed = 1;
};

void begin();
void loop();
const Status& status();

// Called for every core event after the controller has handled it (up to 4).
using EventListener = void (*)(const events::Event&);
void addEventListener(EventListener fn);

// ---- Actions (return false and set lastError() when refused) -------------
bool startAuto(uint16_t delayMin = 0);
// Continue the brew that was running at power loss (Start short press
// does this too while idle), or forget it.
bool resume();
void discardResume();
bool startManual();
bool startAutotune(float setpointC);
void stop();
void pauseResume();
void skip();
// Answers the open prompt, or the skip/stop confirmation if one is open.
void answer(bool yes);
// Long press on Start: open the confirmation, or switch it Skip -> Stop.
void requestSkipOrStop();
void pumpToggle();
void pumpHold(bool held);
void pumpPrime();  // the automatic priming pulses
void heaterToggle();  // manual mode
void setSetpoint(float c);
void setBoilPower(uint8_t pct);
void setTimerMin(uint16_t min);
// Start the rest or boil timer now, although the setpoint is not reached.
void startTimerNow();
// Sparge water heater: thermostat on/off (any mode, also idle) and target.
bool setSparge(bool on);
void setSpargeTemp(float c);

// Clock: Unix time (0 = not set; set by NTP or from a browser).
uint32_t epochNow();
void setClock(uint32_t epoch);

// Short text for an event (brew log, web notices).
std::string eventText(const events::Event& e);

// Fake-heating mode: replaces the control sensor with a kettle model.
void setSim(bool on, float speed = 1);
void setSimTemp(float c);

// The recipe of the running automatic brew (its own copy).
const settings::Recipe& brewRecipe();

const char* lastError();
const char* modeName(Mode m);
const char* zoneName(Heater::Zone z);

}  // namespace controller
