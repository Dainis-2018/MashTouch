// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "iox.h"

#include <Arduino.h>

#include "board/pins.h"
#include "i2cbus.h"

namespace iox {

#if MT_LCD_RST_EXIO || MT_HAS_IOX_FRONT
namespace {

// TCA9554 registers: 0 input, 1 output, 2 polarity, 3 config (1 = input)
constexpr uint8_t kExioOut = 0x01;
constexpr uint8_t kExioCfg = 0x03;

// TCA9535 registers (port 0 only): 0 input, 2 output, 6 config (1 = input)
constexpr uint8_t kFrontIn = 0x00;
constexpr uint8_t kFrontOut = 0x02;
constexpr uint8_t kFrontCfg = 0x06;

constexpr uint32_t kPollMs = 10;
constexpr uint32_t kRetryMs = 3000;

bool frontOk = false;
uint8_t frontInput = 0xFF;  // last read; buttons are active low
uint8_t ledBits = 0;
uint8_t ledWritten = 0xFF;  // force the first write
uint32_t lastPoll = 0;
uint32_t lastTry = 0;
bool warned = false;

#if MT_HAS_IOX_FRONT
constexpr uint8_t kLedMask = (1u << FRONT_BIT_LED_START) | (1u << FRONT_BIT_LED_PUMP);
const uint8_t kButtonBit[2] = {FRONT_BIT_BTN_START, FRONT_BIT_BTN_PUMP};
const uint8_t kLedBit[2] = {FRONT_BIT_LED_START, FRONT_BIT_LED_PUMP};

bool setupFront() {
  // Output latch first (LEDs off), then switch only the LED pins to outputs.
  if (!i2cbus::writeReg(I2C_ADDR_FRONT, kFrontOut, 0x00)) return false;
  if (!i2cbus::writeReg(I2C_ADDR_FRONT, kFrontCfg, (uint8_t)~kLedMask)) return false;
  ledWritten = 0xFF;
  return true;
}
#endif

}  // namespace
#endif

void begin() {
#if MT_LCD_RST_EXIO || MT_HAS_IOX_FRONT
  i2cbus::begin();
#endif
#if MT_HAS_IOX_FRONT
  lastTry = millis();
  frontOk = setupFront();
  if (!frontOk) {
    Serial.printf("front panel expander 0x%02X not found: buttons and LEDs disabled\n", I2C_ADDR_FRONT);
    warned = true;
  }
#endif
}

void loop() {
#if MT_HAS_IOX_FRONT
  uint32_t now = millis();
  if (!frontOk) {
    if (now - lastTry < kRetryMs) return;
    lastTry = now;
    frontOk = setupFront();
    if (frontOk) Serial.println("front panel expander found");
    return;
  }
  if (now - lastPoll < kPollMs) return;
  lastPoll = now;

  uint8_t in;
  if (!i2cbus::readReg(I2C_ADDR_FRONT, kFrontIn, &in)) {
    // Lost (unplugged or bus error): release the buttons, try again later.
    frontOk = false;
    frontInput = 0xFF;
    lastTry = now;
    if (!warned) Serial.println("front panel expander lost");
    warned = true;
    return;
  }
  warned = false;
  frontInput = in;
  if (ledBits != ledWritten && i2cbus::writeReg(I2C_ADDR_FRONT, kFrontOut, ledBits)) ledWritten = ledBits;
#endif
}

void resetLcd() {
#if MT_LCD_RST_EXIO
  const uint8_t bit = 1u << EXIO_BIT_LCD_RST;
  // Read-modify-write so the other expander pins keep their power-on state.
  uint8_t out = 0xFF, cfg = 0xFF;
  i2cbus::readReg(I2C_ADDR_EXIO, kExioOut, &out);
  i2cbus::readReg(I2C_ADDR_EXIO, kExioCfg, &cfg);
  i2cbus::writeReg(I2C_ADDR_EXIO, kExioOut, out | bit);
  i2cbus::writeReg(I2C_ADDR_EXIO, kExioCfg, cfg & ~bit);
  i2cbus::writeReg(I2C_ADDR_EXIO, kExioOut, (out | bit) & ~bit);
  delay(10);
  i2cbus::writeReg(I2C_ADDR_EXIO, kExioOut, out | bit);
  delay(120);  // ST7796: wait before the first command after reset
#endif
}

bool frontPresent() {
#if MT_HAS_IOX_FRONT
  return frontOk;
#else
  return false;
#endif
}

bool buttonDown(uint8_t index) {
#if MT_HAS_IOX_FRONT
  if (index > 1 || !frontOk) return false;
  return (frontInput & (1u << kButtonBit[index])) == 0;
#else
  (void)index;
  return false;
#endif
}

void setLed(uint8_t index, bool on) {
#if MT_HAS_IOX_FRONT
  if (index > 1) return;
  if (on) ledBits |= 1u << kLedBit[index];
  else ledBits &= ~(1u << kLedBit[index]);
#else
  (void)index;
  (void)on;
#endif
}

}  // namespace iox
