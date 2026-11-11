// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screens. create() builds into an empty root (480x280 under the top bar);
// update() runs every 250 ms while the screen is shown. The root is deleted
// when the screen changes, so screens keep no widget pointers beyond that.
#pragma once
#include <lvgl.h>

#define MT_SCREEN(ns)            \
  namespace ns {                 \
  void create(lv_obj_t* root);   \
  void update();                 \
  }

namespace ui {
MT_SCREEN(home)
MT_SCREEN(brew)
MT_SCREEN(recipes)
MT_SCREEN(recipe_edit)
MT_SCREEN(settings_screen)
MT_SCREEN(sensors_screen)
MT_SCREEN(tools)
MT_SCREEN(wifi_screen)
}  // namespace ui

#undef MT_SCREEN
