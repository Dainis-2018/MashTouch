// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Control rules adapted from Open ArdBir 2.8.3-RC PID_HEAT (see NOTICE.md).

#include "heater.h"

void Heater::configure(const settings::Heater& s, float boilTempC) {
  cfg_ = s;
  boilTempC_ = boilTempC;
  boilPct_ = s.boilPowerPct;
  pid_.setTunings(s.kp, s.ki, s.kd);
  pid_.setSampleMs(s.sampleMs);
  pid_.reset();
}

void Heater::off() {
  zone_ = Zone::Off;
  duty_ = 0;
  on_ = false;
  for (bool& s : stageOn_) s = false;
  boiling_ = false;
  pid_.reset();
}

float Heater::onFraction() const {
  int n = 0;
  for (int i = 0; i < stages_; i++)
    if (stageOn_[i]) n++;
  return (float)n / stages_;
}

void Heater::update(const HeatDemand& d, float tempC, uint32_t now) {
  Zone zone = Zone::Off;
  float duty = 0;

  switch (d.mode) {
    case HeatMode::Off:
      off();
      return;

    case HeatMode::Fixed:
      zone = Zone::Fixed;
      duty = d.fixedPct;
      break;

    case HeatMode::Autotune:
      zone = Zone::Autotune;
      duty = tune_.update(tempC, now);
      break;

    case HeatMode::Regulate: {
      float sp = d.setpointC;
      if (sp != lastSetpoint_) {
        lastSetpoint_ = sp;
        pid_.reset();
        boiling_ = false;
      }
      if (d.boil || sp >= boilTempC_) {
        // Boil: full power up to the boil, then the boil power.
        if (tempC >= sp) boiling_ = true;
        else if (tempC < sp - cfg_.boilHystC) boiling_ = false;
        if (boiling_) {
          zone = Zone::Boil;
          duty = boilPct_;
        } else {
          zone = Zone::Full;
          duty = 100;
        }
        pid_.reset();
        break;
      }
      float band = tempC >= boilTempC_ - cfg_.fullPowerBandC ? cfg_.nearBoilBandC
                                                             : cfg_.fullPowerBandC;
      if (sp - tempC > band) {
        zone = Zone::Full;
        duty = 100;
        pid_.reset();  // start the PID fresh on entering the band
      } else {
        zone = Zone::Pid;
        duty = pid_.update(sp, tempC, now);
      }
      if (duty > d.maxPct) duty = d.maxPct;  // mash power limit
      break;
    }
  }

  if (zone_ == Zone::Off) windowStart_ = now;  // first window starts with heat
  zone_ = zone;
  duty_ = duty < 0 ? 0 : (duty > 100 ? 100 : duty);

  uint32_t window = cfg_.windowMs ? cfg_.windowMs : 1;
  if (now - windowStart_ >= window) {
    windowStart_ += window;
    if (now - windowStart_ >= window) windowStart_ = now;  // fell behind
  }
  // Stages: heater i carries the part of the demand between i/n and (i+1)/n.
  uint32_t pos = now - windowStart_;
  on_ = false;
  for (int i = 0; i < stages_; i++) {
    float part = duty_ * stages_ - i * 100.0f;
    part = part < 0 ? 0 : (part > 100 ? 100 : part);
    uint32_t p = (pos + (uint32_t)i * window / stages_) % window;
    stageOn_[i] = part >= 100 || p < (uint32_t)(part / 100.0f * window);
    on_ = on_ || stageOn_[i];
  }
}
