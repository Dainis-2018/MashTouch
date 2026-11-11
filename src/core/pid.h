// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PID loop with output in % (0..100) and a relay-feedback autotuner.
#pragma once
#include <stdint.h>

class Pid {
 public:
  void setTunings(float kp, float ki, float kd);
  void setSampleMs(uint32_t ms) { sampleMs_ = ms ? ms : 1; }

  // Forget the history; the next update starts fresh from this input.
  void reset() { started_ = false; }

  // Recomputes once per sample period and returns the current output.
  float update(float setpoint, float input, uint32_t now);
  float output() const { return output_; }

 private:
  float kp_ = 0, ki_ = 0, kd_ = 0;
  uint32_t sampleMs_ = 1000;
  bool started_ = false;
  uint32_t lastTime_ = 0;
  float lastInput_ = 0;
  float integral_ = 0;
  float output_ = 0;
};

// Relay autotune (Astrom-Hagglund): switch the heater between two outputs
// around the setpoint, measure the oscillation, derive gains with the
// Tyreus-Luyben rule (little overshoot, suits slow thermal loads).
class Autotune {
 public:
  enum class State : uint8_t { Idle, Running, Done, Failed };

  void start(float setpoint, float highPct, float lowPct, float hysteresisC, uint8_t cycles,
             uint32_t timeoutMs, uint32_t now);
  void cancel() { state_ = State::Idle; }
  // Returns the output in %.
  float update(float input, uint32_t now);

  State state() const { return state_; }
  float setpoint() const { return setpoint_; }
  uint8_t cyclesDone() const { return periods_; }
  uint8_t cyclesWanted() const { return cycles_; }
  float kp() const { return kp_; }
  float ki() const { return ki_; }
  float kd() const { return kd_; }
  float ku() const { return ku_; }
  float puSec() const { return pu_; }

 private:
  State state_ = State::Idle;
  float setpoint_ = 0, high_ = 100, low_ = 0, hyst_ = 0.2f;
  uint8_t cycles_ = 4;
  uint32_t startTime_ = 0, timeoutMs_ = 0;
  bool outputHigh_ = true;
  bool started_ = false;       // first crossing seen; measuring from here on
  uint32_t lastRise_ = 0;      // time of the last low->high switch
  float peakMax_ = 0, peakMin_ = 0;
  float sumPeriod_ = 0, sumAmplitude_ = 0;
  uint8_t periods_ = 0;
  float kp_ = 0, ki_ = 0, kd_ = 0, ku_ = 0, pu_ = 0;
};
