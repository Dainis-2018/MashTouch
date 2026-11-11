// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// I2C port expanders on the Waveshare board:
//  - the on-board TCA9554 (LCD reset, touch interrupt, SD detect)
//  - the backpack TCA9535 (front buttons and LED rings)
// Nothing safety-related goes through an expander: heater and pump outputs and
// the level switch are direct GPIOs.
#pragma once
#include <stdint.h>

namespace iox {

// Start the bus and set up both expanders. Safe without a backpack.
void begin();

// Call from loop(): polls the front buttons (about every 10 ms), writes LEDs
// when they change, and retries a missing backpack every few seconds.
void loop();

// Pulse the LCD reset line and wait for the panel (setup only, blocks ~130 ms).
void resetLcd();

// Front panel. Buttons read "not pressed" while the backpack is missing.
bool frontPresent();
bool buttonDown(uint8_t index);  // 0 = Start/Pause, 1 = Pump
void setLed(uint8_t index, bool on);

}  // namespace iox
