// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "temp_onewire.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <string.h>

#include "board/pins.h"

namespace onewire_temp {
namespace {

constexpr uint8_t kResolutionBits = 12;
constexpr uint32_t kConversionMs = 800;  // 750 ms at 12 bit plus margin

OneWire bus(PIN_ONEWIRE);
DallasTemperature dallas(&bus);

struct Sensor {
  DeviceAddress addr;
  float tempC = kNoReading;
  uint16_t errors = 0;
};
Sensor sensors[kMaxSensors];
int sensorCount = 0;

bool converting = false;
uint32_t conversionStart = 0;

void scan() {
  dallas.begin();
  sensorCount = 0;
  int found = dallas.getDeviceCount();
  for (int i = 0; i < found && sensorCount < kMaxSensors; i++) {
    Sensor& s = sensors[sensorCount];
    if (!dallas.getAddress(s.addr, i)) continue;
    dallas.setResolution(s.addr, kResolutionBits);
    s.tempC = kNoReading;
    s.errors = 0;
    sensorCount++;
  }
  dallas.setWaitForConversion(false);
  converting = false;
}

}  // namespace

void begin() { scan(); }

void rescan() { scan(); }

void loop() {
  uint32_t now = millis();
  if (!converting) {
    if (sensorCount == 0) return;
    dallas.requestTemperatures();
    conversionStart = now;
    converting = true;
    return;
  }
  if (now - conversionStart < kConversionMs) return;
  converting = false;
  for (int i = 0; i < sensorCount; i++) {
    Sensor& s = sensors[i];
    float t = dallas.getTempC(s.addr);  // CRC is checked by the library
    // 85.0 is the power-on value: the conversion did not happen.
    if (t == DEVICE_DISCONNECTED_C || t == 85.0f) {
      if (s.errors < UINT16_MAX) s.errors++;
      if (s.errors >= 3) s.tempC = kNoReading;  // keep last value for brief glitches
    } else {
      s.tempC = t;
      s.errors = 0;
    }
  }
}

int count() { return sensorCount; }

float temperature(int index) {
  return (index >= 0 && index < sensorCount) ? sensors[index].tempC : kNoReading;
}

void address(int index, uint8_t out[8]) {
  if (index >= 0 && index < sensorCount) memcpy(out, sensors[index].addr, 8);
}

uint16_t errorCount(int index) {
  return (index >= 0 && index < sensorCount) ? sensors[index].errors : 0;
}

}  // namespace onewire_temp
