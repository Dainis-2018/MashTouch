// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display.h"

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board/pins.h"
#include "i2cbus.h"
#include "iox.h"

// IPS panels (MSP3526, Waveshare 3.5) need colour inversion. Override with
// -DMT_LCD_INVERT=0 if colours come out negative on a particular module,
// -DMT_LCD_BGR=1 if red and blue are swapped, and -DMT_LCD_ROTATION=3 if the
// picture is upside down (1 and 3 are the two landscape orientations).
#ifndef MT_LCD_INVERT
#define MT_LCD_INVERT 1
#endif
#ifndef MT_LCD_BGR
#define MT_LCD_BGR 0
#endif
#ifndef MT_LCD_ROTATION
#define MT_LCD_ROTATION 1
#endif

namespace {

// ST7796 panel + FT6336 touch: the same chips on both boards, only the pins
// and the reset differ.
class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_FT5x06 touch_;  // FT6336U is register-compatible

 public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = PIN_LCD_MISO;
      cfg.pin_dc = PIN_LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = PIN_LCD_CS;
      // No reset GPIO: tied to the MCU reset line (MSP3526) or pulsed through the
      // I2C expander before init (Waveshare). LovyanGFX also sends a software reset.
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = MT_LCD_INVERT;
      cfg.rgb_order = MT_LCD_BGR;
      cfg.dlen_16bit = false;
      cfg.bus_shared = MT_LCD_BUS_SHARED;  // MSP3526: SD card and MAX31865 share the bus
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = -1;  // polled
      cfg.pin_rst = -1;  // reset follows the LCD reset
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.i2c_port = 0;  // Waveshare: the shared bus, see hal/i2cbus
      cfg.i2c_addr = 0x38;
      cfg.pin_sda = PIN_TOUCH_SDA;
      cfg.pin_scl = PIN_TOUCH_SCL;
      cfg.freq = 400000;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};

Panel lcd;

constexpr int kBufLines = 32;

uint32_t tickMs() { return millis(); }

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  // LVGL renders little-endian RGB565; the panel wants big-endian.
  lcd.writePixels(reinterpret_cast<uint16_t*>(px), w * h, true);
  lcd.endWrite();
  lv_display_flush_ready(disp);
}

void touchCb(lv_indev_t*, lv_indev_data_t* data) {
  uint16_t x, y;
  if (lcd.getTouch(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

}  // namespace

namespace display {

bool begin() {
#if MT_LCD_RST_EXIO
  i2cbus::begin();
  iox::resetLcd();
#endif
  if (!lcd.init()) return false;
  lcd.setRotation(MT_LCD_ROTATION);  // landscape (MSP3526: header pins on the left)
  lcd.setBrightness(200);
  lcd.fillScreen(TFT_BLACK);

  lv_init();
  lv_tick_set_cb(tickMs);

  lv_display_t* disp = lv_display_create(WIDTH, HEIGHT);
  const size_t bufBytes = WIDTH * kBufLines * sizeof(uint16_t);
  void* buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  void* buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!buf1 || !buf2) return false;
  lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  return true;
}

void loop() { lv_timer_handler(); }

void setBrightness(uint8_t level) { lcd.setBrightness(level); }

}  // namespace display
