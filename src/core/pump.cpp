// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pump rules adapted from Open ArdBir 2.8.3-RC pump_rest and pump_prime
// (see NOTICE.md).

#include "pump.h"

#include "events.h"

using settings::PumpMode;

void Pump::newStep() {
  override_ = Override::None;
  cycleMs_ = 0;
  if (resting_) events::post(events::Type::PumpRest, 0);
  resting_ = false;
}

void Pump::toggleOverride() { override_ = on_ ? Override::Off : Override::On; }

void Pump::startPrime(uint32_t now) {
  primeStep_ = 1;
  primeAt_ = now;
}

void Pump::off() {
  on_ = false;
  haveLast_ = false;
}

void Pump::update(const PumpRequest& r, float tempC, uint32_t now) {
  uint32_t dt = haveLast_ ? now - lastUpdate_ : 0;
  lastUpdate_ = now;
  haveLast_ = true;

  // Priming, the held button and the override replace the cycle: no rest.
  if ((holdPrime_ || primeStep_ || override_ != Override::None) && resting_) {
    resting_ = false;
    events::post(events::Type::PumpRest, 0);
  }

  // Priming and the held button override everything except the fault gate.
  if (holdPrime_) {
    on_ = true;
    return;
  }
  if (primeStep_) {
    // Odd steps on (750 ms + 250 ms per pulse), even steps off 350 ms.
    uint8_t pulse = (primeStep_ + 1) / 2;
    bool pulseOn = primeStep_ % 2 == 1;
    uint32_t len = pulseOn ? 750 + pulse * 250 : 350;
    if (now - primeAt_ >= len) {
      primeAt_ = now;
      primeStep_++;
      if (primeStep_ > cfg_.primePulses * 2) primeStep_ = 0;
    }
    if (primeStep_) {
      on_ = primeStep_ % 2 == 1;
      return;
    }
  }

  if (override_ != Override::None) {
    on_ = override_ == Override::On;
    return;
  }

  bool want = false;
  bool wasResting = resting_;
  resting_ = false;
  switch (r.mode) {
    case PumpMode::Off:
      break;
    case PumpMode::On:
      want = true;
      break;
    case PumpMode::Cycle: {
      if (!r.reached) {
        want = true;
        cycleMs_ = 0;
        break;
      }
      cycleMs_ += dt;
      uint32_t run = cfg_.cycleMin * 60000UL;
      uint32_t rest = cfg_.restMin * 60000UL;
      if (cycleMs_ < run) {
        want = true;
      } else if (cycleMs_ >= run + rest || r.stepTempC - tempC > cfg_.restRestartDropC) {
        cycleMs_ = 0;
        want = true;
      } else {
        resting_ = true;
      }
      break;
    }
  }
  if (resting_ != wasResting) events::post(events::Type::PumpRest, resting_ ? 1 : 0);

  if (want && r.stopTempApplies) {
    bool sensorNeedsFlow = cfg_.externalSensor && !r.reached;
    if (tempC >= cfg_.stopTempC) stopped_ = true;
    else if (tempC < cfg_.stopTempC - cfg_.stopHystC) stopped_ = false;
    if (stopped_ && !sensorNeedsFlow) want = false;
  }

  on_ = want;
}
