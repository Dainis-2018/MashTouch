// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Output roles: maps heater, pump, fan... demands onto the physical
// channels (OUT1..OUT4 signal, PWR1..PWR2 PWM) according to settings.
// Every channel with a role gets that role's state; several channels may
// share a role (heater 1..n, pump 1..n).
#pragma once
#include <stdint.h>

#include "core/settings.h"

namespace outputs {

struct Demand {
  bool heater[settings::kMaxHeaters] = {};  // heater 1..n (stages)
  bool pump = false;
  uint8_t fanPct = 0;
  bool sparge = false;     // sparge water heater
  bool alarm = false;      // alarm light or horn
  bool contactor = false;  // safety relay in the heater mains feed
  // Heater, sparge heater, pump and contactor channels forced off, also
  // over bench-test values.
  bool safetyCut = false;
};

void begin();
void apply(const Demand& d);

// Number of kettle heater stages configured (highest heater number, at
// least 1), and whether any output has this role.
int heaterCount();
bool hasRole(settings::OutputRole role);

// Bench test from the bring-up screen: value 0..100 per channel. Honoured
// only while allowed (the controller is idle); disallowing clears them.
void setTestAllowed(bool allowed);
void setTest(int channel, uint8_t value);

// Current value of a channel, 0..100 (signal outputs: 0 or 100).
uint8_t value(int channel);

}  // namespace outputs
