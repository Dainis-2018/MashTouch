// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Heater control, adapted from Open ArdBir PID_HEAT (electric mode): full
// power far below the setpoint, PID near it, fixed power once boiling,
// output as a time-proportional on/off signal. Adds relay autotune.
#pragma once
#include <stdint.h>

#include "pid.h"
#include "settings.h"

enum class HeatMode : uint8_t { Off, Regulate, Fixed, Autotune };

struct HeatDemand {
  HeatMode mode = HeatMode::Off;
  float setpointC = 0;
  uint8_t fixedPct = 0;
  // Boil control (full power, then boil power) whatever the setpoint: the
  // boil phase, also when the kettle boils below the configured temperature.
  bool boil = false;
  // Power limit outside the boil (mash: grain in the kettle). 100 = none.
  uint8_t maxPct = 100;
};

class Heater {
 public:
  enum class Zone : uint8_t { Off, Full, Pid, Boil, Fixed, Autotune };

  void configure(const settings::Heater& s, float boilTempC);
  // Number of heater outputs (1..kMaxHeaters). The demand is shared in
  // stages: with 2 heaters, 0-50 % switches heater 1 and 50-100 % adds
  // heater 2 on top. Their windows are offset so they do not switch together.
  void setStages(int n) { stages_ = n < 1 ? 1 : (n > settings::kMaxHeaters ? settings::kMaxHeaters : n); }
  int stages() const { return stages_; }

  // Computes the duty for this loop and the on/off state of the window.
  void update(const HeatDemand& d, float tempC, uint32_t now);
  // Immediate off (fault, pause); restarts the PID next time.
  void off();

  // Any heater on / heater i (0-based) on.
  bool on() const { return on_; }
  bool stageOn(int i) const { return i >= 0 && i < stages_ && stageOn_[i]; }
  // Share of the total power that is on right now (0..1), for the simulator.
  float onFraction() const;
  float dutyPct() const { return duty_; }
  Zone zone() const { return zone_; }

  uint8_t boilPowerPct() const { return boilPct_; }
  void setBoilPowerPct(uint8_t pct) { boilPct_ = pct > 100 ? 100 : pct; }

  Autotune& autotune() { return tune_; }

 private:
  settings::Heater cfg_;
  float boilTempC_ = 99;
  uint8_t boilPct_ = 80;
  Pid pid_;
  Autotune tune_;
  Zone zone_ = Zone::Off;
  float duty_ = 0;
  bool on_ = false;
  int stages_ = 1;
  bool stageOn_[settings::kMaxHeaters] = {};
  bool boiling_ = false;
  float lastSetpoint_ = -1000;
  uint32_t windowStart_ = 0;
};
