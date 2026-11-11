// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "brewlog.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <math.h>

#include <algorithm>

#include "files.h"

namespace brewlog {
namespace {

constexpr const char* kDir = "/logs";
constexpr size_t kKeep = 30;
constexpr size_t kMinFreeBytes = 256 * 1024;

int current = 0;       // active log id, 0 = none
uint32_t baseS = 0;    // time already in the log before a resume

std::vector<int> ids() {
  std::vector<int> out;
  for (const std::string& f : files::list(kDir)) {
    if (f.size() > 6 && f.compare(f.size() - 6, 6, ".jsonl") == 0) {
      int id = atoi(f.c_str());
      if (id > 0) out.push_back(id);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

void appendLine(const JsonDocument& doc) {
  if (!current || !files::mounted()) return;
  File f = LittleFS.open(path(current).c_str(), "a");
  if (!f) return;
  serializeJson(doc, f);
  f.print('\n');
  f.close();
}

// The last complete line of a file (logs end with '\n').
std::string lastLine(File& f) {
  size_t size = f.size();
  size_t from = size > 240 ? size - 240 : 0;
  f.seek(from);
  std::string tail;
  while (f.available()) tail += (char)f.read();
  while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r')) tail.pop_back();
  size_t nl = tail.find_last_of('\n');
  return nl == std::string::npos ? (from == 0 ? tail : std::string()) : tail.substr(nl + 1);
}

bool readInfo(int id, Info& info) {
  File f = LittleFS.open(path(id).c_str(), "r");
  if (!f) return false;
  info.id = id;
  info.size = f.size();
  String head = f.readStringUntil('\n');
  JsonDocument doc;
  if (!deserializeJson(doc, head)) {
    info.recipe = doc["recipe"] | "";
    info.start = doc["start"] | (uint32_t)0;
    info.sim = doc["sim"] | false;
  }
  JsonDocument last;
  if (!deserializeJson(last, lastLine(f))) {
    info.durationS = last["t"] | (uint32_t)0;
    info.result = last["end"] | "";
  }
  f.close();
  return true;
}

void prune() {
  std::vector<int> all = ids();
  size_t i = 0;
  while (i < all.size() && (all.size() - i >= kKeep || files::totalBytes() - files::usedBytes() < kMinFreeBytes)) {
    LittleFS.remove(path(all[i]).c_str());
    i++;
  }
}

}  // namespace

std::string path(int id) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%s/%04d.jsonl", kDir, id);
  return buf;
}

void begin(const std::string& recipe, uint32_t startEpoch, bool sim) {
  current = 0;
  baseS = 0;
  if (!files::ensureDir(kDir)) return;
  prune();
  std::vector<int> all = ids();
  current = all.empty() ? 1 : all.back() + 1;
  JsonDocument doc;
  doc["v"] = 1;
  doc["id"] = current;
  doc["recipe"] = recipe;
  doc["start"] = startEpoch;
  doc["sim"] = sim;
  appendLine(doc);
}

bool resume() {
  current = 0;
  baseS = 0;
  std::vector<int> all = ids();
  if (all.empty()) return false;
  Info info;
  if (!readInfo(all.back(), info) || !info.result.empty()) return false;
  current = info.id;
  baseS = info.durationS;
  return true;
}

bool active() { return current != 0; }
int activeId() { return current; }

void sample(uint32_t tS, float tempC, float setpointC, uint8_t duty, bool pump) {
  JsonDocument doc;
  doc["t"] = baseS + tS;
  doc["T"] = lroundf(tempC * 10) / 10.0;
  doc["sp"] = lroundf(setpointC * 10) / 10.0;
  doc["h"] = duty;
  doc["p"] = pump ? 1 : 0;
  appendLine(doc);
}

void event(uint32_t tS, const std::string& text) {
  JsonDocument doc;
  doc["t"] = baseS + tS;
  doc["e"] = text;
  appendLine(doc);
}

void end(uint32_t tS, const char* result) {
  JsonDocument doc;
  doc["t"] = baseS + tS;
  doc["end"] = result;
  appendLine(doc);
  current = 0;
  baseS = 0;
}

std::vector<Info> list() {
  std::vector<Info> out;
  std::vector<int> all = ids();
  for (auto it = all.rbegin(); it != all.rend(); ++it) {
    Info info;
    if (readInfo(*it, info)) out.push_back(info);
  }
  return out;
}

bool remove(int id) {
  if (id == current) return false;  // still being written
  return files::remove(path(id).c_str());
}

}  // namespace brewlog
