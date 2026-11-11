// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#include "board/pins.h"
#include "es8311.h"

namespace audio {
namespace {

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kRate = 22050;
constexpr int kChunk = 256;  // samples rendered per write
constexpr int kMaxNotes = 8;
constexpr float kTwoPi = 6.2831853f;

struct Note {
  uint16_t freq;  // Hz, 0 = silence
  uint16_t ms;
};

struct Request {
  Note notes[kMaxNotes];
  uint8_t count;
  uint8_t repeat;  // times the whole phrase is played
  bool bell;       // bell (decaying, with an overtone) or plain beep
};

QueueHandle_t queue = nullptr;
volatile uint8_t volumePct = 70;
bool ready = false;

// Notes: C5 523, D5 587, E5 659, G5 784, A5 880, B5 988, C6 1047, E6 1319, G6 1568.
Request phrase(Sound s) {
  switch (s) {
    case Sound::Startup: return {{{784, 110}, {1047, 220}}, 2, 1, true};
    case Sound::Click: return {{{1500, 18}}, 1, 1, false};
    case Sound::TempReached: return {{{880, 160}, {1319, 380}}, 2, 1, true};
    case Sound::StepDone: return {{{1047, 280}, {784, 520}}, 2, 1, true};
    case Sound::Prompt: return {{{784, 200}, {1047, 200}, {1319, 420}, {0, 250}}, 4, 2, true};
    case Sound::Reminder: return {{{1047, 140}, {0, 70}, {1047, 220}}, 3, 1, true};
    case Sound::Addition: return {{{1319, 150}, {1047, 150}, {1319, 150}, {1047, 150}, {1568, 450}, {0, 250}}, 6, 2, true};
    case Sound::TimerDone: return {{{988, 200}, {1319, 200}, {988, 200}, {1319, 450}, {0, 200}}, 5, 2, true};
    case Sound::BoilSoon: return {{{1480, 120}, {0, 70}, {1480, 120}, {0, 70}, {1480, 120}, {0, 300}}, 6, 2, false};
    case Sound::Finished: return {{{523, 180}, {659, 180}, {784, 180}, {1047, 260}, {784, 160}, {1047, 600}}, 6, 1, true};
    case Sound::Fault: return {{{392, 320}, {0, 90}, {311, 480}, {0, 200}}, 4, 2, false};
  }
  return {{{880, 100}}, 1, 1, false};
}

void render(const Request& r) {
  static int16_t buf[kChunk];
  uint8_t vol = volumePct;
  if (vol == 0) return;
  // Perceived loudness is roughly the square of the amplitude.
  float gain = (vol / 100.0f) * (vol / 100.0f) * 14000.0f;

  for (uint8_t rep = 0; rep < r.repeat; rep++) {
    for (uint8_t n = 0; n < r.count; n++) {
      const Note& note = r.notes[n];
      int total = (int)((long)note.ms * kRate / 1000);
      float step = kTwoPi * note.freq / kRate;
      float phase = 0;
      int attack = kRate / 250;                    // 4 ms: no click at the start
      int release = kRate / 100;                   // 10 ms: no click at the end
      float tau = (r.bell ? 0.35f : 10.0f) * kRate;  // bell: dies away; beep: flat
      for (int done = 0; done < total;) {
        int len = total - done < kChunk ? total - done : kChunk;
        for (int i = 0; i < len; i++) {
          int k = done + i;
          float v = 0;
          if (note.freq) {
            float env = expf(-k / tau);
            if (k < attack) env *= (float)k / attack;
            if (total - k < release) env *= (float)(total - k) / release;
            v = sinf(phase);
            if (r.bell) v = 0.75f * v + 0.25f * sinf(phase * 2.0f);  // a little overtone
            v *= env * gain;
            phase += step;
            if (phase > kTwoPi) phase -= kTwoPi;
          }
          buf[i] = (int16_t)v;
        }
        size_t written = 0;
        i2s_write(kPort, buf, len * sizeof(int16_t), &written, portMAX_DELAY);
        done += len;
      }
    }
  }
  // Let the last samples out, then silence (the amplifier idles quietly).
  memset(buf, 0, sizeof(buf));
  size_t written = 0;
  for (int i = 0; i < 6; i++) i2s_write(kPort, buf, sizeof(buf), &written, portMAX_DELAY);
}

void task(void*) {
  Request r;
  for (;;) {
    if (xQueueReceive(queue, &r, portMAX_DELAY) == pdTRUE) render(r);
  }
}

void enqueue(const Request& r) {
  if (!ready) return;
  xQueueSend(queue, &r, 0);  // drop it if three sounds are already waiting
}

}  // namespace

void begin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = kChunk;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;  // silence, not noise, if the task falls behind
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // the codec runs from MCLK = 256 fs
  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) return;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_I2S_MCLK;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRCLK;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kPort, &pins) != ESP_OK) return;
  i2s_zero_dma_buffer(kPort);
  // MCLK is running now; the codec can be set up.
  if (!es8311::begin()) Serial.println("ES8311 codec not found: no sound");

  queue = xQueueCreate(3, sizeof(Request));
  if (!queue) return;
  xTaskCreatePinnedToCore(task, "audio", 4096, nullptr, 2, nullptr, 0);
  ready = true;
}

void play(Sound s) { enqueue(phrase(s)); }

void tone(uint16_t freqHz, uint16_t ms, uint8_t count) {
  Request r = {};
  r.bell = false;
  r.repeat = 1;
  r.count = 0;
  for (uint8_t i = 0; i < count && r.count + 2 <= kMaxNotes; i++) {
    r.notes[r.count++] = {freqHz, ms};
    r.notes[r.count++] = {0, ms};
  }
  enqueue(r);
}

void setVolume(uint8_t pct) { volumePct = pct > 100 ? 100 : pct; }

}  // namespace audio
