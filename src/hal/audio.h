// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound through I2S to the ES8311 codec + amplifier on the Waveshare board
// and a small speaker.
// Chimes are synthesised (bell-like tones with a soft attack and a decay),
// so every event has its own recognisable sound and no audio files are
// needed. Playback runs in its own task and never blocks loop().
#pragma once
#include <stdint.h>

namespace audio {

enum class Sound : uint8_t {
  Startup,
  Click,        // key or button feedback
  TempReached,  // one soft "ding"
  StepDone,     // ding-dong: a step is over, the next one starts
  Prompt,       // rising three notes: the user must do something
  Reminder,     // short double note: a prompt is still waiting
  Addition,     // hop or other addition: add now
  TimerDone,    // manual countdown finished
  BoilSoon,     // urgent: watch for a boil-over
  Finished,     // brew finished
  Fault,        // low, harsh: sensor fault or safety cut
};

void begin();
void play(Sound s);
// Plain beeps (hardware test, simple feedback).
void tone(uint16_t freqHz, uint16_t ms, uint8_t count = 1);
// 0..100; 0 is silent.
void setVolume(uint8_t pct);

}  // namespace audio
