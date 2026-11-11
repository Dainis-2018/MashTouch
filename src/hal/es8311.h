// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// ES8311 audio codec (Waveshare 3.5 board): playback only. The ESP32 is the
// I2S master and drives MCLK at 256 x the sample rate; the codec is the slave.
// Register values follow the Everest ES8311 datasheet and user guide.
#pragma once
#include <stdint.h>

namespace es8311 {

// Configure the codec for 16-bit I2S playback at MCLK = 256 fs. Call after
// the I2S driver is running (MCLK present). Blocks a few ms (setup only).
// Returns false if the codec does not answer on I2C.
bool begin();

// 0..100 % DAC volume (0 = mute). Not needed for normal use: the chime
// loudness is set in the synthesiser; the DAC stays at its begin() level.
void setVolume(uint8_t pct);

uint8_t address();  // 0 if not found

}  // namespace es8311
