// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tools: fake heating, autotune, pump prime, output test, system info.

#include <Arduino.h>
#include <stdio.h>

#include "app/controller.h"
#include "core/settings.h"
#include "kit.h"
#include "screens.h"
#include "storage/files.h"
#include "ui.h"

#ifndef MT_VERSION
#define MT_VERSION "0.1.0-dev"
#endif

namespace ui {
namespace tools {
namespace {
lv_obj_t* about;
}

void create(lv_obj_t* root) {
  lv_obj_t* list = kit::scrollList(root);
  const controller::Status& st = controller::status();

  kit::rowHeader(list, "TESTING");
  kit::rowSwitch(list, "Fake heating (simulated kettle)", st.sim,
                 [](bool on) { controller::setSim(on, settings::current().sim.speed); });
  kit::rowValue(list, "Simulated temperature", "set...", []() {
    if (!controller::status().sim) {
      kit::toast("Fake heating is off");
      return;
    }
    kit::editNumber("Simulated water temperature", toDisplay(controller::status().tempC), toDisplay(0),
                    toDisplay(100), 1, 1, unit(), [](float v) { controller::setSimTemp(fromDisplay(v)); });
  });
  lv_obj_t* b = kit::button(list, LV_SYMBOL_EYE_OPEN " Output and button test", []() {
    if (controller::status().mode != controller::Mode::Idle) kit::message("Busy", "Stop the running mode first.");
    else go(Screen::Bringup);
  });
  lv_obj_set_width(b, LV_PCT(100));

  kit::rowHeader(list, "NETWORK");
  b = kit::button(list, LV_SYMBOL_WIFI " Wi-Fi and web UI", []() { go(Screen::Wifi); });
  lv_obj_set_width(b, LV_PCT(100));

  kit::rowHeader(list, "PUMP AND HEATER");
  if (st.spargeAvailable) {
    kit::rowSwitch(list, "Sparge water heater", st.spargeOn, [](bool on) {
      if (!controller::setSparge(on)) kit::message("Sparge heater", controller::lastError());
    });
    kit::rowValue(list, "Sparge water temperature", "set...", []() {
      kit::editNumber("Sparge water", toDisplay(controller::status().spargeSetC), toDisplay(30), toDisplay(99), 0.5f, 1,
                      unit(), [](float v) { controller::setSpargeTemp(fromDisplay(v)); });
    });
  }
  b = kit::button(list, LV_SYMBOL_REFRESH " Prime the pump", []() { controller::pumpPrime(); });
  lv_obj_set_width(b, LV_PCT(100));
  b = kit::button(list, LV_SYMBOL_SHUFFLE " PID autotune...", []() {
    kit::editNumber("Autotune at (water in the kettle)", toDisplay(65), toDisplay(30), toDisplay(90), 0.5f, 1,
                    unit(), [](float v) {
                      if (!controller::startAutotune(fromDisplay(v)))
                        kit::message("Cannot start", controller::lastError());
                    });
  });
  lv_obj_set_width(b, LV_PCT(100));

  kit::rowHeader(list, "ABOUT");
  about = kit::label(list, "", kit::kSmall);
  lv_label_set_long_mode(about, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(about, LV_PCT(100));
  update();
}

void update() {
  char buf[256];
  uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf),
           "MashTouch %s (GPL-3.0-or-later, based on Open ArdBir)\n"
           "Storage: %u of %u kB used\n"
           "Heap: %u kB free, PSRAM: %u kB free\n"
           "Up: %u h %02u min",
           MT_VERSION, (unsigned)(files::usedBytes() / 1024), (unsigned)(files::totalBytes() / 1024),
           (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(up / 3600),
           (unsigned)(up / 60 % 60));
  lv_label_set_text(about, buf);
}

}  // namespace tools
}  // namespace ui
