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

}  // namespace i2cbus
