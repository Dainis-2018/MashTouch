// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "i2cbus.h"

#include <LovyanGFX.hpp>

#include "board/pins.h"

namespace i2cbus {
namespace {

constexpr int kPort = 0;
constexpr uint32_t kFreq = 400000;
bool started = false;

}  // namespace

#ifdef PIN_I2C_SDA

bool available() { return true; }

bool begin() {
  if (started) return true;
  started = lgfx::i2c::init(kPort, PIN_I2C_SDA, PIN_I2C_SCL).has_value();
  return started;
}

bool probe(uint8_t addr) {
  if (!started) return false;
  // A zero-length write: the device ACKs its address or not.
  return lgfx::i2c::transactionWrite(kPort, addr, nullptr, 0, kFreq).has_value();
}

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  if (!started) return false;
  const uint8_t buf[2] = {reg, value};
  return lgfx::i2c::transactionWrite(kPort, addr, buf, 2, kFreq).has_value();
}

bool readRegs(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
  if (!started) return false;
  return lgfx::i2c::transactionWriteRead(kPort, addr, &reg, 1, data, len, kFreq).has_value();
}

bool readReg(uint8_t addr, uint8_t reg, uint8_t* value) { return readRegs(addr, reg, value, 1); }

#else  // board without a shared bus (DevKit: the touch controller has the bus to itself)

bool available() { return false; }
bool begin() { return false; }
bool probe(uint8_t) { return false; }
bool writeReg(uint8_t, uint8_t, uint8_t) { return false; }
bool readReg(uint8_t, uint8_t, uint8_t*) { return false; }
bool readRegs(uint8_t, uint8_t, uint8_t*, size_t) { return false; }

#endif

}  // namespace i2cbus
