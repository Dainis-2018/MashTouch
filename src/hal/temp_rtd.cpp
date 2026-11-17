// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MAX31865 driver written from the Maxim/Analog Devices datasheet.

#include "temp_rtd.h"

#include <Arduino.h>
#include <SPI.h>
#include <math.h>

#include "board/pins.h"

namespace rtd_temp {
namespace {

// Registers (read address; writes set bit 7)
constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegRtdMsb = 0x01;       // + LSB at 0x02, bit 0 = fault
constexpr uint8_t kRegHighFaultMsb = 0x03;  // high MSB/LSB, low MSB/LSB
constexpr uint8_t kRegFault = 0x07;
constexpr uint8_t kWrite = 0x80;

// Configuration bits
constexpr uint8_t kCfgBias = 0x80;
constexpr uint8_t kCfgAuto = 0x40;
constexpr uint8_t kCfgOneShot = 0x20;     // self-clearing
constexpr uint8_t kCfg3Wire = 0x10;
constexpr uint8_t kCfgFaultCycle = 0x0C;  // fault detection cycle (unused)
constexpr uint8_t kCfgFaultClear = 0x02;  // self-clearing
constexpr uint8_t kCfg50Hz = 0x01;
constexpr uint8_t kCfgStable = (uint8_t)~(kCfgOneShot | kCfgFaultCycle | kCfgFaultClear);

constexpr uint32_t kReadMs = 250;    // automatic conversions run every ~20 ms
constexpr uint32_t kRetryMs = 5000;  // a missing chip is looked for this often
constexpr uint16_t kRawOpen = 32700; // at full scale: probe or wire open
constexpr uint8_t kFaultsBeforeLoss = 3;  // keep the last value over brief glitches

const int kCsPins[kChannels] = {PIN_MAX31865_CS1, PIN_MAX31865_CS2};

// SPI3: the display has SPI2 to itself.
SPIClass spi(HSPI);
const SPISettings kSpi(1000000, MSBFIRST, SPI_MODE1);
bool spiStarted = false;

struct Channel {
  Config cfg;
  bool configured = false;  // cfg was applied to the chip at least once
  bool present = false;
  uint8_t cfgByte = 0;
  float tempC = kNoReading;
  float ohms = 0;
  uint16_t raw = 0;
  uint8_t fault = 0;
  uint16_t errors = 0;
  uint32_t lastTry = 0;
};
Channel chans[kChannels];
uint32_t lastRead = 0;

bool valid(int ch) { return ch >= 0 && ch < kChannels; }

void startSpi() {
  if (spiStarted) return;
  for (int pin : kCsPins) {
    digitalWrite(pin, HIGH);
    pinMode(pin, OUTPUT);
  }
  spi.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  spiStarted = true;
}

void transfer(int ch, uint8_t addr, uint8_t* data, int n, bool write) {
  spi.beginTransaction(kSpi);
  digitalWrite(kCsPins[ch], LOW);
  spi.transfer(write ? (uint8_t)(addr | kWrite) : addr);
  for (int i = 0; i < n; i++) {
    uint8_t in = spi.transfer(write ? data[i] : 0xFF);
    if (!write) data[i] = in;
  }
  digitalWrite(kCsPins[ch], HIGH);
  spi.endTransaction();
}

void writeReg(int ch, uint8_t addr, uint8_t v) { transfer(ch, addr, &v, 1, true); }

uint8_t readReg(int ch, uint8_t addr) {
  uint8_t v = 0;
  transfer(ch, addr, &v, 1, false);
  return v;
}

uint8_t configByte(const Config& c) {
  uint8_t b = kCfgBias | kCfgAuto;
  if (c.wires == 3) b |= kCfg3Wire;
  if (c.filter50Hz) b |= kCfg50Hz;
  return b;
}

// Program the chip and check that it answers with what was written.
void apply(int ch) {
  Channel& c = chans[ch];
  c.lastTry = millis();
  c.configured = true;
  startSpi();
  c.cfgByte = configByte(c.cfg);
  // The filter may only change while automatic conversion is off.
  writeReg(ch, kRegConfig, c.cfgByte & ~kCfgAuto);
  // Thresholds wide open: open and short are judged from the reading itself.
  uint8_t thresholds[4] = {0xFF, 0xFF, 0x00, 0x00};
  transfer(ch, kRegHighFaultMsb, thresholds, 4, true);
  writeReg(ch, kRegConfig, c.cfgByte | kCfgFaultClear);
  c.present = (readReg(ch, kRegConfig) & kCfgStable) == c.cfgByte;
  if (!c.present) {
    c.fault = 0;
    c.tempC = kNoReading;
  }
}

void failRead(Channel& c) {
  if (c.errors < UINT16_MAX) c.errors++;
  if (c.errors >= kFaultsBeforeLoss) c.tempC = kNoReading;
}

void readChannel(int ch) {
  Channel& c = chans[ch];
  // A chip that stops answering (unplugged breakout, broken wire on the
  // backpack) no longer returns its configuration.
  if ((readReg(ch, kRegConfig) & kCfgStable) != c.cfgByte) {
    c.present = false;
    c.fault = 0;
    failRead(c);
    return;
  }
  uint8_t b[2] = {0, 0};
  transfer(ch, kRegRtdMsb, b, 2, false);
  uint16_t word = (uint16_t)(b[0] << 8 | b[1]);
  c.raw = word >> 1;
  c.ohms = c.raw * c.cfg.refOhm / 32768.0f;
  uint8_t fault = 0;
  if (word & 1) {
    fault = readReg(ch, kRegFault) & 0xFC;
    writeReg(ch, kRegConfig, c.cfgByte | kCfgFaultClear);
  }
  if (c.raw >= kRawOpen) fault |= kFaultOpen;
  else if (c.ohms < c.cfg.r0 * 0.5f) fault |= kFaultShort;  // below about -120 C
  c.fault = fault;
  if (fault) {
    failRead(c);
    return;
  }
  c.tempC = ohmsToCelsius(c.ohms, c.cfg.r0);
  c.errors = 0;
}

}  // namespace

void configure(int ch, const Config& cfg) {
  if (!valid(ch)) return;
  Channel& c = chans[ch];
  bool same = c.configured && c.cfg.enabled == cfg.enabled && c.cfg.r0 == cfg.r0 && c.cfg.refOhm == cfg.refOhm &&
              c.cfg.wires == cfg.wires && c.cfg.filter50Hz == cfg.filter50Hz;
  if (same) return;
  c.cfg = cfg;
  c.errors = 0;
  c.fault = 0;
  c.tempC = kNoReading;
  if (!cfg.enabled) {
    c.configured = true;
    c.present = false;
    if (spiStarted) writeReg(ch, kRegConfig, 0);  // bias off
    return;
  }
  apply(ch);
}

void recheck() {
  for (int ch = 0; ch < kChannels; ch++)
    if (chans[ch].cfg.enabled) apply(ch);
}

void loop() {
  uint32_t now = millis();
  for (int ch = 0; ch < kChannels; ch++) {
    Channel& c = chans[ch];
    if (c.cfg.enabled && !c.present && now - c.lastTry >= kRetryMs) apply(ch);
  }
  if (now - lastRead < kReadMs) return;
  lastRead = now;
  for (int ch = 0; ch < kChannels; ch++)
    if (chans[ch].cfg.enabled && chans[ch].present) readChannel(ch);
}

bool enabled(int ch) { return valid(ch) && chans[ch].cfg.enabled; }
bool present(int ch) { return valid(ch) && chans[ch].present; }
float temperature(int ch) { return valid(ch) ? chans[ch].tempC : kNoReading; }
float resistance(int ch) { return valid(ch) ? chans[ch].ohms : 0; }
uint16_t raw(int ch) { return valid(ch) ? chans[ch].raw : 0; }
uint8_t faultBits(int ch) { return valid(ch) ? chans[ch].fault : 0; }
uint16_t errorCount(int ch) { return valid(ch) ? chans[ch].errors : 0; }

const char* faultText(uint8_t bits) {
  if (bits & kFaultOpen) return "open circuit";
  if (bits & kFaultShort) return "short circuit";
  if (bits & kFaultRefInHigh) return "REFIN- high (wiring)";
  if (bits & kFaultRefInLow) return "REFIN- low (FORCE- open)";
  if (bits & kFaultRtdInLow) return "RTDIN- low (FORCE- open)";
  if (bits & kFaultVoltage) return "over/undervoltage";
  if (bits & (kFaultHighThreshold | kFaultLowThreshold)) return "out of range";
  return "";
}

float ohmsToCelsius(float ohms, float r0) {
  // IEC 60751 coefficients
  const double A = 3.9083e-3, B = -5.775e-7, C = -4.183e-12;
  double ratio = ohms / r0;
  // R/R0 = 1 + A t + B t^2 for t >= 0: solve the quadratic.
  double disc = A * A - 4 * B * (1 - ratio);
  if (disc < 0) disc = 0;
  double t = (-A + sqrt(disc)) / (2 * B);
  if (ratio >= 1) return (float)t;
  // Below 0 C a C (t - 100) t^3 term is added: refine with Newton's method,
  // starting from the quadratic result (already within a few hundredths).
  for (int i = 0; i < 4; i++) {
    double f = 1 + A * t + B * t * t + C * (t - 100) * t * t * t - ratio;
    double df = A + 2 * B * t + C * (4 * t * t * t - 300 * t * t);
    t -= f / df;
  }
  return (float)t;
}

}  // namespace rtd_temp
