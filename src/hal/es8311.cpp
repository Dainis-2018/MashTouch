// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "es8311.h"

#include <Arduino.h>

#include "i2cbus.h"

namespace es8311 {
namespace {

// The address depends on the CE pin: 0x18 (low) or 0x19 (high).
const uint8_t kAddrs[2] = {0x18, 0x19};
uint8_t addr = 0;

// Register numbers (datasheet names in comments)
constexpr uint8_t kReset = 0x00;      // RESET: CSM_ON, MSC (master), resets
constexpr uint8_t kClk1 = 0x01;       // CLK_MANAGER: MCLK source, clock enables
constexpr uint8_t kClk2 = 0x02;       // pre-divider / pre-multiplier
constexpr uint8_t kClk3 = 0x03;       // ADC fs mode, ADC OSR
constexpr uint8_t kClk4 = 0x04;       // DAC OSR
constexpr uint8_t kClk5 = 0x05;       // ADC / DAC clock dividers
constexpr uint8_t kClk6 = 0x06;       // BCLK divider (master only)
constexpr uint8_t kClk7 = 0x07;       // LRCK divider high (master only)
constexpr uint8_t kClk8 = 0x08;       // LRCK divider low (master only)
constexpr uint8_t kSdpIn = 0x09;      // DAC serial port: channel, mute, word length, format
constexpr uint8_t kSdpOut = 0x0A;     // ADC serial port
constexpr uint8_t kSys0B = 0x0B;
constexpr uint8_t kSys0C = 0x0C;
constexpr uint8_t kSysPower = 0x0D;   // analog power up
constexpr uint8_t kSys0E = 0x0E;      // PGA / ADC modulator power
constexpr uint8_t kDacPower = 0x12;   // DAC power (0 = on)
constexpr uint8_t kOutDrive = 0x13;   // output drive enable
constexpr uint8_t kDacMute = 0x31;    // DAC mute bits 6:5
constexpr uint8_t kDacVol = 0x32;     // DAC volume, 0xBF = 0 dB, 0.5 dB steps
constexpr uint8_t kDacEq = 0x37;      // DAC ramp / EQ bypass

bool w(uint8_t reg, uint8_t v) { return i2cbus::writeReg(addr, reg, v); }

}  // namespace

bool begin() {
  i2cbus::begin();
  addr = 0;
  for (uint8_t a : kAddrs) {
    if (i2cbus::probe(a)) {
      addr = a;
      break;
    }
  }
  if (!addr) return false;

  bool ok = w(kReset, 0x1F);  // reset all blocks
  delay(5);
  ok &= w(kReset, 0x00);
  ok &= w(kReset, 0x80);  // state machine on, slave mode

  ok &= w(kClk1, 0x3F);  // MCLK from the MCLK pin, all clocks on
  ok &= w(kClk2, 0x00);  // MCLK = 256 fs: no pre-divide, no multiply
  ok &= w(kClk3, 0x10);  // single speed, ADC OSR
  ok &= w(kClk4, 0x10);  // DAC OSR
  ok &= w(kClk5, 0x00);  // ADC and DAC clock divide by 1
  ok &= w(kClk6, 0x03);
  ok &= w(kClk7, 0x00);
  ok &= w(kClk8, 0xFF);

  ok &= w(kSdpIn, 0x0C);   // left channel, unmuted, 16-bit, I2S
  ok &= w(kSdpOut, 0x0C);  // same for the ADC side (unused)
  ok &= w(kSys0B, 0x00);
  ok &= w(kSys0C, 0x00);
  ok &= w(kSysPower, 0x01);  // analog power up
  ok &= w(kSys0E, 0x02);
  ok &= w(kDacPower, 0x00);  // DAC on
  ok &= w(kOutDrive, 0x10);  // output drive on (to the NS4150B amplifier)
  ok &= w(kDacEq, 0x08);     // bypass the DAC equaliser
  ok &= w(kDacMute, 0x00);
  ok &= w(kDacVol, 0xBF);    // 0 dB; loudness comes from the synthesiser
  return ok;
}

void setVolume(uint8_t pct) {
  if (!addr) return;
  if (pct > 100) pct = 100;
  w(kDacVol, (uint8_t)((uint16_t)pct * 0xBF / 100));
}

uint8_t address() { return addr; }

}  // namespace es8311
