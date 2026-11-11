// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sim.h"

namespace {
constexpr float kWaterJPerKgK = 4186.0f;
constexpr float kBoilC = 100.0f;
}  // namespace

void KettleSim::reset(const settings::Sim& s) {
  cfg_ = s;
  water_ = sensor_ = s.startC;
}

void KettleSim::step(uint32_t dtMs, float heaterFraction) {
  float dt = dtMs / 1000.0f;
  float heatCapacity = (cfg_.liters > 0.1f ? cfg_.liters : 0.1f) * kWaterJPerKgK;
  float power = cfg_.heaterW * heaterFraction - cfg_.lossWPerK * (water_ - cfg_.ambientC);
  water_ += power * dt / heatCapacity;
  if (water_ > kBoilC) water_ = kBoilC;  // energy goes into steam

  // Sensor lag (probe in a thermowell)
  float tau = cfg_.sensorTauS > 0.1f ? cfg_.sensorTauS : 0.1f;
  float k = dt / tau;
  if (k > 1) k = 1;
  sensor_ += (water_ - sensor_) * k;
}
