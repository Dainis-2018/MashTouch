// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bringup.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app/controller.h"
#include "hal/audio.h"
#include "hal/display.h"
#include "hal/io.h"
#include "hal/outputs.h"
#include "hal/sensors.h"
#include "hal/temp_onewire.h"

namespace ui_bringup {
namespace {

lv_obj_t* tempLabel;
lv_obj_t* statusLabel;
lv_obj_t* touchLabel;
lv_obj_t* ctrlLabel;
lv_obj_t* powerLabels[io::kPowerOutputs];

lv_obj_t* makeRow(lv_obj_t* parent) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 8, 0);
  return row;
}

void onSignalToggled(lv_event_t* e) {
  lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  // Bench-test values only apply while the controller is idle.
  outputs::setTest(index, lv_obj_has_state(btn, LV_STATE_CHECKED) ? 100 : 0);
}

void onPowerChanged(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  int duty = lv_slider_get_value(slider);
  outputs::setTest(io::kSignalOutputs + index, duty);
  lv_label_set_text_fmt(powerLabels[index], "PWR%d %d%%", index + 1, duty);
}

// Speaker test: the "brew finished" melody.
void onBeep(lv_event_t*) { audio::play(audio::Sound::Finished); }

void onBacklight(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  display::setBrightness(lv_slider_get_value(slider));
}

void onRescan(lv_event_t*) { sensors::rescan(); }

void refreshAll() {
  // Temperatures
  int n = onewire_temp::count();
  if (n == 0) {
    lv_label_set_text(tempLabel, "No DS18B20");
  } else {
    char buf[96];
    int len = 0;
    for (int i = 0; i < n && len < (int)sizeof(buf) - 16; i++) {
      float t = onewire_temp::temperature(i);
      if (t == onewire_temp::kNoReading)
        len += snprintf(buf + len, sizeof(buf) - len, "%s--.-", i ? "  " : "");
      else
        len += snprintf(buf + len, sizeof(buf) - len, "%s%.2f", i ? "  " : "", t);
    }
    lv_label_set_text(tempLabel, buf);
  }

  // Front buttons belong to the controller; only show whether they are held.
  const char* names[2] = {"START", "PUMP"};
  const char* last[2];
  for (int i = 0; i < 2; i++) last[i] = io::isHeld(static_cast<io::Button>(i)) ? "held" : "-";
  lv_label_set_text_fmt(statusLabel, "%s: %s   %s: %s   Level: %s   Heap %u k  PSRAM %u k",
                        names[0], last[0], names[1], last[1],
                        io::levelSensorClosed() ? "closed" : "open",
                        (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getFreePsram() / 1024));

  // Controller state
  const auto& c = controller::status();
  int sp10 = (int)(c.setpointC * 10 + 0.5f);
  lv_label_set_text_fmt(ctrlLabel, "%s%s%s  sp %d.%d  heat %s %d%%  pump %s  %s",
                        controller::modeName(c.mode), c.paused ? " paused" : "",
                        c.sim ? " SIM" : "", sp10 / 10, sp10 % 10,
                        controller::zoneName(c.heaterZone), (int)c.heaterDuty,
                        c.pumpOn ? "on" : "off",
                        c.mode == controller::Mode::Auto ? c.stepName : "");

  // Last touch point
  lv_indev_t* indev = lv_indev_get_next(nullptr);
  if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_label_set_text_fmt(touchLabel, "Touch %d,%d", (int)p.x, (int)p.y);
  }
}

}  // namespace

void update() { refreshAll(); }

void create(lv_obj_t* parent) {
  // Scrolling column inside the screen area; leaving the screen clears all
  // bench-test values so no output stays on.
  lv_obj_t* scr = lv_obj_create(parent);
  lv_obj_remove_style_all(scr);
  lv_obj_set_size(scr, LV_PCT(100), LV_PCT(100));
  lv_obj_add_event_cb(
      scr, [](lv_event_t*) {
        outputs::setTestAllowed(false);
        outputs::setTestAllowed(true);
      },
      LV_EVENT_DELETE, nullptr);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(scr, 2, 0);
  lv_obj_set_style_pad_row(scr, 8, 0);

  // Title + touch position
  lv_obj_t* row = makeRow(scr);
  lv_obj_t* title = lv_label_create(row);
  lv_label_set_text(title, "Bench test (idle only)");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  touchLabel = lv_label_create(row);
  lv_label_set_text(touchLabel, "Touch -,-");

  // Temperatures
  row = makeRow(scr);
  tempLabel = lv_label_create(row);
  lv_obj_set_style_text_font(tempLabel, &lv_font_montserrat_48, 0);
  lv_label_set_text(tempLabel, "--.-");
  lv_obj_t* rescan = lv_button_create(row);
  lv_label_set_text(lv_label_create(rescan), LV_SYMBOL_REFRESH " Scan");
  lv_obj_add_event_cb(rescan, onRescan, LV_EVENT_CLICKED, nullptr);

  // Signal outputs
  row = makeRow(scr);
  for (int i = 0; i < io::kSignalOutputs; i++) {
    lv_obj_t* btn = lv_button_create(row);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text_fmt(label, "OUT%d", i + 1);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, onSignalToggled, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
  }

  // Power outputs
  for (int i = 0; i < io::kPowerOutputs; i++) {
    row = makeRow(scr);
    powerLabels[i] = lv_label_create(row);
    lv_obj_set_width(powerLabels[i], 100);
    lv_label_set_text_fmt(powerLabels[i], "PWR%d 0%%", i + 1);
    lv_obj_t* slider = lv_slider_create(row);
    lv_obj_set_flex_grow(slider, 1);
    lv_slider_set_range(slider, 0, 100);
    lv_obj_add_event_cb(slider, onPowerChanged, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)i);
  }

  // Buzzer + backlight
  row = makeRow(scr);
  lv_obj_t* beep = lv_button_create(row);
  lv_label_set_text(lv_label_create(beep), LV_SYMBOL_BELL " Sound");
  lv_obj_add_event_cb(beep, onBeep, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* bl = lv_slider_create(row);
  lv_obj_set_flex_grow(bl, 1);
  lv_slider_set_range(bl, 10, 255);
  lv_slider_set_value(bl, 200, LV_ANIM_OFF);
  lv_obj_add_event_cb(bl, onBacklight, LV_EVENT_VALUE_CHANGED, nullptr);

  statusLabel = lv_label_create(scr);
  lv_label_set_text(statusLabel, "");
  ctrlLabel = lv_label_create(scr);
  lv_label_set_text(ctrlLabel, "");


}

}  // namespace ui_bringup
