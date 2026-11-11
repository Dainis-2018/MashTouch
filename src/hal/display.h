// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ST7796 display + FT6336 touch (MSP3526 or Waveshare 3.5), driven through LovyanGFX,
// with LVGL on top. Landscape, 480x320.
#pragma once
#include <stdint.h>

namespace display {

constexpr int WIDTH = 480;
constexpr int HEIGHT = 320;

// Initialise panel, touch, backlight and LVGL. Returns false if the panel
// did not respond.
bool begin();

// Run LVGL timers; call from loop() as often as possible.
void loop();

// Backlight 0..255.
void setBrightness(uint8_t level);

}  // namespace display
