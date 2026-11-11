// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step timer driven by elapsed-time ticks, so pausing is simply not ticking.
#pragma once
#include <stdint.h>

class StepTimer {
 public:
  void reset(uint32_t durationMs) {
    duration_ = durationMs;
    elapsed_ = 0;
  }
  void tick(uint32_t dtMs) { elapsed_ += dtMs; }
  void setElapsed(uint32_t ms) { elapsed_ = ms; }
  void setDuration(uint32_t ms) { duration_ = ms; }

  uint32_t durationMs() const { return duration_; }
  uint32_t elapsedMs() const { return elapsed_; }
  uint32_t remainingMs() const { return elapsed_ >= duration_ ? 0 : duration_ - elapsed_; }
  bool done() const { return elapsed_ >= duration_; }

 private:
  uint32_t duration_ = 0;
  uint32_t elapsed_ = 0;
};
