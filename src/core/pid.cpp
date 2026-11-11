// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pid.h"

namespace {

constexpr float kPi = 3.14159265f;

float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

void Pid::setTunings(float kp, float ki, float kd) {
  kp_ = kp;
  ki_ = ki;
  kd_ = kd;
}

float Pid::update(float setpoint, float input, uint32_t now) {
  if (!started_) {
    started_ = true;
    lastTime_ = now - sampleMs_;  // compute immediately
    lastInput_ = input;
    integral_ = 0;
  }
  if (now - lastTime_ < sampleMs_) return output_;

  float dt = (now - lastTime_) / 1000.0f;
  lastTime_ = now;

  float error = setpoint - input;
  float p = kp_ * error;
  // Derivative on measurement: no kick when the setpoint changes.
  float d = dt > 0 ? -kd_ * (input - lastInput_) / dt : 0;
  lastInput_ = input;

  // Integrate, then clamp so P + I + D stays inside 0..100 (anti-windup).
  integral_ += ki_ * error * dt;
  integral_ = clamp(integral_, 0.0f - p - d, 100.0f - p - d);
  integral_ = clamp(integral_, 0.0f, 100.0f);

  output_ = clamp(p + integral_ + d, 0.0f, 100.0f);
  return output_;
}

void Autotune::start(float setpoint, float highPct, float lowPct, float hysteresisC,
                     uint8_t cycles, uint32_t timeoutMs, uint32_t now) {
  state_ = State::Running;
  setpoint_ = setpoint;
  high_ = highPct;
  low_ = lowPct;
  hyst_ = hysteresisC;
  cycles_ = cycles ? cycles : 1;
  timeoutMs_ = timeoutMs;
  startTime_ = now;
  outputHigh_ = true;
  started_ = false;
  periods_ = 0;
  sumPeriod_ = sumAmplitude_ = 0;
  kp_ = ki_ = kd_ = ku_ = pu_ = 0;
}

float Autotune::update(float input, uint32_t now) {
  if (state_ != State::Running) return 0;
  if (now - startTime_ > timeoutMs_) {
    state_ = State::Failed;
    return 0;
  }

  if (started_) {
    if (input > peakMax_) peakMax_ = input;
    if (input < peakMin_) peakMin_ = input;
  }

  if (outputHigh_ && input > setpoint_ + hyst_) {
    outputHigh_ = false;
  } else if (!outputHigh_ && input < setpoint_ - hyst_) {
    outputHigh_ = true;
    if (!started_) {
      // The first rise only marks the start; the warm-up is not measured.
      started_ = true;
    } else {
      sumPeriod_ += (now - lastRise_) / 1000.0f;
      sumAmplitude_ += (peakMax_ - peakMin_) / 2.0f;
      periods_++;
    }
    lastRise_ = now;
    peakMax_ = peakMin_ = input;

    if (periods_ >= cycles_) {
      float amplitude = sumAmplitude_ / periods_;
      pu_ = sumPeriod_ / periods_;
      if (amplitude <= 0.01f || pu_ <= 0) {
        state_ = State::Failed;
        return 0;
      }
      float d = (high_ - low_) / 2.0f;
      ku_ = 4.0f * d / (kPi * amplitude);
      // Tyreus-Luyben PID
      kp_ = ku_ / 2.2f;
      float ti = 2.2f * pu_;
      float td = pu_ / 6.3f;
      ki_ = kp_ / ti;
      kd_ = kp_ * td;
      state_ = State::Done;
      return 0;
    }
  }
  return outputHigh_ ? high_ : low_;
}
