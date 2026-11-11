// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Board I/O: signal outputs, PWM power outputs, front buttons with LED
// rings and level sensor. Sound is in hal/audio. Roles (heater, pump, fan...) are mapped
// onto these physical channels by higher layers.
#pragma once
#include <stdint.h>

namespace io {

constexpr int kSignalOutputs = 4;  // OUT1..OUT4
constexpr int kPowerOutputs = 2;   // PWR1..PWR2

enum class Button : uint8_t { Start, Pump };
enum class Press : uint8_t { None, Short, Long };

// All outputs off, pins configured. Call first thing in setup().
void begin();

// Debounce buttons. Call every loop.
void loop();

void setSignal(int index, bool on);
bool signal(int index);

// duty 0..100 %
void setPower(int index, uint8_t duty);
uint8_t power(int index);

void allOff();

// Returns and clears the last completed press of a button.
Press takePress(Button b);
bool isHeld(Button b);

void setLed(Button b, bool on);

// Plain beeps through the speaker: count x (onMs on, onMs off) at freqHz.
// Events use the chimes in hal/audio instead.
void beep(uint8_t count = 1, uint16_t onMs = 120, uint16_t freqHz = 2700);

bool levelSensorClosed();

}  // namespace io
