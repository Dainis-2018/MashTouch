// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "events.h"

namespace events {
namespace {

constexpr int kQueue = 16;
Event queue[kQueue];
uint8_t head = 0, tail = 0;  // single-threaded: posted and taken from loop()

}  // namespace

void post(Type t, int16_t arg) {
  uint8_t next = (head + 1) % kQueue;
  if (next == tail) tail = (tail + 1) % kQueue;  // full: drop the oldest
  queue[head] = {t, arg};
  head = next;
}

bool take(Event& out) {
  if (tail == head) return false;
  out = queue[tail];
  tail = (tail + 1) % kQueue;
  return true;
}

const char* name(Type t) {
  switch (t) {
    case Type::TempReached: return "temp-reached";
    case Type::StepStarted: return "step-started";
    case Type::StepDone: return "step-done";
    case Type::PromptOpened: return "prompt";
    case Type::Addition: return "addition";
    case Type::TimerDone: return "timer-done";
    case Type::BrewFinished: return "brew-finished";
    case Type::BoilSoon: return "boil-soon";
    case Type::BoilStalled: return "boil-stalled";
    case Type::SensorFault: return "sensor-fault";
    case Type::SafetyCut: return "safety-cut";
    case Type::AutotuneDone: return "autotune-done";
    case Type::AutotuneFailed: return "autotune-failed";
    case Type::PumpRest: return "pump-rest";
  }
  return "?";
}

}  // namespace events
