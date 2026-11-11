// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DS18B20 sensors on the 1-Wire bus, read without blocking. Any number of
// sensors is supported; each is identified by its 64-bit ROM address.
#pragma once
#include <stdint.h>

namespace onewire_temp {

constexpr int kMaxSensors = 8;
constexpr float kNoReading = -127.0f;

void begin();
void loop();

int count();
// Temperature in degrees C, or kNoReading if the sensor failed.
float temperature(int index);
void address(int index, uint8_t out[8]);
// Consecutive failed reads; reset on success.
uint16_t errorCount(int index);

// Rescan the bus (e.g. after plugging in a sensor).
void rescan();

}  // namespace onewire_temp
