// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pin map for the bench build: ESP32-S3-DevKitC-1 N16R8 (WROOM-1) + MSP3526
// display module on a breadboard. Include "board/pins.h", not this file.
//
// Not usable: GPIO 19/20 (USB), 26-32 (flash), 35-37 (octal PSRAM).
// Strapping pins 0, 45, 46 are left alone. GPIO 3 is a strapping pin only
// for the JTAG source when an eFuse is burnt; it drives a MOSFET gate here,
// which has a pull-down, so it reads low at reset.
// GPIO 43/44 are UART0 (boot log on 43 at every reset): kept free as a
// debug header, never used for an output that must stay quiet at boot.
//
// Every output stage has a gate pull-down on the PCB, so heater, pump and
// the other outputs stay off while the MCU is in reset or being flashed.
#pragma once

// Display: MSP3526 (ST7796S over SPI). MAX31865 and the SD slot share the bus.
// The display RESET and the touch reset (CTP_RST) are tied to the MCU reset
// line (EN), not to a GPIO: the panel resets together with the ESP32.
#define PIN_SPI_SCK    12
#define PIN_SPI_MOSI   11
#define PIN_SPI_MISO   13
#define PIN_LCD_CS     10
#define PIN_LCD_DC      9
#define PIN_LCD_BL      7
#define PIN_SD_CS      14

// The LCD and the MAX31865 converters share this one bus on this board.
#define PIN_LCD_SCK    PIN_SPI_SCK
#define PIN_LCD_MOSI   PIN_SPI_MOSI
#define PIN_LCD_MISO   PIN_SPI_MISO
#define MT_LCD_BUS_SHARED 1

// Touch: FT6336U over I2C, polled (its INT line is not connected).
#define PIN_TOUCH_SDA   5
#define PIN_TOUCH_SCL   6

// PT100/PT1000 converters
#define PIN_MAX31865_CS1 39
#define PIN_MAX31865_CS2 40

// Front panel buttons (to GND, internal pull-up) and their LED rings
#define PIN_BTN_START   1
#define PIN_BTN_PUMP    2
#define PIN_LED_START  42
#define PIN_LED_PUMP   41

// Signal outputs: low-side drivers for SSR inputs and relay coils
#define PIN_OUT1       16
#define PIN_OUT2       17
#define PIN_OUT3       18
#define PIN_OUT4       21

// 12 V power outputs, PWM capable (12 V pump, fan)
#define PIN_PWR1       15
#define PIN_PWR2        3

// Speaker: I2S class-D amplifier (MAX98357A) with a small 8 ohm speaker
#define PIN_I2S_BCLK    4
#define PIN_I2S_LRCLK   8
#define PIN_I2S_DOUT   38

// Sensors
#define PIN_ONEWIRE    47
#define PIN_LEVEL      48

// UART0 debug header (not used by the firmware; logs go over USB)
#define PIN_DEBUG_TX   43
#define PIN_DEBUG_RX   44
