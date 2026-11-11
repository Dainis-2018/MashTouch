// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pump rules, adapted from Open ArdBir pump_rest / pump_prime: per-step
// mode (off, on, rest cycles), stop temperature near the boil, priming
// pulses. On/off only (any pump voltage: 12 V direct, or SSR/relay for
// 230 V); adds manual override until the next step and prime-while-held.
#pragma once
#include <stdint.h>

#include "settings.h"

struct PumpRequest {
  settings::PumpMode mode = settings::PumpMode::Off;
  float stepTempC = 0;        // for rest-cycle early restart
  bool reached = false;       // step temperature reached: cycles count from here
  bool stopTempApplies = true;
};

class Pump {
 public:
  void configure(const settings::Pump& s) { cfg_ = s; }

  // New step: clear the override and restart the rest cycle.
  void newStep();

  // Toggle relative to the current state; holds until newStep().
  void toggleOverride();
  void clearOverride() { override_ = Override::None; }
  bool overridden() const { return override_ != Override::None; }

  // Automatic priming pulses (Open ArdBir pump_prime), non-blocking.
  void startPrime(uint32_t now);
  void stopPrime() { primeStep_ = 0; }
  bool priming() const { return primeStep_ > 0; }

  // Pump button held: run while true.
  void setHoldPrime(bool held) { holdPrime_ = held; }

  void update(const PumpRequest& r, float tempC, uint32_t now);
  void off();

  bool on() const { return on_; }
  // In a rest period of a cycle: the heater must stay off.
  bool resting() const { return resting_; }

 private:
  enum class Override : uint8_t { None, On, Off };

  settings::Pump cfg_;
  Override override_ = Override::None;
  bool holdPrime_ = false;
  uint8_t primeStep_ = 0;  // 1..2*pulses, 0 = not priming
  uint32_t primeAt_ = 0;

  bool on_ = false;
  bool resting_ = false;
  bool stopped_ = false;  // stop-temperature latch
  uint32_t cycleMs_ = 0;  // time into the current run/rest cycle
  uint32_t lastUpdate_ = 0;
  bool haveLast_ = false;
};
