// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "io.h"

#include <Arduino.h>

#include "audio.h"
#include "board/pins.h"
#include "iox.h"

namespace io {
namespace {

const uint8_t kSignalPins[kSignalOutputs] = {PIN_OUT1, PIN_OUT2, PIN_OUT3, PIN_OUT4};
const uint8_t kPowerPins[kPowerOutputs] = {PIN_PWR1, PIN_PWR2};

// LEDC channels; channel 7 belongs to the display backlight.
const uint8_t kPowerChannels[kPowerOutputs] = {0, 1};
constexpr uint32_t kPowerPwmHz = 20000;  // above audible range
constexpr uint8_t kPwmBits = 8;

constexpr uint32_t kDebounceMs = 30;
constexpr uint32_t kLongPressMs = 800;

bool signalState[kSignalOutputs];
uint8_t powerDuty[kPowerOutputs];

// Front buttons and LED rings are on the backpack's I2C expander (polled by
// iox::loop()).
bool buttonRaw(uint8_t i) { return iox::buttonDown(i); }
void ledWrite(uint8_t i, bool on) { iox::setLed(i, on); }

struct ButtonState {
  explicit ButtonState(uint8_t i) : index(i) {}
  uint8_t index;
  bool raw = false;
  bool stable = false;
  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;
  bool longReported = false;
  Press pending = Press::None;
};
ButtonState buttons[2] = {ButtonState(0), ButtonState(1)};

void updateButton(ButtonState& b, uint32_t now) {
  bool raw = buttonRaw(b.index);
  if (raw != b.raw) {
    b.raw = raw;
    b.changedAt = now;
  }
  if (b.raw != b.stable && now - b.changedAt >= kDebounceMs) {
    b.stable = b.raw;
    if (b.stable) {
      b.pressedAt = now;
      b.longReported = false;
    } else if (!b.longReported) {
      b.pending = Press::Short;
    }
  }
  // Report a long press as soon as the threshold passes, while still held.
  if (b.stable && !b.longReported && now - b.pressedAt >= kLongPressMs) {
    b.longReported = true;
    b.pending = Press::Long;
  }
}

}  // namespace

void begin() {
  for (int i = 0; i < kSignalOutputs; i++) {
    digitalWrite(kSignalPins[i], LOW);
    pinMode(kSignalPins[i], OUTPUT);
    signalState[i] = false;
  }
  for (int i = 0; i < kPowerOutputs; i++) {
    digitalWrite(kPowerPins[i], LOW);
    pinMode(kPowerPins[i], OUTPUT);
    ledcSetup(kPowerChannels[i], kPowerPwmHz, kPwmBits);
    ledcAttachPin(kPowerPins[i], kPowerChannels[i]);
    ledcWrite(kPowerChannels[i], 0);
    powerDuty[i] = 0;
  }

  iox::begin();  // also starts the shared I2C bus
  pinMode(PIN_LEVEL, INPUT_PULLUP);
}

void loop() {
  iox::loop();
  uint32_t now = millis();
  for (auto& b : buttons) updateButton(b, now);
}

void setSignal(int index, bool on) {
  if (index < 0 || index >= kSignalOutputs) return;
  signalState[index] = on;
  digitalWrite(kSignalPins[index], on ? HIGH : LOW);
}

bool signal(int index) {
  return index >= 0 && index < kSignalOutputs && signalState[index];
}

void setPower(int index, uint8_t duty) {
  if (index < 0 || index >= kPowerOutputs) return;
  if (duty > 100) duty = 100;
  powerDuty[index] = duty;
  ledcWrite(kPowerChannels[index], (uint32_t)duty * 255 / 100);
}

uint8_t power(int index) {
  return (index >= 0 && index < kPowerOutputs) ? powerDuty[index] : 0;
}

void allOff() {
  for (int i = 0; i < kSignalOutputs; i++) setSignal(i, false);
  for (int i = 0; i < kPowerOutputs; i++) setPower(i, 0);
}

Press takePress(Button b) {
  auto& s = buttons[(int)b];
  Press p = s.pending;
  s.pending = Press::None;
  return p;
}

bool isHeld(Button b) { return buttons[(int)b].stable; }

void setLed(Button b, bool on) { ledWrite((uint8_t)b, on); }

void beep(uint8_t count, uint16_t onMs, uint16_t freqHz) { audio::tone(freqHz, onMs, count); }

bool levelSensorClosed() { return digitalRead(PIN_LEVEL) == LOW; }

}  // namespace io
