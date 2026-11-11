// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Touch UI: a top bar (home, title, status) and one screen below it.
// The brew screen opens by itself when a mode starts, and home returns when
// it ends. All actions go through the controller action layer.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ui {

enum class Screen : uint8_t { Home, Brew, Recipes, RecipeEdit, Settings, Sensors, Tools, Bringup, Wifi };

void begin();  // after display::begin(), storage and controller
// Switch screens after the current event (safe from button callbacks).
void go(Screen s);
Screen current();
void setTitle(const char* text);

// ---- Units: internal values are C, the display may be F ------------------
bool fahrenheit();
const char* unit();  // "°C" or "°F"
float toDisplay(float c);
float fromDisplay(float d);
float deltaToDisplay(float c);    // differences (bands, hysteresis)
float deltaFromDisplay(float d);
void fmtTemp(char* buf, size_t n, float c, int decimals = 1);
void fmtTime(char* buf, size_t n, uint32_t ms);

// ---- Temperature history for charts (one sample every 5 s) --------------
constexpr int kHistory = 240;  // 20 minutes
// i = kHistory - 1 is the newest sample; tenths of a display degree,
// INT16_MIN where there is no data.
int16_t historyTemp(int i);
int16_t historySetpoint(int i);  // INT16_MIN when no setpoint
uint32_t historySamples();       // total taken; changes when a sample is added

}  // namespace ui
