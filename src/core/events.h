// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Events raised by the brewing core (alarms, prompts, step changes). The
// application layer turns them into beeps, screen notices and web pushes.
#pragma once
#include <stdint.h>

namespace events {

enum class Type : uint8_t {
  TempReached,
  StepStarted,   // arg = step index (mash), or phase
  StepDone,
  PromptOpened,  // arg = prompt id
  Addition,      // arg = addition index
  TimerDone,     // manual countdown finished
  BrewFinished,
  BoilSoon,      // close to the boil: watch for a boil-over
  BoilStalled,   // heating in the boil phase but the temperature stopped rising
  SensorFault,
  SafetyCut,     // over-temperature, heatsink, level
  AutotuneDone,
  AutotuneFailed,
  PumpRest,      // arg = 1 rest started, 0 ended
};

struct Event {
  Type type;
  int16_t arg;
};

void post(Type t, int16_t arg = 0);
// Returns false when the queue is empty.
bool take(Event& out);

const char* name(Type t);

}  // namespace events
