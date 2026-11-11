// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Web server on port 80: the web UI (embedded in the firmware, gzipped)
// and a JSON API on the same controller action layer as the touch screen
// and the front buttons. Served from loop() like everything else, so the
// handlers can use the controller and storage directly. See docs/web-api.md.
#pragma once

namespace web {

void begin();
void loop();

}  // namespace web
