// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See LICENSE and NOTICE.md.

#include <Arduino.h>

#include "app/console.h"
#include "app/controller.h"
#include "hal/audio.h"
#include "hal/display.h"
#include "hal/io.h"
#include "net/web.h"
#include "net/wifi.h"
#include "core/settings.h"
#include "storage/storage.h"
#include "ui/ui.h"

void setup() {
  // Outputs first, so heater and pump are guaranteed off from boot.
  io::begin();

  Serial.setRxBufferSize(4096);  // room for a pasted recipe file
  Serial.begin(115200);
  Serial.println("MashTouch");

  if (!display::begin()) {
    Serial.println("Display init failed");
  }
  storage::begin();     // settings and active recipe from LittleFS
  controller::begin();  // sensors, output roles (all off), resume point
  console::begin();
  wifi::begin();        // home network, or the setup access point
  web::begin();         // web UI and JSON API on port 80
  Serial.println("Type 'help' for console commands");

  ui::begin();
  audio::begin();
  audio::setVolume(settings::current().sound.volume);
  audio::play(audio::Sound::Startup);

  // Reset if loop() stalls for longer than the task watchdog timeout (5 s).
  enableLoopWDT();
}

void loop() {
  io::loop();
  controller::loop();
  console::loop();
  wifi::loop();
  web::loop();
  display::loop();
  delay(1);
}
