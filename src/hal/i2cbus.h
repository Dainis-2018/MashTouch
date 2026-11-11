// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shared I2C bus on boards that have one (Waveshare: touch, expanders,
// codec, PMU, RTC, IMU). It uses LovyanGFX's I2C driver on port 0, the same
// one its touch driver uses, so only one driver ever touches the peripheral.
// Do not use Arduino Wire on this bus.
//
// Calls are short register transfers (well under 1 ms at 400 kHz) and are
// made from loop() or setup() only.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace i2cbus {

// True if this board has the shared bus. begin() is safe to call twice.
bool available();
bool begin();

bool probe(uint8_t addr);
bool writeReg(uint8_t addr, uint8_t reg, uint8_t value);
bool readReg(uint8_t addr, uint8_t reg, uint8_t* value);
bool readRegs(uint8_t addr, uint8_t reg, uint8_t* data, size_t len);

}  // namespace i2cbus
