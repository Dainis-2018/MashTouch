// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Home: temperature, active recipe, resume offer and the main menu.

#include <stdio.h>

#include "app/controller.h"
#include "core/settings.h"
#include "kit.h"
#include "screens.h"
#include "ui.h"

namespace ui {
namespace home {
namespace {

lv_obj_t* tempLabel;
lv_obj_t* resumeBox;
lv_obj_t* resumeLabel;
lv_obj_t* brewTile;
lv_obj_t* manualTile;

void refused() { kit::message("Cannot start", controller::lastError()); }

void startBrew() {
  if (controller::status().mode != controller::Mode::Idle) {
    go(Screen::Brew);
    return;
  }
  char title[64];
  snprintf(title, sizeof(title), "Start '%s'", settings::recipe().name.c_str());
  kit::choose(title, {LV_SYMBOL_PLAY " Start now", LV_SYMBOL_BELL " Delayed start..."}, [](int i) {
    if (i == 0) {
      if (!controller::startAuto(0)) refused();
      return;
    }
    kit::editNumber("Start heating in", 60, 15, 24 * 60, 15, 0, "min", [](float min) {
      if (!controller::startAuto((uint16_t)min)) refused();
    });
  });
}

lv_obj_t* tile(lv_obj_t* parent, const char* text, kit::Fn fn) {
  lv_obj_t* b = kit::button(parent, text, fn, kit::kNormal);
  lv_obj_set_size(b, 150, 72);
  return b;
}

}  // namespace

void create(lv_obj_t* root) {
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(root, kit::kPad, 0);
  lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);  // the resume banner may not fit

  // Temperature + recipe
  lv_obj_t* top = kit::row(root);
  lv_obj_t* tc = kit::card(top);
  lv_obj_set_size(tc, 200, 86);
  tempLabel = kit::label(tc, "--.-", kit::kHuge);
  lv_obj_center(tempLabel);

  lv_obj_t* rc = kit::button(top, "", []() { go(Screen::Recipes); });
  lv_obj_set_flex_grow(rc, 1);
  lv_obj_set_height(rc, 86);
  lv_obj_set_style_bg_color(rc, kit::panel(), 0);
  char buf[96];
  snprintf(buf, sizeof(buf), "Recipe\n%s", settings::recipe().name.c_str());
  lv_obj_t* rl = lv_obj_get_child(rc, 0);
  lv_label_set_text(rl, buf);
  lv_label_set_long_mode(rl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(rl, LV_PCT(100));

  // Unfinished brew after power loss
  resumeBox = kit::card(root);
  lv_obj_set_size(resumeBox, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(resumeBox, lv_color_hex(0x4A3B00), 0);
  lv_obj_set_flex_flow(resumeBox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(resumeBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(resumeBox, kit::kPad, 0);
  resumeLabel = kit::label(resumeBox, "", kit::kNormal);
  lv_obj_set_flex_grow(resumeLabel, 1);
  kit::button(resumeBox, "Discard", []() {
    kit::confirm("Forget the unfinished brew?", LV_SYMBOL_TRASH " Discard", []() { controller::discardResume(); });
  });
  lv_obj_t* rb = kit::button(resumeBox, LV_SYMBOL_PLAY " Resume", []() {
    if (!controller::resume()) refused();
  });
  kit::setColor(rb, kit::good());

  // Main menu
  lv_obj_t* grid = kit::box(root);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(grid, kit::kPad, 0);
  brewTile = tile(grid, LV_SYMBOL_PLAY " Brew", startBrew);
  kit::setColor(brewTile, kit::good());
  manualTile = tile(grid, LV_SYMBOL_CHARGE " Manual", []() {
    if (!controller::startManual()) refused();
  });
  tile(grid, LV_SYMBOL_LIST " Recipes", []() { go(Screen::Recipes); });
  tile(grid, LV_SYMBOL_EYE_OPEN " Sensors", []() { go(Screen::Sensors); });
  tile(grid, LV_SYMBOL_SETTINGS " Settings", []() { go(Screen::Settings); });
  tile(grid, LV_SYMBOL_DRIVE " Tools", []() { go(Screen::Tools); });

  update();
}

void update() {
  const controller::Status& st = controller::status();
  char buf[64];
  if (st.tempOk) fmtTemp(buf, sizeof(buf), st.tempC);
  else snprintf(buf, sizeof(buf), "--.-%s", unit());
  lv_label_set_text(tempLabel, buf);

  if (st.resumeOffered && st.mode == controller::Mode::Idle) {
    lv_obj_remove_flag(resumeBox, LV_OBJ_FLAG_HIDDEN);
    if (st.resumePhase == ::brew::Phase::Mash)
      snprintf(buf, sizeof(buf), "Unfinished brew:\n%s, step %d", ::brew::phaseName(st.resumePhase), st.resumeStep + 1);
    else
      snprintf(buf, sizeof(buf), "Unfinished brew:\n%s", ::brew::phaseName(st.resumePhase));
    lv_label_set_text(resumeLabel, buf);
  } else {
    lv_obj_add_flag(resumeBox, LV_OBJ_FLAG_HIDDEN);
  }

  bool running = st.mode != controller::Mode::Idle;
  kit::setText(brewTile, running ? LV_SYMBOL_LEFT " Back to brew" : LV_SYMBOL_PLAY " Brew");
  if (running) lv_obj_add_state(manualTile, LV_STATE_DISABLED);
  else lv_obj_remove_state(manualTile, LV_STATE_DISABLED);
}

}  // namespace home
}  // namespace ui
