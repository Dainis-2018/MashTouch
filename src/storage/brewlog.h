// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brew log: one file per automatic brew in /logs/NNNN.jsonl, one JSON object
// per line, appended as the brew goes on (so a power cut loses nothing):
//   {"v":1,"id":7,"recipe":"Pale Ale","start":1794300000,"sim":false}   header
//   {"t":30,"T":45.2,"sp":66.0,"h":100,"p":1}      sample every 30 s
//   {"t":912,"e":"Add the grain"}                  event
//   {"t":11520,"end":"finished"}                   last line: finished | stopped
// t is seconds since the brew started; start is Unix time, 0 if the clock
// was not set. The newest 30 logs are kept.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace brewlog {

struct Info {
  int id = 0;
  std::string recipe;
  uint32_t start = 0;      // Unix time, 0 = unknown
  bool sim = false;
  uint32_t durationS = 0;
  std::string result;      // "finished", "stopped" or "" (cut off, or still running)
  size_t size = 0;
};

// Start a new log; it becomes the active one.
void begin(const std::string& recipe, uint32_t startEpoch, bool sim);
// Continue the newest log if it has no end line (resume after power loss).
bool resume();
bool active();
int activeId();

// tS: seconds since this start() or resume().
void sample(uint32_t tS, float tempC, float setpointC, uint8_t duty, bool pump);
void event(uint32_t tS, const std::string& text);
void end(uint32_t tS, const char* result);

std::vector<Info> list();  // newest first
std::string path(int id);
bool remove(int id);

}  // namespace brewlog
