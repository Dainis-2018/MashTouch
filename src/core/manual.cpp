// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from Open ArdBir 2.8.3-RC manual_mode (see NOTICE.md).

#include "manual.h"

#include "events.h"

void Manual::start(const settings::Settings&) {
  active_ = true;
  heater_ = false;
  pump_ = false;
  reached_ = false;
  countdown_ = false;
  countUp_.reset(0);
  countdownTimer_.reset(0);
}

void Manual::setCountdownMin(uint16_t min) {
  countdown_ = min > 0;
  countdownTimer_.reset(min * 60000UL);
}

Manual::Output Manual::tick(float tempC, uint32_t dtMs) {
  Output out;
  if (!active_) return out;

  if (!reached_ && tempC >= setpoint_) {
    reached_ = true;
    countUp_.reset(0);
    events::post(events::Type::TempReached);
  } else if (reached_ && tempC + lostBandC_ < setpoint_) {
    reached_ = false;  // setpoint raised or heat lost: start over
  }
  if (reached_) {
    countUp_.tick(dtMs);
    if (countdown_) {
      countdownTimer_.tick(dtMs);
      if (countdownTimer_.done()) {
        countdown_ = false;
        events::post(events::Type::TimerDone);
      }
    }
  }

  if (heater_) {
    out.heat.mode = HeatMode::Regulate;
    out.heat.setpointC = setpoint_;
  }
  out.pump.mode = pump_ ? settings::PumpMode::On : settings::PumpMode::Off;
  out.pump.stepTempC = setpoint_;
  out.pump.reached = reached_;
  return out;
}
