// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hardware test screen (Tools > Output and button test): sensor readings,
// touch position and button state, and manual switching of every output
// while the controller is idle.
#pragma once
#include <lvgl.h>

namespace ui_bringup {

void create(lv_obj_t* parent);
void update();

}  // namespace ui_bringup
