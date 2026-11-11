// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sensors.h"

#include <string.h>

#include <vector>

#include "temp_onewire.h"

namespace sensors {
namespace {

using settings::SensorRole;

// Readings outside this range are treated as faults.
constexpr float kMinValidC = -30.0f;
constexpr float kMaxValidC = 150.0f;

std::vector<Info> list;
bool overrideOn = false;
SensorRole overrideRole = SensorRole::None;
float overrideC = 0;

bool isCritical(SensorRole r) {
  return r == SensorRole::Kettle || r == SensorRole::Rims || r == SensorRole::Heatsink;
}

void build() {
  std::vector<Info> old;
  old.swap(list);
  const auto& cfg = settings::current().sensors;
  std::vector<bool> used(cfg.size(), false);

  for (int i = 0; i < onewire_temp::count(); i++) {
    Info s;
    onewire_temp::address(i, s.rom);
    s.busIndex = i;
    for (size_t c = 0; c < cfg.size(); c++) {
      if (memcmp(cfg[c].rom, s.rom, 8) == 0) {
        s.role = cfg[c].role;
        s.offsetC = cfg[c].offsetC;
        s.name = cfg[c].name;
        used[c] = true;
        break;
      }
    }
    if (s.name.empty()) {
      char buf[24];
      snprintf(buf, sizeof(buf), "DS18B20 %02X%02X", s.rom[6], s.rom[7]);
      s.name = buf;
    }
    list.push_back(s);
  }
  // Configured sensors that are missing: listed, and faulty if critical.
  for (size_t c = 0; c < cfg.size(); c++) {
    if (used[c]) continue;
    Info s;
    memcpy(s.rom, cfg[c].rom, 8);
    s.role = cfg[c].role;
    s.offsetC = cfg[c].offsetC;
    s.name = cfg[c].name;
    list.push_back(s);
  }
  if (!assigned(SensorRole::Kettle)) {
    for (auto& s : list) {
      if (s.busIndex >= 0 && s.role == SensorRole::None) {
        s.role = SensorRole::Kettle;
        break;
      }
    }
  }
  // Keep the last readings, so a role change does not look like a fault.
  for (auto& s : list) {
    if (s.busIndex < 0) continue;
    for (const auto& o : old) {
      if (o.busIndex >= 0 && memcmp(o.rom, s.rom, 8) == 0) {
        s.ok = o.ok;
        s.errors = o.errors;
        s.tempC = o.ok ? o.tempC - o.offsetC + s.offsetC : o.tempC;
        break;
      }
    }
  }
}

}  // namespace

void begin() {
  onewire_temp::begin();
  build();
}

void rescan() {
  onewire_temp::rescan();
  build();
}

void reload() { build(); }

bool configure(int index, SensorRole role, float offsetC, const std::string& name) {
  if (index < 0 || index >= (int)list.size()) return false;
  auto& cfg = settings::current().sensors;
  const Info& s = list[index];
  settings::SensorConfig* entry = nullptr;
  for (auto& c : cfg)
    if (memcmp(c.rom, s.rom, 8) == 0) entry = &c;
  if (!entry) {
    cfg.push_back(settings::SensorConfig());
    entry = &cfg.back();
    memcpy(entry->rom, s.rom, 8);
  }
  entry->role = role;
  entry->offsetC = offsetC;
  entry->name = name;
  build();
  return true;
}

bool forget(int index) {
  if (index < 0 || index >= (int)list.size()) return false;
  auto& cfg = settings::current().sensors;
  for (size_t i = 0; i < cfg.size(); i++) {
    if (memcmp(cfg[i].rom, list[index].rom, 8) == 0) {
      cfg.erase(cfg.begin() + i);
      build();
      return true;
    }
  }
  return false;
}

void loop() {
  onewire_temp::loop();
  for (auto& s : list) {
    if (s.busIndex < 0) {
      s.ok = false;
      continue;
    }
    float t = onewire_temp::temperature(s.busIndex);
    s.errors = onewire_temp::errorCount(s.busIndex);
    s.ok = t != onewire_temp::kNoReading && t > kMinValidC && t < kMaxValidC;
    if (s.ok) s.tempC = t + s.offsetC;
  }
}

int count() { return (int)list.size(); }

const Info& info(int index) {
  static Info none;
  return (index >= 0 && index < (int)list.size()) ? list[index] : none;
}

bool read(SensorRole role, float& tempC) {
  if (overrideOn && role == overrideRole) {
    tempC = overrideC;
    return true;
  }
  for (const auto& s : list) {
    if (s.role == role && s.ok) {
      tempC = s.tempC;
      return true;
    }
  }
  return false;
}

bool assigned(SensorRole role) {
  if (overrideOn && role == overrideRole) return true;
  for (const auto& s : list)
    if (s.role == role) return true;
  return false;
}

bool criticalFault() {
  for (const auto& s : list) {
    if (!isCritical(s.role) || s.ok) continue;
    if (overrideOn && s.role == overrideRole) continue;  // replaced by the simulation
    return true;
  }
  return false;
}

void setOverride(SensorRole role, float tempC) {
  overrideOn = true;
  overrideRole = role;
  overrideC = tempC;
}

void clearOverride() { overrideOn = false; }

}  // namespace sensors
