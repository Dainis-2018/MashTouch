// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pin map for the Waveshare ESP32-S3-Touch-LCD-3.5 (V2 PCB, ESP32-S3R8 +
// 16 MB flash) with the MashTouch I/O backpack plugged into its 2x16 2.54 mm
// header J8. Include "board/pins.h", not this file.
//
// Sources: Waveshare V2 schematic (PIN_OUT table, J8) and the vendor examples
// 11_lvgl_arduino_v8 / 01_audio_out / 04_es8311_example. See docs/design.md.
//
// On-board use (not free): 1-3, 5, 6 LCD; 4 TCA9554 INT; 7/8 I2C; 9-11 SD;
// 12-16 ES8311; 19/20 USB; 0 BOOT. J8 brings out 17, 18, 21, 38-42, 45-48
// (the camera pins: free while no camera is plugged in), the SD bus
// 9/10/11, I2C 7/8 and UART0 43/44.
//
// Heater and pump outputs and the level switch are direct GPIOs with gate
// pull-downs on the backpack (off in reset). Only the front buttons and LEDs
// go through the backpack's I2C expander. GPIO 45/46 are strapping pins:
// spare, usable only for outputs with pull-downs.
#pragma once

// Display: ST7796 on its own SPI bus. CS is not wired to the MCU; reset is
// TCA9554 EXIO1. Backlight on a plain GPIO (LEDC PWM).
#define PIN_LCD_SCK     5
#define PIN_LCD_MOSI    1
#define PIN_LCD_MISO    2
#define PIN_LCD_DC      3
#define PIN_LCD_CS     -1
#define PIN_LCD_BL      6
#define MT_LCD_RST_EXIO 1

// Shared I2C: FT6336 touch 0x38, TCA9554 0x20, ES8311, AXP2101, PCF85063,
// QMI8658, and the backpack TCA9535. Owned by src/hal/i2cbus.
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     7
#define PIN_TOUCH_SDA   PIN_I2C_SDA
#define PIN_TOUCH_SCL   PIN_I2C_SCL
#define PIN_IOX_INT     4  // on-board TCA9554 interrupt (unused, polled)

// On-board TCA9554 expander
#define I2C_ADDR_EXIO       0x20
#define EXIO_BIT_CAM_PWDN   0
#define EXIO_BIT_LCD_RST    1
#define EXIO_BIT_TP_INT     2
#define EXIO_BIT_SD_CD      3

// Speaker: ES8311 codec (I2S slave, MCLK from the ESP32) + NS4150B amp.
// The amplifier enable (PA_CTRL) is not on a GPIO in the schematic's pin table;
// it is assumed to default to on (check on hardware).
#define MT_AUDIO_ES8311 1
#define PIN_I2S_MCLK   12
#define PIN_I2S_BCLK   13
#define PIN_I2S_LRCLK  15
#define PIN_I2S_DOUT   16  // ESP32 -> codec (DSDIN)
#define PIN_I2S_DIN    14  // codec -> ESP32 (microphone, unused)

// microSD (SD_MMC 1-bit). Unused: its lines double as the sensor SPI bus.
#define PIN_SD_CLK     11
#define PIN_SD_CMD     10
#define PIN_SD_D0       9

// ---- Backpack (through J8) ----

// MAX31865 PT100/PT1000 converters on the J8 SPI lines (the SD bus).
#define PIN_SPI_SCK    11
#define PIN_SPI_MOSI   10
#define PIN_SPI_MISO    9
#define PIN_MAX31865_CS1 47
#define PIN_MAX31865_CS2 48

// Signal outputs: low-side drivers for SSR inputs and relay coils
#define PIN_OUT1       38
#define PIN_OUT2       39
#define PIN_OUT3       40
#define PIN_OUT4       41

// 12 V power outputs, PWM capable (12 V pump, fan)
#define PIN_PWR1       42
#define PIN_PWR2       21

// Sensors
#define PIN_ONEWIRE    17
#define PIN_LEVEL      18

// Front buttons and LED rings on the backpack TCA9535 (port 0). Buttons pull
// to GND with 10k pull-ups on the backpack; LEDs are driven high = on.
#define MT_HAS_IOX_FRONT 1
#define I2C_ADDR_FRONT      0x22
#define FRONT_BIT_BTN_START 0
#define FRONT_BIT_BTN_PUMP  1
#define FRONT_BIT_LED_START 2
#define FRONT_BIT_LED_PUMP  3

// UART0 debug header (not used by the firmware; logs go over USB)
#define PIN_DEBUG_TX   43
#define PIN_DEBUG_RX   44
