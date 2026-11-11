// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Serial command console (115200 baud, line based) for driving and testing
// the controller before the touch UI exists. Type "help".
#pragma once

namespace console {

void begin();
void loop();

}  // namespace console
