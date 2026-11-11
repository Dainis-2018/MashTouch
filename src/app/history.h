// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rolling temperature history in RAM for the charts: one sample every 10 s
// of controller time, 6 hours deep. It is kept in all modes (also idle), so
// a web page that is opened or reloaded gets the curve so far. The brew
// log on flash (storage/brewlog) is separate and survives a restart.
#pragma once
#include <stdint.h>

namespace history {

constexpr int kCount = 2160;
constexpr uint32_t kStepS = 10;
constexpr int16_t kNone = INT16_MIN;

struct Sample {
  uint32_t t;      // controller clock, seconds (runs faster in fake heating)
  int16_t temp10;  // tenths of a degree C, kNone = no reading
  int16_t sp10;    // setpoint, kNone = none (idle)
  uint8_t duty;    // heater power %
  uint8_t pump;    // 1 = on
};

// Call every loop; stores a sample when the next 10 s step is due.
void update(uint32_t clockS, bool tempOk, float tempC, bool hasSetpoint, float setpointC, uint8_t duty,
            bool pump);

int count();
const Sample& at(int i);  // 0 = oldest

}  // namespace history
