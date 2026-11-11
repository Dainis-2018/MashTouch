// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "app/controller.h"
#include "bringup.h"
#include "core/settings.h"
#include "hal/display.h"
#include "kit.h"
#include "net/wifi.h"
#include "screens.h"

namespace ui {
namespace {

constexpr int kTopBarH = 40;
constexpr uint32_t kRefreshMs = 250;
constexpr uint32_t kSampleMs = 5000;

lv_obj_t* titleLabel;
lv_obj_t* statusLabel;
lv_obj_t* faultIcon;
lv_obj_t* simIcon;
lv_obj_t* heatIcon;
lv_obj_t* pumpIcon;
lv_obj_t* wifiIcon;
lv_obj_t* content;
lv_obj_t* root = nullptr;

Screen cur = Screen::Home;
Screen pending = Screen::Home;
bool goQueued = false;
controller::Mode lastMode = controller::Mode::Idle;

int16_t temps[kHistory];
int16_t setpoints[kHistory];
uint32_t samples = 0;
uint32_t lastSample = 0;

void build(Screen s) {
  kit::closePopups();
  if (root) lv_obj_delete(root);
  root = kit::box(content);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  cur = s;
  switch (s) {
    case Screen::Home: setTitle("MashTouch"); home::create(root); break;
    case Screen::Brew: setTitle("Brew"); brew::create(root); break;
    case Screen::Recipes: setTitle("Recipes"); recipes::create(root); break;
    case Screen::RecipeEdit: setTitle("Edit recipe"); recipe_edit::create(root); break;
    case Screen::Settings: setTitle("Settings"); settings_screen::create(root); break;
    case Screen::Sensors: setTitle("Sensors"); sensors_screen::create(root); break;
    case Screen::Tools: setTitle("Tools"); tools::create(root); break;
    case Screen::Bringup: setTitle("Output test"); ui_bringup::create(root); break;
    case Screen::Wifi: setTitle("Wi-Fi"); wifi_screen::create(root); break;
  }
}

void updateScreen() {
  switch (cur) {
    case Screen::Home: home::update(); break;
    case Screen::Brew: brew::update(); break;
    case Screen::Recipes: recipes::update(); break;
    case Screen::RecipeEdit: recipe_edit::update(); break;
    case Screen::Settings: settings_screen::update(); break;
    case Screen::Sensors: sensors_screen::update(); break;
    case Screen::Tools: tools::update(); break;
    case Screen::Bringup: ui_bringup::update(); break;
    case Screen::Wifi: wifi_screen::update(); break;
  }
}

void sample(const controller::Status& st) {
  int i = samples % kHistory;
  temps[i] = st.tempOk ? (int16_t)(toDisplay(st.tempC) * 10) : INT16_MIN;
  setpoints[i] = st.mode != controller::Mode::Idle ? (int16_t)(toDisplay(st.setpointC) * 10) : INT16_MIN;
  samples++;
}

void updateTopBar(const controller::Status& st) {
  lv_label_set_text(faultIcon, st.faults ? LV_SYMBOL_WARNING : "");
  lv_label_set_text(simIcon, st.sim ? "SIM" : "");
  lv_label_set_text(heatIcon, st.heaterOn ? LV_SYMBOL_CHARGE : "");
  lv_label_set_text(pumpIcon, st.pumpOn ? LV_SYMBOL_REFRESH : "");
  wifi::Status w = wifi::status();
  lv_label_set_text(wifiIcon, w.connected || w.apOn ? LV_SYMBOL_WIFI : "");
  lv_obj_set_style_text_color(wifiIcon, w.connected ? lv_color_white() : kit::warn(), 0);
  char t[16];
  if (st.tempOk) fmtTemp(t, sizeof(t), st.tempC);
  else snprintf(t, sizeof(t), "--.-%s", unit());
  lv_label_set_text(statusLabel, t);
}

lv_obj_t* icon(lv_obj_t* bar, lv_color_t c) {
  lv_obj_t* l = kit::label(bar, "", kit::kNormal);
  lv_obj_set_style_text_color(l, c, 0);
  return l;
}

void tick(lv_timer_t*) {
  const controller::Status& st = controller::status();
  if (st.mode != lastMode) {
    if (lastMode == controller::Mode::Idle) go(Screen::Brew);
    else if (st.mode == controller::Mode::Idle && cur == Screen::Brew) go(Screen::Home);
    lastMode = st.mode;
  }
  uint32_t now = millis();
  if (samples == 0 || now - lastSample >= kSampleMs) {
    lastSample = now;
    sample(st);
  }
  updateTopBar(st);
  if (!goQueued) updateScreen();
}

void onEvent(const events::Event& e) {
  using events::Type;
  char buf[96];
  switch (e.type) {
    case Type::Addition: {
      const auto& adds = settings::recipe().additions;
      if (e.arg < 0 || e.arg >= (int)adds.size()) break;
      const auto& a = adds[e.arg];
      if (a.amount > 0) snprintf(buf, sizeof(buf), "%s\n%g %s", a.name.c_str(), a.amount, a.unit.c_str());
      else snprintf(buf, sizeof(buf), "%s", a.name.c_str());
      kit::message("Add now", buf);
      break;
    }
    case Type::TimerDone: kit::message("Timer", "The countdown has finished."); break;
    case Type::TempReached: kit::toast("Temperature reached"); break;
    case Type::BoilSoon: kit::toast(LV_SYMBOL_WARNING " Close to the boil: watch for a boil-over"); break;
    case Type::BoilStalled: {
      char t[16];
      fmtTemp(t, sizeof(t), controller::status().tempC);
      snprintf(buf, sizeof(buf), "The temperature stopped rising at %s.\nIs it boiling? Start the boil timer?", t);
      kit::confirm(buf, LV_SYMBOL_PLAY " Start timer", []() { controller::startTimerNow(); });
      break;
    }
    case Type::AutotuneDone: {
      const auto& h = settings::current().heater;
      snprintf(buf, sizeof(buf), "New PID gains (saved):\nkp %.2f  ki %.4f  kd %.1f", h.kp, h.ki, h.kd);
      kit::message("Autotune done", buf);
      break;
    }
    case Type::AutotuneFailed: kit::message("Autotune", "Autotune did not finish (timeout or fault)."); break;
    case Type::SensorFault: kit::toast(LV_SYMBOL_WARNING " Sensor fault: heater and pump off"); break;
    case Type::SafetyCut: kit::toast(LV_SYMBOL_WARNING " Safety cut: heater off"); break;
    default: break;
  }
}

}  // namespace

void begin() {
  for (int i = 0; i < kHistory; i++) temps[i] = setpoints[i] = INT16_MIN;
  display::setBrightness(settings::current().brightness);

  lv_indev_t* indev = lv_indev_get_next(nullptr);
  if (indev) lv_indev_set_long_press_time(indev, 800);  // hold-to-confirm

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x111418), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* bar = kit::box(scr);
  lv_obj_set_size(bar, LV_PCT(100), kTopBarH);
  lv_obj_set_style_bg_color(bar, kit::panel(), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bar, 8, 0);

  lv_obj_t* home = kit::button(bar, LV_SYMBOL_HOME, []() { go(Screen::Home); });
  lv_obj_set_size(home, 56, kTopBarH);
  lv_obj_set_style_radius(home, 0, 0);
  titleLabel = kit::label(bar, "", kit::kNormal);
  lv_obj_set_flex_grow(titleLabel, 1);
  faultIcon = icon(bar, kit::bad());
  simIcon = icon(bar, kit::warn());
  heatIcon = icon(bar, kit::heat());
  pumpIcon = icon(bar, kit::pump());
  wifiIcon = icon(bar, lv_color_white());
  lv_obj_add_flag(wifiIcon, LV_OBJ_FLAG_CLICKABLE);
  kit::onClick(wifiIcon, []() { go(Screen::Wifi); });
  statusLabel = kit::label(bar, "", kit::kNormal);
  lv_obj_set_style_pad_right(statusLabel, 8, 0);

  content = kit::box(scr);
  lv_obj_set_size(content, display::WIDTH, display::HEIGHT - kTopBarH);
  lv_obj_set_pos(content, 0, kTopBarH);
  lv_obj_set_style_pad_all(content, kit::kPad, 0);

  controller::addEventListener(onEvent);
  build(controller::status().mode == controller::Mode::Idle ? Screen::Home : Screen::Brew);
  lv_timer_create(tick, kRefreshMs, nullptr);
}

void go(Screen s) {
  pending = s;
  if (goQueued) return;
  goQueued = true;
  kit::later([]() {
    goQueued = false;
    build(pending);
  });
}

Screen current() { return cur; }

void setTitle(const char* text) { lv_label_set_text(titleLabel, text); }

bool fahrenheit() { return settings::current().fahrenheit; }
const char* unit() { return fahrenheit() ? "\xC2\xB0" "F" : "\xC2\xB0" "C"; }
float toDisplay(float c) { return fahrenheit() ? c * 1.8f + 32 : c; }
float fromDisplay(float d) { return fahrenheit() ? (d - 32) / 1.8f : d; }
float deltaToDisplay(float c) { return fahrenheit() ? c * 1.8f : c; }
float deltaFromDisplay(float d) { return fahrenheit() ? d / 1.8f : d; }

void fmtTemp(char* buf, size_t n, float c, int decimals) {
  snprintf(buf, n, "%.*f%s", decimals, toDisplay(c), unit());
}

void fmtTime(char* buf, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s >= 3600) snprintf(buf, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
  else snprintf(buf, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

namespace {
// i = kHistory - 1 is the newest sample; older ones to the left.
int16_t historyAt(const int16_t* buf, int i) {
  uint32_t age = (uint32_t)(kHistory - 1 - i);
  if (i < 0 || i >= kHistory || age >= samples) return INT16_MIN;
  return buf[(samples - 1 - age) % kHistory];
}
}  // namespace

int16_t historyTemp(int i) { return historyAt(temps, i); }
int16_t historySetpoint(int i) { return historyAt(setpoints, i); }

uint32_t historySamples() { return samples; }

}  // namespace ui
