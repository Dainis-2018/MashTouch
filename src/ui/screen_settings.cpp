// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings: sections on the left, fields on the right. Fields are a table
// of setting paths, read and written through storage::settingsJson() and
// storage::setSetting(), so the same validation applies as on the console.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/settings.h"
#include "hal/display.h"
#include "hal/sensors.h"
#include "kit.h"
#include "screens.h"
#include "storage/storage.h"
#include "ui.h"

namespace ui {
namespace settings_screen {
namespace {

enum class Kind : uint8_t { Number, Temp, Delta, Bool, Choice };

struct Field {
  const char* label;
  const char* path;
  Kind kind;
  float min, max, step;
  int decimals;
  const char* unit;     // Number only
  const char* choices;  // Choice: JSON values separated by '\n'
};

// Field constructors (C++11: keep the tables readable).
Field num(const char* l, const char* p, float mn, float mx, float st, int d, const char* u) {
  return Field{l, p, Kind::Number, mn, mx, st, d, u, nullptr};
}
Field temp(const char* l, const char* p, float mn, float mx) {
  return Field{l, p, Kind::Temp, mn, mx, 0.5f, 1, nullptr, nullptr};
}
Field delta(const char* l, const char* p, float mx) {
  return Field{l, p, Kind::Delta, 0, mx, 0.1f, 1, nullptr, nullptr};
}
Field flag(const char* l, const char* p) { return Field{l, p, Kind::Bool, 0, 0, 0, 0, nullptr, nullptr}; }
Field choice(const char* l, const char* p, const char* c) {
  return Field{l, p, Kind::Choice, 0, 0, 0, 0, nullptr, c};
}

struct Section {
  const char* name;
  std::vector<Field> fields;
};

std::vector<Section> sections() {
  std::vector<Section> s;
  s.push_back({"Heater",
               {num("PID kp (%/°)", "heater.kp", 0, 500, 0.5f, 1, ""),
                num("PID ki (%/°s)", "heater.ki", 0, 5, 0.001f, 3, ""),
                num("PID kd (%s/°)", "heater.kd", 0, 5000, 1, 0, ""),
                num("PID sample time", "heater.sampleMs", 100, 60000, 100, 0, "ms"),
                num("Output window", "heater.windowMs", 500, 60000, 500, 0, "ms"),
                delta("Full power band", "heater.fullPowerBandC", 20),
                delta("Band near the boil", "heater.nearBoilBandC", 10),
                num("Boil power", "heater.boilPowerPct", 0, 100, 5, 0, "%"),
                num("Mash power limit", "heater.mashPowerPct", 10, 100, 5, 0, "%"),
                delta("Boil hysteresis", "heater.boilHystC", 5),
                choice("Control sensor", "heater.controlSensor", "kettle\nrims")}});
  s.push_back({"Pump",
               {num("Rest cycle: run", "pump.cycleMin", 1, 120, 1, 0, "min"),
                num("Rest cycle: rest", "pump.restMin", 0, 60, 1, 0, "min"),
                delta("Rest ends on a drop of", "pump.restRestartDropC", 10),
                temp("Stop temperature", "pump.stopTempC", 70, 110),
                delta("Stop hysteresis", "pump.stopHystC", 10),
                flag("Sensor in the pump outlet", "pump.externalSensor"),
                flag("Pump during the boil", "pump.inBoil"),
                flag("Pump during cooling", "pump.duringCooling"),
                flag("Prime at start", "pump.autoPrime"),
                num("Prime pulses", "pump.primePulses", 1, 20, 1, 0, "")}});
  s.push_back({"Process",
               {temp("Boil temperature", "process.boilTempC", 90, 105),
                delta("Reached tolerance", "process.reachedTolC", 2),
                flag("Skip 'remove malt'", "process.skipRemoveMalt"),
                flag("Heat while removing malt", "process.heatDuringRemoveMalt"),
                flag("Iodine test before mash out", "process.iodineTest"),
                num("Iodine test timeout (0 = wait)", "process.iodineTimeoutMin", 0, 120, 1, 0, "min"),
                choice("Whirlpool", "process.whirlpool", "off\nhot\ncold"),
                num("Whirlpool time", "process.whirlpoolMin", 1, 60, 1, 0, "min"),
                temp("Cooling target", "process.coolingTargetC", 5, 40)}});
  s.push_back({"Sparge",
               {temp("Sparge water", "sparge.tempC", 30, 99),
                delta("Hysteresis", "sparge.hystC", 5),
                flag("Heat with the brew", "sparge.autoWithBrew")}});
  s.push_back({"Fan",
               {num("Run-on after heating", "fan.runOnSec", 0, 3600, 10, 0, "s"),
                temp("Heatsink: fan on", "fan.onTempC", 20, 80),
                temp("Heatsink: full speed", "fan.fullTempC", 20, 90),
                temp("Heatsink: heater cut", "fan.alarmTempC", 40, 110),
                num("Minimum fan speed", "fan.minPct", 0, 100, 5, 0, "%")}});
  s.push_back({"Safety",
               {temp("Heater cut above", "safety.maxTempC", 50, 130),
                delta("Runaway cut (0 = off)", "safety.runawayC", 20),
                choice("Level switch", "safety.level", "ignore\nclosed\nopen")}});
  s.push_back({"Autotune",
               {num("Output high", "autotune.outputHighPct", 10, 100, 5, 0, "%"),
                num("Output low", "autotune.outputLowPct", 0, 90, 5, 0, "%"),
                delta("Hysteresis", "autotune.hysteresisC", 2),
                num("Cycles", "autotune.cycles", 1, 10, 1, 0, ""),
                num("Timeout", "autotune.timeoutMin", 10, 600, 10, 0, "min"),
                flag("Pump on", "autotune.pumpOn")}});
  Section out{"Outputs", {}};
  static const char* names[settings::kOutputChannels] = {"OUT1", "OUT2", "OUT3", "OUT4", "PWR1", "PWR2"};
  static char paths[settings::kOutputChannels][3][24];
  static char labels[settings::kOutputChannels][3][24];
  for (int ch = 0; ch < settings::kOutputChannels; ch++) {
    const char* keys[3] = {"role", "index", "inverted"};
    for (int k = 0; k < 3; k++) {
      snprintf(paths[ch][k], sizeof(paths[ch][k]), "outputs.%d.%s", ch, keys[k]);
      snprintf(labels[ch][k], sizeof(labels[ch][k]), "%s %s", names[ch], k == 0 ? "role" : k == 1 ? "number" : "inverted");
    }
    out.fields.push_back(choice(labels[ch][0], paths[ch][0], "none\nheater\npump\nfan\nsparge\nalarm\ncontactor"));
    out.fields.push_back(num(labels[ch][1], paths[ch][1], 1, 4, 1, 0, ""));  // heater 1..4 = stages
    out.fields.push_back(flag(labels[ch][2], paths[ch][2]));
  }
  s.push_back(out);
  s.push_back({"Display",
               {flag("Fahrenheit", "fahrenheit"),
                num("Brightness", "brightness", 10, 255, 5, 0, ""),
                num("Sound volume", "sound.volume", 0, 100, 5, 0, "%")}});
  s.push_back({"Simulator",
               {num("Clock speed", "sim.speed", 1, 100, 1, 0, "x"),
                num("Water", "sim.liters", 1, 200, 1, 0, "l"),
                num("Heater power", "sim.heaterW", 100, 20000, 100, 0, "W"),
                num("Heat loss", "sim.lossWPerK", 0, 100, 1, 0, "W/K"),
                temp("Ambient", "sim.ambientC", -10, 40),
                temp("Start temperature", "sim.startC", 0, 90),
                num("Sensor lag", "sim.sensorTauS", 0, 120, 1, 0, "s"),
                flag("Drive real outputs", "sim.driveOutputs")}});
  return s;
}

std::vector<Section> table;
int section = 0;
lv_obj_t* sideList;
lv_obj_t* fieldList;

std::string raw(const char* path) { return storage::settingsJson(path, false); }

std::string unquote(const std::string& s) {
  return s.size() >= 2 && s[0] == '"' ? s.substr(1, s.size() - 2) : s;
}

void renderFields();

void apply(const char* path, const std::string& value) {
  std::string err;
  if (!storage::setSetting(path, value.c_str(), err)) kit::message("Not saved", err.c_str());
  sensors::reload();
  if (!strcmp(path, "brightness")) display::setBrightness(settings::current().brightness);
  kit::later([]() {
    if (current() != Screen::Settings) return;  // left the screen meanwhile
    int32_t y = lv_obj_get_scroll_y(fieldList);
    lv_obj_clean(fieldList);
    renderFields();
    lv_obj_update_layout(fieldList);
    lv_obj_scroll_to_y(fieldList, y, LV_ANIM_OFF);
  });
}

void renderField(const Field& f) {
  std::string v = raw(f.path);
  char buf[40];
  switch (f.kind) {
    case Kind::Bool: {
      const char* path = f.path;
      kit::rowSwitch(fieldList, f.label, v == "true", [path](bool on) { apply(path, on ? "true" : "false"); });
      break;
    }
    case Kind::Choice: {
      std::string cur = unquote(v);
      std::string opts = f.choices;
      int sel = 0, idx = 0;
      size_t start = 0;
      std::vector<std::string> values;
      while (start <= opts.size()) {
        size_t end = opts.find('\n', start);
        if (end == std::string::npos) end = opts.size();
        values.push_back(opts.substr(start, end - start));
        if (values.back() == cur) sel = idx;
        idx++;
        start = end + 1;
      }
      const char* path = f.path;
      kit::rowChoice(fieldList, f.label, f.choices, sel, [path, values](int i) { apply(path, values[i]); });
      break;
    }
    default: {
      float c = atof(v.c_str());
      float shown = f.kind == Kind::Temp ? toDisplay(c) : f.kind == Kind::Delta ? deltaToDisplay(c) : c;
      const char* u = f.kind == Kind::Number ? f.unit : unit();
      snprintf(buf, sizeof(buf), "%.*f%s%s", f.decimals, shown, *u ? " " : "", u);
      Field fc = f;
      kit::rowValue(fieldList, f.label, buf, [fc, shown]() {
        float mn = fc.kind == Kind::Temp ? toDisplay(fc.min) : fc.kind == Kind::Delta ? deltaToDisplay(fc.min) : fc.min;
        float mx = fc.kind == Kind::Temp ? toDisplay(fc.max) : fc.kind == Kind::Delta ? deltaToDisplay(fc.max) : fc.max;
        kit::editNumber(fc.label, shown, mn, mx, fc.step, fc.decimals, fc.kind == Kind::Number ? fc.unit : unit(),
                        [fc](float d) {
                          float c2 = fc.kind == Kind::Temp ? fromDisplay(d) : fc.kind == Kind::Delta ? deltaFromDisplay(d) : d;
                          char val[24];
                          snprintf(val, sizeof(val), "%.*f", fc.decimals + 2, c2);
                          if (fc.decimals == 0 && fc.kind == Kind::Number) snprintf(val, sizeof(val), "%d", (int)(c2 + 0.5f));
                          apply(fc.path, val);
                        });
      });
      break;
    }
  }
}

void renderFields() {
  for (const Field& f : table[section].fields) renderField(f);
  if (table[section].fields.empty()) kit::label(fieldList, "Nothing here", kit::kNormal);
  if (!strcmp(table[section].name, "Display")) {
    kit::rowHeader(fieldList, "RESET");
    lv_obj_t* b = kit::button(fieldList, LV_SYMBOL_WARNING " All settings to defaults", []() {
      kit::confirm("Reset all settings to defaults? Recipes are kept.", "Reset", []() {
        std::string err;
        if (!storage::resetSettings(err)) kit::message("Not saved", err.c_str());
        sensors::reload();
        display::setBrightness(settings::current().brightness);
        go(Screen::Settings);
      });
    });
    lv_obj_set_width(b, LV_PCT(100));
    kit::setColor(b, kit::bad());
  }
}

void renderSide() {
  for (size_t i = 0; i < table.size(); i++) {
    int idx = (int)i;
    lv_obj_t* b = kit::button(sideList, table[i].name, [idx]() {
      section = idx;
      go(Screen::Settings);
    });
    lv_obj_set_width(b, LV_PCT(100));
    if (idx != section) lv_obj_set_style_bg_color(b, kit::panel(), 0);
  }
}

}  // namespace

void create(lv_obj_t* root) {
  if (table.empty()) table = sections();
  if (section >= (int)table.size()) section = 0;
  sideList = kit::scrollList(root);
  lv_obj_set_size(sideList, 128, LV_PCT(100));
  fieldList = kit::scrollList(root);
  lv_obj_set_size(fieldList, 468 - 128 - kit::kPad, LV_PCT(100));
  lv_obj_set_pos(fieldList, 128 + kit::kPad, 0);
  renderSide();
  renderFields();
}

void update() {}

}  // namespace settings_screen
}  // namespace ui
