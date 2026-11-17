// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PT100 / PT1000 probes through MAX31865 converters on the backpack (J8 SPI
// lines, the panel's unused microSD bus; one chip select per channel). The
// chips run in automatic conversion mode, so reading never waits: loop()
// fetches the latest result a few times per second. A channel whose chip
// does not answer is reported as missing and retried every few seconds.
#pragma once
#include <stdint.h>

namespace rtd_temp {

constexpr int kChannels = 2;
constexpr float kNoReading = -127.0f;

struct Config {
  bool enabled = false;
  float r0 = 100;       // nominal resistance at 0 C: 100 (PT100) or 1000 (PT1000)
  float refOhm = 430;   // reference resistor on the board
  uint8_t wires = 3;    // 2, 3 or 4
  bool filter50Hz = true;  // mains filter: 50 Hz, otherwise 60 Hz
};

// Fault bits as in the MAX31865 fault status register, plus our own
// open/short detection in the low bits that the chip leaves unused.
constexpr uint8_t kFaultHighThreshold = 0x80;
constexpr uint8_t kFaultLowThreshold = 0x40;
constexpr uint8_t kFaultRefInHigh = 0x20;
constexpr uint8_t kFaultRefInLow = 0x10;
constexpr uint8_t kFaultRtdInLow = 0x08;
constexpr uint8_t kFaultVoltage = 0x04;
constexpr uint8_t kFaultOpen = 0x02;   // reading at full scale
constexpr uint8_t kFaultShort = 0x01;  // reading near zero

// Applies a channel's settings; the chip is only rewritten when they change.
void configure(int ch, const Config& cfg);
// Re-detect the chips and reapply their settings (after plugging something in).
void recheck();
void loop();

bool enabled(int ch);
// The chip answered at the last check.
bool present(int ch);
// Degrees C, or kNoReading after repeated faults.
float temperature(int ch);
float resistance(int ch);
uint16_t raw(int ch);       // 15-bit ADC ratio, 32768 = refOhm
uint8_t faultBits(int ch);  // of the last read, 0 = none
uint16_t errorCount(int ch);  // consecutive failed reads; reset on success
// Short text for the first fault bit set ("" if none).
const char* faultText(uint8_t bits);

// Callendar-Van Dusen (IEC 60751): resistance to temperature for a platinum
// sensor with nominal resistance r0. Pure function, valid -200..850 C.
float ohmsToCelsius(float ohms, float r0);

}  // namespace rtd_temp
