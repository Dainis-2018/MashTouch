// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"

namespace settings {
namespace {

// C++11: structs with member defaults cannot be brace-initialised.
OutputConfig output(OutputRole role, uint8_t index) {
  OutputConfig o;
  o.role = role;
  o.index = index;
  return o;
}

MashStep step(const char* name, float tempC, uint16_t minutes) {
  MashStep s;
  s.name = name;
  s.tempC = tempC;
  s.minutes = minutes;
  return s;
}

Addition addition(const char* name, uint16_t atMinutes) {
  Addition a;
  a.name = name;
  a.atMinutes = atMinutes;
  a.unit = "g";
  return a;
}

}  // namespace

Settings defaults() {
  Settings s;
  s.outputs[0] = output(OutputRole::Heater, 1);  // OUT1
  s.outputs[4] = output(OutputRole::Pump, 1);    // PWR1
  s.outputs[5] = output(OutputRole::Fan, 1);     // PWR2
  return s;
}

Recipe::Recipe() {
  mashIn = step("Mash in", 67.0f, 0);
  mashOut = step("Mash out", 76.0f, 10);
}

void arrangeMash(Recipe& r, std::vector<MashStep> steps, int mashOutIndex) {
  r.rests.clear();
  if (steps.empty()) {
    r.mash = false;
    return;
  }
  r.mash = true;
  r.mashIn = steps.front();
  if (r.mashIn.name.empty()) r.mashIn.name = "Mash in";
  if (mashOutIndex == 0) mashOutIndex = -1;  // the mash in cannot also be the mash out
  bool haveOut = false;
  for (size_t i = 1; i < steps.size(); i++) {
    if ((int)i == mashOutIndex) {
      r.mashOut = steps[i];
      haveOut = true;
    } else {
      r.rests.push_back(steps[i]);
    }
  }
  if (!haveOut && mashOutIndex < 0 && !r.rests.empty() && r.rests.back().tempC >= 75) {
    r.mashOut = r.rests.back();
    r.rests.pop_back();
    haveOut = true;
  }
  if (!haveOut) {
    // No mash out heating: hold the last temperature, then remove the malt.
    r.mashOut = step("Mash out", r.rests.empty() ? r.mashIn.tempC : r.rests.back().tempC, 0);
    r.mashOut.pump = r.rests.empty() ? r.mashIn.pump : r.rests.back().pump;
  }
  if (r.mashOut.name.empty()) r.mashOut.name = "Mash out";
}

Recipe defaultRecipe() {
  Recipe r;
  r.name = "Single infusion";
  r.mashIn = step("Mash in", 68.0f, 0);
  r.rests.push_back(step("Saccharification", 66.0f, 60));
  r.mashOut = step("Mash out", 76.0f, 10);
  r.boilMinutes = 60;
  r.additions.push_back(addition("Bittering hops", 60));
  r.additions.push_back(addition("Aroma hops", 15));
  r.additions.push_back(addition("Flameout hops", 0));
  return r;
}

Settings& current() {
  static Settings s = defaults();
  return s;
}

Recipe& recipe() {
  static Recipe r = defaultRecipe();
  return r;
}

const char* roleName(OutputRole r) {
  switch (r) {
    case OutputRole::Heater: return "heater";
    case OutputRole::Pump: return "pump";
    case OutputRole::Fan: return "fan";
    case OutputRole::Sparge: return "sparge";
    case OutputRole::Alarm: return "alarm";
    case OutputRole::Contactor: return "contactor";
    default: return "none";
  }
}

const char* roleName(SensorRole r) {
  switch (r) {
    case SensorRole::Kettle: return "kettle";
    case SensorRole::Rims: return "rims";
    case SensorRole::Heatsink: return "heatsink";
    case SensorRole::Aux: return "aux";
    case SensorRole::Sparge: return "sparge";
    default: return "none";
  }
}

}  // namespace settings
