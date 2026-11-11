// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Manual mode, adapted from Open ArdBir manual_mode: adjustable setpoint,
// heater and pump toggles, count-up timer once the temperature is reached,
// optional countdown, boil power when the setpoint is at the boil.
#pragma once
#include <stdint.h>

#include "heater.h"
#include "pump.h"
#include "settings.h"
#include "timer.h"

class Manual {
 public:
  void start(const settings::Settings& s);
  void stop() { active_ = false; }
  bool active() const { return active_; }

  void setSetpoint(float c) { setpoint_ = c; }
  float setpointC() const { return setpoint_; }
  void setHeater(bool on) { heater_ = on; }
  bool heater() const { return heater_; }
  void setPump(bool on) { pump_ = on; }
  bool pump() const { return pump_; }

  // Countdown in minutes; it runs while the temperature is reached.
  // 0 cancels it.
  void setCountdownMin(uint16_t min);
  bool countdownActive() const { return countdown_; }

  struct Output {
    HeatDemand heat;
    PumpRequest pump;
  };
  Output tick(float tempC, uint32_t dtMs);

  bool reached() const { return reached_; }
  uint32_t elapsedMs() const { return countUp_.elapsedMs(); }
  uint32_t remainingMs() const { return countdownTimer_.remainingMs(); }

 private:
  bool active_ = false;
  float setpoint_ = 35;
  float lostBandC_ = 2.0f;  // reached state resets this far below the setpoint
  bool heater_ = false;
  bool pump_ = false;
  bool reached_ = false;
  bool countdown_ = false;
  StepTimer countUp_;
  StepTimer countdownTimer_;
};
