// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "history.h"

#include <math.h>

namespace history {
namespace {

Sample ring[kCount];
int head = 0;   // next write position
int stored = 0;
uint32_t nextAt = 0;
bool started = false;

}  // namespace

void update(uint32_t clockS, bool tempOk, float tempC, bool hasSetpoint, float setpointC, uint8_t duty,
            bool pump) {
  if (started && clockS < nextAt) return;
  started = true;
  nextAt = clockS + kStepS;
  Sample& s = ring[head];
  s.t = clockS;
  s.temp10 = tempOk ? (int16_t)lroundf(tempC * 10) : kNone;
  s.sp10 = hasSetpoint ? (int16_t)lroundf(setpointC * 10) : kNone;
  s.duty = duty;
  s.pump = pump ? 1 : 0;
  head = (head + 1) % kCount;
  if (stored < kCount) stored++;
}

int count() { return stored; }

const Sample& at(int i) {
  int first = stored < kCount ? 0 : head;
  return ring[(first + i) % kCount];
}

}  // namespace history
