// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings and recipe model. All temperatures are stored in degrees C;
// Fahrenheit is a display option only. Defaults live here; loading and
// saving as JSON on LittleFS comes with the storage module.
// Setting meanings follow Open ArdBir 2.8.3-RC (see NOTICE.md); the
// structure is new.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace settings {

// ---- Outputs -------------------------------------------------------------

// Physical channels: 0..3 = OUT1..OUT4 (signal), 4..5 = PWR1..PWR2 (12 V PWM).
constexpr int kOutputChannels = 6;
constexpr int kFirstPowerChannel = 4;

// Heater 1..n share the kettle power in stages (see core/heater.h). Sparge
// is the sparge water heater with its own sensor. Alarm is a light or horn:
// on at a fault, blinking while a prompt waits. Contactor is a safety relay
// in the mains feed of the heaters: closed only while a mode runs without a
// fault, so a welded SSR cannot keep heating.
enum class OutputRole : uint8_t { None, Heater, Pump, Fan, Sparge, Alarm, Contactor };
constexpr int kMaxHeaters = 4;

struct OutputConfig {
  OutputRole role = OutputRole::None;
  uint8_t index = 1;      // heater 1..kMaxHeaters, pump 1..n
  bool inverted = false;  // active low
};

// ---- Sensors -------------------------------------------------------------

enum class SensorRole : uint8_t { None, Kettle, Rims, Heatsink, Aux, Sparge };

struct SensorConfig {
  uint8_t rom[8] = {};
  SensorRole role = SensorRole::None;
  float offsetC = 0;
  std::string name;
};

// ---- Control -------------------------------------------------------------

struct Heater {
  // PID gains, output in % (0..100): kp in %/C, ki in %/(C*s), kd in %*s/C.
  float kp = 30.0f;
  float ki = 0.05f;
  float kd = 0.0f;
  uint32_t sampleMs = 2000;
  // Time-proportional window: on for duty% of each window.
  uint32_t windowMs = 5000;
  // Full power while further than this below the setpoint, PID inside it.
  float fullPowerBandC = 3.5f;
  // Narrower band once within fullPowerBandC of the boil temperature.
  float nearBoilBandC = 1.0f;
  // Boil: fixed power once the boil temperature is reached; held until the
  // temperature drops boilHystC below it.
  uint8_t boilPowerPct = 80;
  float boilHystC = 1.0f;
  // Power limit during the mash (grain in the kettle: avoids scorching on
  // the element). 100 = no limit. Heating strike water is not limited.
  uint8_t mashPowerPct = 100;
  SensorRole controlSensor = SensorRole::Kettle;
};

struct Autotune {
  float outputHighPct = 100;
  float outputLowPct = 0;
  float hysteresisC = 0.2f;
  uint8_t cycles = 4;            // full oscillations measured after the first
  uint32_t timeoutMin = 240;
  bool pumpOn = true;
};

enum class PumpMode : uint8_t { Off, On, Cycle };

struct Pump {
  // Rest cycles during mash steps in Cycle mode: run, then rest (heater off).
  uint8_t cycleMin = 10;
  uint8_t restMin = 2;
  // A rest ends early if the temperature falls this far below the step.
  float restRestartDropC = 1.0f;
  // Stop above this temperature to avoid cavitation near the boil.
  float stopTempC = 95.0f;
  float stopHystC = 2.0f;
  // Sensor in the pump outlet: the pump must run to give a valid reading,
  // so the stop temperature only applies once the step temperature is reached.
  bool externalSensor = false;
  bool inBoil = false;
  bool duringCooling = false;
  bool autoPrime = true;
  uint8_t primePulses = 5;
};

enum class Whirlpool : uint8_t { Off, Hot, Cold };

struct Process {
  float boilTempC = 99.0f;
  // A step counts as reached at setpoint minus this.
  float reachedTolC = 0.2f;
  bool skipRemoveMalt = false;
  bool heatDuringRemoveMalt = false;  // hold the last step temperature
  bool iodineTest = false;
  uint16_t iodineTimeoutMin = 0;      // 0 = wait for confirmation
  Whirlpool whirlpool = Whirlpool::Off;
  uint8_t whirlpoolMin = 5;
  float coolingTargetC = 25.0f;
  // Warn this far below the boil temperature (boil-over watch). 0 = off.
  float boilWarnC = 3.0f;
};

struct Fan {
  // Without a heatsink sensor: on while any heater is on, plus run-on.
  uint16_t runOnSec = 120;
  // With a heatsink sensor: thermostatic.
  float onTempC = 40;
  float fullTempC = 55;
  float alarmTempC = 70;  // heater cut
  uint8_t minPct = 40;
};

enum class LevelMode : uint8_t { Ignore, RequireClosed, RequireOpen };

struct Safety {
  float maxTempC = 110;  // heater cut above this on the control sensor
  LevelMode level = LevelMode::Ignore;
  // Runaway: the kettle gets this much hotter while the heater is off (an
  // SSR that failed closed). Cuts everything and opens the contactor. 0 = off.
  float runawayC = 5.0f;
};

// Sparge water heater: a simple thermostat on the sparge sensor.
struct Sparge {
  float tempC = 76.0f;
  float hystC = 1.0f;
  // Switch it on when a brew with a sparge temperature starts, off at the boil.
  bool autoWithBrew = true;
};

struct Sound {
  uint8_t volume = 70;  // 0..100
};

struct Net {
  std::string ssid;            // home network; empty = setup access point only
  std::string password;
  std::string hostname = "mashtouch";  // http://mashtouch.local
  std::string apPassword = "mashtouch";  // setup access point (8+ characters)
  std::string webPassword;     // empty = no login for the web UI (user "admin")
  // POSIX time zone for the clock (NTP); default: Riga, Helsinki, Kyiv.
  std::string timezone = "EET-2EEST,M3.5.0/3,M10.5.0/4";
};

struct Sim {
  float speed = 1.0f;     // time acceleration
  float liters = 20.0f;
  float heaterW = 2500.0f;
  float lossWPerK = 8.0f;
  float ambientC = 20.0f;
  float startC = 15.0f;
  float sensorTauS = 10.0f;
  bool driveOutputs = false;  // false: physical heater/pump stay off
};

struct Settings {
  Heater heater;
  Autotune autotune;
  Pump pump;
  Process process;
  Fan fan;
  Safety safety;
  Sparge sparge;
  Sound sound;
  Sim sim;
  Net net;
  OutputConfig outputs[kOutputChannels];
  std::vector<SensorConfig> sensors;
  bool fahrenheit = false;
  uint8_t brightness = 200;  // display backlight 10..255
  std::string activeRecipe;  // recipe id (file name without .json)
};

// ---- Recipes -------------------------------------------------------------

// A mash step heats to its temperature, then rests for its minutes
// (0 = no rest: go on as soon as the temperature is reached).
struct MashStep {
  std::string name;
  float tempC = 0;
  uint16_t minutes = 0;
  PumpMode pump = PumpMode::On;
};

struct Addition {
  std::string name;
  uint16_t atMinutes = 0;  // minutes before the end of the boil
  float amount = 0;
  std::string unit;
};

// The mash has a fixed frame and a free middle:
//   mash in  -> "add malt" -> rests (0..n) -> mash out -> "remove malt"
// Mash in and mash out always exist when there is a mash; a mash out at the
// last rest's temperature with 0 minutes means "no mash out heating".
struct Recipe {
  std::string name;
  bool mash = true;  // false: extract brew, boil only
  MashStep mashIn;
  std::vector<MashStep> rests;
  MashStep mashOut;
  // Sparge water temperature, 0 = no sparge. Shown at "lift the bag out";
  // with a sparge heater and sensor it is also heated automatically.
  float spargeTempC = 0;
  uint16_t boilMinutes = 60;  // 0 = no boil
  std::vector<Addition> additions;

  Recipe();
};

// Builds mashIn / rests / mashOut from a plain ordered list of steps (old
// recipe files, BeerXML). mashOutIndex: the step that is the mash out, or -1
// to decide: the last step if it is at 75 C or more, otherwise a mash out
// at the last temperature with no rest is added. An empty list: no mash.
void arrangeMash(Recipe& r, std::vector<MashStep> steps, int mashOutIndex);

Settings& current();
Recipe& recipe();  // the active recipe
Settings defaults();
Recipe defaultRecipe();

const char* roleName(OutputRole r);
const char* roleName(SensorRole r);

}  // namespace settings
