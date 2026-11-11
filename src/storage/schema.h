// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// JSON form of settings, recipes and the resume snapshot. Each struct has
// one field list used for both directions. Reading keeps the current value
// for missing keys and for values of the wrong type or out of range, so
// older files load with defaults for new settings.
#pragma once
#include <ArduinoJson.h>

#include <string>

#include "core/brew.h"
#include "core/settings.h"

namespace storage {

constexpr int kSettingsVersion = 1;
constexpr int kRecipeVersion = 3;  // 3: mashIn, rests, mashOut (2: one steps list)

void toJson(const settings::Settings& s, JsonObject out);
void fromJson(JsonObjectConst in, settings::Settings& s);

void toJson(const settings::Recipe& r, JsonObject out);
void fromJson(JsonObjectConst in, settings::Recipe& r);

void toJson(const brew::Snapshot& s, JsonObject out);
void fromJson(JsonObjectConst in, brew::Snapshot& s);

// Fixes or rejects values that would break the process. Returns false with
// a reason if the recipe cannot be used.
bool validate(settings::Recipe& r, std::string& err);

// Set the value at a dotted path ("heater.kp", "steps.0.minutes") inside a
// document. The value is parsed as JSON (number, true/false, "text"); bare
// words are taken as strings. The path must already exist.
bool setPath(JsonDocument& doc, const char* path, const char* value, std::string& err);
// The value at a path, or null.
JsonVariantConst getPath(const JsonDocument& doc, const char* path);
// Equal values (numbers with a small tolerance for float rounding).
bool sameValue(JsonVariantConst a, JsonVariantConst b);

}  // namespace storage
