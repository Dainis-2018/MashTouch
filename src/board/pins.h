// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Board selection. The build environment defines one MT_BOARD_* flag
// (platformio.ini); this header pulls in that board's pin map and features.
//
//   MT_BOARD_DEVKIT  bench build: ESP32-S3-DevKitC-1 + MSP3526 display
//   MT_BOARD_WS35    product: Waveshare ESP32-S3-Touch-LCD-3.5 + I/O backpack
#pragma once

#if defined(MT_BOARD_WS35)
#include "pins_ws35.h"
#elif defined(MT_BOARD_DEVKIT)
#include "pins_devkit.h"
#else
#error "No board selected: define MT_BOARD_DEVKIT or MT_BOARD_WS35 (see platformio.ini)"
#endif

// Features a board can have (0 = absent unless the board header says so).
#ifndef MT_HAS_IOX_FRONT
#define MT_HAS_IOX_FRONT 0  // front buttons and LED rings on an I2C expander
#endif
#ifndef MT_AUDIO_ES8311
#define MT_AUDIO_ES8311 0  // speaker through an ES8311 codec (needs MCLK and I2C setup)
#endif
#ifndef MT_LCD_RST_EXIO
#define MT_LCD_RST_EXIO 0  // LCD reset through the on-board TCA9554 expander
#endif
#ifndef MT_LCD_BUS_SHARED
#define MT_LCD_BUS_SHARED 0
#endif
