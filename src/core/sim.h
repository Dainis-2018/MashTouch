// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fake-heating mode: a simple kettle model fed by the heater output, used
// in place of the control sensor to test the process without hardware.
#pragma once
#include <stdint.h>

#include "settings.h"

class KettleSim {
 public:
  void reset(const settings::Sim& s);
  void configure(const settings::Sim& s) { cfg_ = s; }
  // Advance by dtMs with the heater at heaterFraction (0..1).
  void step(uint32_t dtMs, float heaterFraction);

  float waterC() const { return water_; }
  float sensorC() const { return sensor_; }
  void setWaterC(float c) { water_ = sensor_ = c; }

 private:
  settings::Sim cfg_;
  float water_ = 15;
  float sensor_ = 15;
};
