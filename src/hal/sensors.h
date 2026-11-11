// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Temperature sensors by role. Matches the sensors found on the bus with
// the configured ones (by ROM address), applies calibration offsets and
// reports faults. If no kettle sensor is configured, the first unassigned
// sensor found becomes the kettle sensor.
#pragma once
#include <stdint.h>

#include <string>

#include "core/settings.h"

namespace sensors {

struct Info {
  std::string name;
  settings::SensorRole role = settings::SensorRole::None;
  uint8_t rom[8] = {};
  float offsetC = 0;
  int busIndex = -1;  // -1: configured but not found on the bus
  float tempC = 0;
  bool ok = false;
  uint16_t errors = 0;
};

void begin();
void rescan();
// Re-apply settings::current().sensors without rescanning the bus.
void reload();
void loop();

// Store role, offset and name of a listed sensor in the settings (keyed by
// its ROM address; the caller saves the settings). forget() drops the entry.
bool configure(int index, settings::SensorRole role, float offsetC, const std::string& name);
bool forget(int index);

int count();
const Info& info(int index);

// Temperature of the first healthy sensor with this role. Returns false if
// no sensor has the role or all of them are faulty.
bool read(settings::SensorRole role, float& tempC);
bool assigned(settings::SensorRole role);
// An assigned kettle, RIMS or heatsink sensor has no valid reading.
bool criticalFault();

// Replace a role's reading (fake-heating mode).
void setOverride(settings::SensorRole role, float tempC);
void clearOverride();

}  // namespace sensors
