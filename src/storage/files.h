// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LittleFS helpers: mount, and JSON files written atomically (temp file,
// then rename) so a power cut never leaves a half-written file.
#pragma once
#include <ArduinoJson.h>

#include <string>
#include <vector>

namespace files {

// Mounts LittleFS; formats the partition if it cannot be mounted.
bool begin();
bool mounted();

bool readJson(const char* path, JsonDocument& doc, std::string& err);
bool writeJson(const char* path, const JsonDocument& doc, std::string& err);
bool exists(const char* path);
bool remove(const char* path);
bool rename(const char* from, const char* to);
bool ensureDir(const char* path);
// File names (without directory) in a directory.
std::vector<std::string> list(const char* dir);

size_t usedBytes();
size_t totalBytes();

}  // namespace files
