// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <LittleFS.h>

namespace files {
namespace {
bool isMounted = false;
}

bool begin() {
  isMounted = LittleFS.begin(true);  // format on first use or corruption
  return isMounted;
}

bool mounted() { return isMounted; }

bool readJson(const char* path, JsonDocument& doc, std::string& err) {
  if (!isMounted) {
    err = "no filesystem";
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    err = std::string("cannot open ") + path;
    return false;
  }
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    err = std::string(path) + ": " + e.c_str();
    return false;
  }
  return true;
}

bool writeJson(const char* path, const JsonDocument& doc, std::string& err) {
  if (!isMounted) {
    err = "no filesystem";
    return false;
  }
  std::string tmp = std::string(path) + ".tmp";
  File f = LittleFS.open(tmp.c_str(), "w");
  if (!f) {
    err = std::string("cannot write ") + path;
    return false;
  }
  size_t want = measureJson(doc);
  size_t wrote = serializeJson(doc, f);
  f.close();
  if (wrote != want) {
    LittleFS.remove(tmp.c_str());
    err = std::string("filesystem full writing ") + path;
    return false;
  }
  if (!LittleFS.rename(tmp.c_str(), path)) {
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp.c_str(), path)) {
      err = std::string("cannot replace ") + path;
      return false;
    }
  }
  return true;
}

bool exists(const char* path) { return isMounted && LittleFS.exists(path); }

bool remove(const char* path) { return isMounted && LittleFS.remove(path); }

bool rename(const char* from, const char* to) {
  if (!isMounted) return false;
  LittleFS.remove(to);
  return LittleFS.rename(from, to);
}

bool ensureDir(const char* path) {
  if (!isMounted) return false;
  return LittleFS.exists(path) || LittleFS.mkdir(path);
}

std::vector<std::string> list(const char* dir) {
  std::vector<std::string> out;
  if (!isMounted) return out;
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) return out;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory()) out.push_back(f.name());  // core 2.x: name without directory
  }
  return out;
}

size_t usedBytes() { return isMounted ? LittleFS.usedBytes() : 0; }
size_t totalBytes() { return isMounted ? LittleFS.totalBytes() : 0; }

}  // namespace files
