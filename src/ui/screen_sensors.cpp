// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Temperature sensors: live reading, role, calibration offset, name.
// Settings are stored by ROM address and saved at once.

#include <stdio.h>
#include <string.h>

#include <vector>

#include "core/settings.h"
#include "hal/sensors.h"
#include "kit.h"
#include "screens.h"
#include "storage/storage.h"
#include "ui.h"

namespace ui {
namespace sensors_screen {
namespace {

using settings::SensorRole;

std::vector<lv_obj_t*> tempLabels;
const char* kRoles = "none\nkettle\nrims\nheatsink\naux\nsparge";

bool isConfigured(const sensors::Info& s) {
  for (const auto& c : settings::current().sensors)
    if (memcmp(c.rom, s.rom, 8) == 0) return true;
  return false;
}

void saveAndRedraw() {
  std::string err;
  if (!storage::saveSettings(err)) kit::message("Not saved", err.c_str());
  go(Screen::Sensors);
}

void configure(int i, SensorRole role, float offset, const std::string& name) {
  sensors::configure(i, role, offset, name);
  saveAndRedraw();
}

}  // namespace

void create(lv_obj_t* root) {
  tempLabels.clear();
  lv_obj_t* list = kit::scrollList(root);

  lv_obj_t* bar = kit::row(list);
  char buf[64];
  snprintf(buf, sizeof(buf), "%d sensor(s)", sensors::count());
  kit::label(bar, buf, kit::kNormal);
  kit::button(bar, LV_SYMBOL_REFRESH " Rescan bus", []() {
    sensors::rescan();
    go(Screen::Sensors);
  });

  for (int i = 0; i < sensors::count(); i++) {
    const sensors::Info& s = sensors::info(i);
    lv_obj_t* c = kit::card(list);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, kit::kPad, 0);

    lv_obj_t* r1 = kit::row(c);
    std::string title = s.name;
    if (!isConfigured(s) && s.role == SensorRole::Kettle) title += " (auto)";
    lv_obj_t* name = kit::label(r1, title.c_str(), kit::kNormal);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_t* t = kit::label(r1, "", kit::kLarge);
    tempLabels.push_back(t);

    lv_obj_t* r2 = kit::row(c);
    lv_obj_t* dd = lv_dropdown_create(r2);
    lv_obj_set_width(dd, 150);
    lv_obj_set_style_text_font(dd, kit::kNormal, 0);
    lv_dropdown_set_options(dd, kRoles);
    lv_dropdown_set_selected(dd, (uint32_t)s.role);
    kit::on(dd, LV_EVENT_VALUE_CHANGED, [i, dd]() {
      const sensors::Info& si = sensors::info(i);
      configure(i, (SensorRole)lv_dropdown_get_selected(dd), si.offsetC, si.name);
    });

    char off[24];
    snprintf(off, sizeof(off), "%+.1f%s", deltaToDisplay(s.offsetC), unit());
    kit::button(r2, off, [i]() {
      const sensors::Info& si = sensors::info(i);
      kit::editNumber("Calibration offset", deltaToDisplay(si.offsetC), deltaToDisplay(-10), deltaToDisplay(10),
                      0.1f, 1, unit(), [i](float v) {
                        const sensors::Info& sj = sensors::info(i);
                        configure(i, sj.role, deltaFromDisplay(v), sj.name);
                      });
    });
    kit::button(r2, LV_SYMBOL_EDIT, [i]() {
      kit::editText("Sensor name", sensors::info(i).name, [i](const std::string& n) {
        const sensors::Info& sj = sensors::info(i);
        configure(i, sj.role, sj.offsetC, n);
      });
    });
    if (isConfigured(s)) {
      kit::button(r2, LV_SYMBOL_TRASH, [i]() {
        kit::confirm("Forget this sensor's settings?", LV_SYMBOL_TRASH " Forget", [i]() {
          sensors::forget(i);
          saveAndRedraw();
        });
      });
    }
  }
  if (sensors::count() == 0)
    kit::label(list, "No sensors found. Check the 1-Wire wiring and the 4.7k pull-up, then rescan.", kit::kNormal);
  update();
}

void update() {
  for (size_t i = 0; i < tempLabels.size(); i++) {
    const sensors::Info& s = sensors::info((int)i);
    char buf[24];
    if (s.busIndex < 0) snprintf(buf, sizeof(buf), "missing");
    else if (s.ok) fmtTemp(buf, sizeof(buf), s.tempC, 2);
    else snprintf(buf, sizeof(buf), "fault");
    lv_label_set_text(tempLabels[i], buf);
    lv_obj_set_style_text_color(tempLabels[i], s.ok ? lv_color_white() : kit::bad(), 0);
  }
}

}  // namespace sensors_screen
}  // namespace ui
