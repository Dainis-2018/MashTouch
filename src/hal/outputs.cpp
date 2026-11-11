// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "outputs.h"

#include "io.h"

namespace outputs {
namespace {

using settings::OutputRole;
using settings::kFirstPowerChannel;
using settings::kOutputChannels;

bool testAllowed = false;
bool testSet[kOutputChannels];
uint8_t testValue[kOutputChannels];
uint8_t current[kOutputChannels];

void write(int ch, uint8_t value, bool inverted) {
  current[ch] = value;
  if (ch < kFirstPowerChannel) {
    bool on = value > 0;
    io::setSignal(ch, inverted ? !on : on);
  } else {
    io::setPower(ch - kFirstPowerChannel, inverted ? 100 - value : value);
  }
}

}  // namespace

void begin() { apply(Demand{}); }

void apply(const Demand& d) {
  const auto& cfg = settings::current().outputs;
  for (int ch = 0; ch < kOutputChannels; ch++) {
    OutputRole role = cfg[ch].role;
    uint8_t v = 0;
    switch (role) {
      case OutputRole::Heater: {
        int stage = cfg[ch].index - 1;
        v = (stage >= 0 && stage < settings::kMaxHeaters && d.heater[stage]) ? 100 : 0;
        break;
      }
      case OutputRole::Pump: v = d.pump ? 100 : 0; break;  // on/off: SSR, relay or 12 V direct
      case OutputRole::Fan: v = d.fanPct; break;
      case OutputRole::Sparge: v = d.sparge ? 100 : 0; break;
      case OutputRole::Alarm: v = d.alarm ? 100 : 0; break;
      case OutputRole::Contactor: v = d.contactor ? 100 : 0; break;
      default: v = 0; break;
    }
    if (testAllowed && testSet[ch]) v = testValue[ch];
    if (d.safetyCut && (role == OutputRole::Heater || role == OutputRole::Pump || role == OutputRole::Sparge ||
                        role == OutputRole::Contactor))
      v = 0;
    write(ch, v, cfg[ch].inverted);
  }
}

int heaterCount() {
  int n = 1;
  for (const auto& o : settings::current().outputs)
    if (o.role == OutputRole::Heater && o.index > n) n = o.index;
  return n > settings::kMaxHeaters ? settings::kMaxHeaters : n;
}

bool hasRole(OutputRole role) {
  for (const auto& o : settings::current().outputs)
    if (o.role == role) return true;
  return false;
}

void setTestAllowed(bool allowed) {
  testAllowed = allowed;
  if (!allowed)
    for (int ch = 0; ch < kOutputChannels; ch++) testSet[ch] = false;
}

void setTest(int channel, uint8_t value) {
  if (!testAllowed || channel < 0 || channel >= kOutputChannels) return;
  testSet[channel] = true;
  testValue[channel] = value > 100 ? 100 : value;
}

uint8_t value(int channel) {
  return (channel >= 0 && channel < kOutputChannels) ? current[channel] : 0;
}

}  // namespace outputs
