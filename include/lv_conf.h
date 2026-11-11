// MashTouch LVGL configuration. Only overrides are listed here;
// everything else uses the defaults from lv_conf_internal.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Use the C library heap; with PSRAM enabled, large blocks land in PSRAM.
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE
#define LV_DEF_REFR_PERIOD 20

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

// QR codes on the Wi-Fi screen (join the setup access point, open the UI)
#define LV_USE_QRCODE 1

#endif
