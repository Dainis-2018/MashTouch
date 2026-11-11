// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "schema.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <vector>

namespace storage {
namespace {

using namespace settings;

// ---- Enum names ----------------------------------------------------------

template <class E>
struct Name {
  E value;
  const char* name;
};

const Name<OutputRole> kOutputRoles[] = {
    {OutputRole::None, "none"},     {OutputRole::Heater, "heater"}, {OutputRole::Pump, "pump"},
    {OutputRole::Fan, "fan"},       {OutputRole::Sparge, "sparge"}, {OutputRole::Alarm, "alarm"},
    {OutputRole::Contactor, "contactor"},
};
const Name<SensorRole> kSensorRoles[] = {
    {SensorRole::None, "none"},         {SensorRole::Kettle, "kettle"}, {SensorRole::Rims, "rims"},
    {SensorRole::Heatsink, "heatsink"}, {SensorRole::Aux, "aux"},     {SensorRole::Sparge, "sparge"},
};
const Name<PumpMode> kPumpModes[] = {
    {PumpMode::Off, "off"}, {PumpMode::On, "on"}, {PumpMode::Cycle, "cycle"},
};
const Name<Whirlpool> kWhirlpool[] = {
    {Whirlpool::Off, "off"}, {Whirlpool::Hot, "hot"}, {Whirlpool::Cold, "cold"},
};
const Name<LevelMode> kLevelModes[] = {
    {LevelMode::Ignore, "ignore"},
    {LevelMode::RequireClosed, "closed"},
    {LevelMode::RequireOpen, "open"},
};

// ---- Visitors: one field list, two directions -----------------------------

struct Writer {
  JsonObject o;

  template <class T>
  void field(const char* k, T& v) {
    o[k] = v;
  }
  void field(const char* k, float& v) {
    // Round to 6 significant digits so files read 0.05, not 0.0500000007.
    char buf[24];
    snprintf(buf, sizeof(buf), "%.6g", v);
    o[k] = strtod(buf, nullptr);
  }
  void field(const char* k, std::string& v) { o[k] = v.c_str(); }

  template <class E, size_t N>
  void enumField(const char* k, E& v, const Name<E> (&table)[N]) {
    for (size_t i = 0; i < N; i++)
      if (table[i].value == v) o[k] = table[i].name;
  }

  void rom(const char* k, uint8_t (&rom)[8]) {
    char hex[17];
    for (int i = 0; i < 8; i++) snprintf(hex + i * 2, 3, "%02X", rom[i]);
    o[k] = hex;
  }

  Writer child(const char* k) { return Writer{o[k].to<JsonObject>()}; }

  template <class T>
  void list(const char* k, std::vector<T>& vec, void (*fn)(Writer&, T&)) {
    JsonArray a = o[k].to<JsonArray>();
    for (auto& item : vec) {
      Writer w{a.add<JsonObject>()};
      fn(w, item);
    }
  }

  template <class T, size_t N>
  void array(const char* k, T (&arr)[N], void (*fn)(Writer&, T&)) {
    JsonArray a = o[k].to<JsonArray>();
    for (size_t i = 0; i < N; i++) {
      Writer w{a.add<JsonObject>()};
      fn(w, arr[i]);
    }
  }
};

struct Reader {
  JsonObjectConst o;

  template <class T>
  void field(const char* k, T& v) {
    JsonVariantConst x = o[k];
    if (x.is<T>()) v = x.as<T>();  // also range-checks integer types
  }
  void field(const char* k, std::string& v) {
    JsonVariantConst x = o[k];
    if (x.is<const char*>()) v = x.as<const char*>();
  }

  template <class E, size_t N>
  void enumField(const char* k, E& v, const Name<E> (&table)[N]) {
    const char* s = o[k] | (const char*)nullptr;
    if (!s) return;
    for (size_t i = 0; i < N; i++)
      if (!strcmp(table[i].name, s)) v = table[i].value;
  }

  void rom(const char* k, uint8_t (&rom)[8]) {
    const char* s = o[k] | "";
    if (strlen(s) != 16) return;
    for (int i = 0; i < 8; i++) {
      char byte[3] = {s[i * 2], s[i * 2 + 1], 0};
      rom[i] = (uint8_t)strtoul(byte, nullptr, 16);
    }
  }

  Reader child(const char* k) { return Reader{o[k].as<JsonObjectConst>()}; }

  template <class T>
  void list(const char* k, std::vector<T>& vec, void (*fn)(Reader&, T&)) {
    if (!o[k].is<JsonArrayConst>()) return;
    vec.clear();
    for (JsonVariantConst e : o[k].as<JsonArrayConst>()) {
      T item;
      Reader r{e.as<JsonObjectConst>()};
      fn(r, item);
      vec.push_back(item);
    }
  }

  template <class T, size_t N>
  void array(const char* k, T (&arr)[N], void (*fn)(Reader&, T&)) {
    if (!o[k].is<JsonArrayConst>()) return;
    JsonArrayConst a = o[k].as<JsonArrayConst>();
    for (size_t i = 0; i < N && i < a.size(); i++) {
      Reader r{a[i].as<JsonObjectConst>()};
      fn(r, arr[i]);
    }
  }
};

// ---- Settings schema -----------------------------------------------------

template <class V>
void visitOutput(V& v, OutputConfig& o) {
  v.enumField("role", o.role, kOutputRoles);
  v.field("index", o.index);
  v.field("inverted", o.inverted);
}

template <class V>
void visitSensor(V& v, SensorConfig& s) {
  v.rom("rom", s.rom);
  v.enumField("role", s.role, kSensorRoles);
  v.field("offsetC", s.offsetC);
  v.field("name", s.name);
}

template <class V>
void visitHeater(V& v, settings::Heater& h) {
  v.field("kp", h.kp);
  v.field("ki", h.ki);
  v.field("kd", h.kd);
  v.field("sampleMs", h.sampleMs);
  v.field("windowMs", h.windowMs);
  v.field("fullPowerBandC", h.fullPowerBandC);
  v.field("nearBoilBandC", h.nearBoilBandC);
  v.field("boilPowerPct", h.boilPowerPct);
  v.field("boilHystC", h.boilHystC);
  v.field("mashPowerPct", h.mashPowerPct);
  v.enumField("controlSensor", h.controlSensor, kSensorRoles);
}

template <class V>
void visitAutotune(V& v, settings::Autotune& a) {
  v.field("outputHighPct", a.outputHighPct);
  v.field("outputLowPct", a.outputLowPct);
  v.field("hysteresisC", a.hysteresisC);
  v.field("cycles", a.cycles);
  v.field("timeoutMin", a.timeoutMin);
  v.field("pumpOn", a.pumpOn);
}

template <class V>
void visitPump(V& v, settings::Pump& p) {
  v.field("cycleMin", p.cycleMin);
  v.field("restMin", p.restMin);
  v.field("restRestartDropC", p.restRestartDropC);
  v.field("stopTempC", p.stopTempC);
  v.field("stopHystC", p.stopHystC);
  v.field("externalSensor", p.externalSensor);
  v.field("inBoil", p.inBoil);
  v.field("duringCooling", p.duringCooling);
  v.field("autoPrime", p.autoPrime);
  v.field("primePulses", p.primePulses);
}

template <class V>
void visitProcess(V& v, settings::Process& p) {
  v.field("boilTempC", p.boilTempC);
  v.field("reachedTolC", p.reachedTolC);
  v.field("skipRemoveMalt", p.skipRemoveMalt);
  v.field("heatDuringRemoveMalt", p.heatDuringRemoveMalt);
  v.field("iodineTest", p.iodineTest);
  v.field("iodineTimeoutMin", p.iodineTimeoutMin);
  v.enumField("whirlpool", p.whirlpool, kWhirlpool);
  v.field("whirlpoolMin", p.whirlpoolMin);
  v.field("coolingTargetC", p.coolingTargetC);
  v.field("boilWarnC", p.boilWarnC);
}

template <class V>
void visitFan(V& v, settings::Fan& f) {
  v.field("runOnSec", f.runOnSec);
  v.field("onTempC", f.onTempC);
  v.field("fullTempC", f.fullTempC);
  v.field("alarmTempC", f.alarmTempC);
  v.field("minPct", f.minPct);
}

template <class V>
void visitSafety(V& v, settings::Safety& s) {
  v.field("maxTempC", s.maxTempC);
  v.enumField("level", s.level, kLevelModes);
  v.field("runawayC", s.runawayC);
}

template <class V>
void visitSparge(V& v, settings::Sparge& s) {
  v.field("tempC", s.tempC);
  v.field("hystC", s.hystC);
  v.field("autoWithBrew", s.autoWithBrew);
}

template <class V>
void visitSound(V& v, settings::Sound& s) {
  v.field("volume", s.volume);
}

template <class V>
void visitNet(V& v, settings::Net& n) {
  v.field("ssid", n.ssid);
  v.field("password", n.password);
  v.field("hostname", n.hostname);
  v.field("apPassword", n.apPassword);
  v.field("webPassword", n.webPassword);
  v.field("timezone", n.timezone);
}

template <class V>
void visitSim(V& v, settings::Sim& s) {
  v.field("speed", s.speed);
  v.field("liters", s.liters);
  v.field("heaterW", s.heaterW);
  v.field("lossWPerK", s.lossWPerK);
  v.field("ambientC", s.ambientC);
  v.field("startC", s.startC);
  v.field("sensorTauS", s.sensorTauS);
  v.field("driveOutputs", s.driveOutputs);
}

template <class V>
void visitSettings(V& v, Settings& s) {
  V c = v.child("heater");
  visitHeater(c, s.heater);
  c = v.child("autotune");
  visitAutotune(c, s.autotune);
  c = v.child("pump");
  visitPump(c, s.pump);
  c = v.child("process");
  visitProcess(c, s.process);
  c = v.child("fan");
  visitFan(c, s.fan);
  c = v.child("safety");
  visitSafety(c, s.safety);
  c = v.child("sparge");
  visitSparge(c, s.sparge);
  c = v.child("sound");
  visitSound(c, s.sound);
  c = v.child("sim");
  visitSim(c, s.sim);
  c = v.child("net");
  visitNet(c, s.net);
  v.array("outputs", s.outputs, &visitOutput<V>);
  v.list("sensors", s.sensors, &visitSensor<V>);
  v.field("fahrenheit", s.fahrenheit);
  v.field("brightness", s.brightness);
  v.field("activeRecipe", s.activeRecipe);
}

// ---- Recipe schema -------------------------------------------------------

template <class V>
void visitStep(V& v, MashStep& s) {
  v.field("name", s.name);
  v.field("tempC", s.tempC);
  v.field("minutes", s.minutes);
  v.enumField("pump", s.pump, kPumpModes);
}

template <class V>
void visitAddition(V& v, Addition& a) {
  v.field("name", a.name);
  v.field("atMinutes", a.atMinutes);
  v.field("amount", a.amount);
  v.field("unit", a.unit);
}

template <class V>
void visitRecipe(V& v, Recipe& r) {
  v.field("name", r.name);
  v.field("mash", r.mash);
  V c = v.child("mashIn");
  visitStep(c, r.mashIn);
  v.list("rests", r.rests, &visitStep<V>);
  c = v.child("mashOut");
  visitStep(c, r.mashOut);
  v.field("spargeTempC", r.spargeTempC);
  v.field("boilMinutes", r.boilMinutes);
  v.list("additions", r.additions, &visitAddition<V>);
}

std::vector<std::string> splitPath(const char* path) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* p = path; *p; p++) {
    if (*p == '.') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += *p;
    }
  }
  out.push_back(cur);
  return out;
}

bool sameKind(JsonVariantConst a, JsonVariantConst b) {
  if (a.is<bool>()) return b.is<bool>();
  if (a.is<float>()) return b.is<float>() && !b.is<bool>();
  if (a.is<const char*>()) return b.is<const char*>();
  if (a.is<JsonArrayConst>()) return b.is<JsonArrayConst>();
  if (a.is<JsonObjectConst>()) return b.is<JsonObjectConst>();
  return true;
}

}  // namespace

void toJson(const Settings& s, JsonObject out) {
  out["version"] = kSettingsVersion;
  Writer w{out};
  visitSettings(w, const_cast<Settings&>(s));  // the writer only reads
}

void fromJson(JsonObjectConst in, Settings& s) {
  Reader r{in};
  visitSettings(r, s);
}

void toJson(const Recipe& rec, JsonObject out) {
  out["version"] = kRecipeVersion;
  Writer w{out};
  visitRecipe(w, const_cast<Recipe&>(rec));
}

void fromJson(JsonObjectConst in, Recipe& rec) {
  Reader r{in};
  visitRecipe(r, rec);
  int version = in["version"] | 1;
  if (version >= 3) return;

  // Versions 1 and 2 had one plain "steps" list; version 2 marked the mash
  // out with a flag, version 1 also had a separate mash-in temperature.
  std::vector<MashStep> steps;
  int mashOutIndex = -1;
  float mashIn = in["mashInTempC"] | 0.0f;
  if (version < 2 && mashIn > 0) {
    MashStep s;
    s.name = "Mash in";
    s.tempC = mashIn;
    r.enumField("mashInPump", s.pump, kPumpModes);
    steps.push_back(s);
  }
  if (in["steps"].is<JsonArrayConst>()) {
    for (JsonVariantConst e : in["steps"].as<JsonArrayConst>()) {
      MashStep s;
      Reader sr{e.as<JsonObjectConst>()};
      visitStep(sr, s);
      if (e["mashOut"] | false) mashOutIndex = (int)steps.size();
      steps.push_back(s);
    }
  }
  arrangeMash(rec, steps, mashOutIndex);
}

void toJson(const brew::Snapshot& s, JsonObject out) {
  out["phase"] = (uint8_t)s.phase;
  out["step"] = s.step;
  out["elapsedMs"] = s.elapsedMs;
  out["additionsFired"] = s.additionsFired;
}

void fromJson(JsonObjectConst in, brew::Snapshot& s) {
  uint8_t phase = in["phase"] | (uint8_t)0;
  if (phase > (uint8_t)brew::Phase::Finished) phase = 0;
  s.phase = (brew::Phase)phase;
  s.step = in["step"] | (int8_t)0;
  s.elapsedMs = in["elapsedMs"] | (uint32_t)0;
  s.additionsFired = in["additionsFired"] | (uint32_t)0;
}

bool validate(Recipe& r, std::string& err) {
  if (r.name.empty()) r.name = "Recipe";
  if (r.mash) {
    // mash in, rests..., mash out
    std::vector<MashStep*> all;
    all.push_back(&r.mashIn);
    for (auto& s : r.rests) all.push_back(&s);
    all.push_back(&r.mashOut);
    for (size_t i = 0; i < all.size(); i++) {
      MashStep& s = *all[i];
      std::string which = i == 0 ? "mash in" : i + 1 == all.size() ? "mash out" : "rest " + std::to_string(i);
      if (s.tempC < 1 || s.tempC > 100) {
        err = which + ": temperature must be 1..100 C";
        return false;
      }
      if (s.minutes > 24 * 60) {
        err = which + ": more than 24 h";
        return false;
      }
      if (s.name.empty()) s.name = i == 0 ? "Mash in" : i + 1 == all.size() ? "Mash out" : "Rest " + std::to_string(i);
    }
  }
  if (r.spargeTempC < 0) r.spargeTempC = 0;
  if (r.spargeTempC > 100) {
    err = "sparge temperature above 100 C";
    return false;
  }
  if (r.boilMinutes > 600) {
    err = "boil longer than 10 h";
    return false;
  }
  if (r.additions.size() > 32) {
    err = "more than 32 additions";
    return false;
  }
  return true;
}

bool setPath(JsonDocument& doc, const char* path, const char* value, std::string& err) {
  std::vector<std::string> keys = splitPath(path);
  JsonVariant cur = doc.as<JsonVariant>();
  for (const std::string& k : keys) {
    JsonVariant next;
    if (cur.is<JsonArray>()) {
      char* end;
      long idx = strtol(k.c_str(), &end, 10);
      if (k.empty() || *end || idx < 0 || idx >= (long)cur.size()) {
        err = "no index '" + k + "'";
        return false;
      }
      next = cur[(size_t)idx];
    } else if (cur.is<JsonObject>()) {
      next = cur[k.c_str()];
      if (next.isNull()) {
        err = "no key '" + k + "'";
        return false;
      }
    } else {
      err = "'" + k + "' is past a value";
      return false;
    }
    cur = next;
  }

  JsonDocument val;
  if (deserializeJson(val, value)) val.set(value);  // bare word: a string
  if (!sameKind(cur, val.as<JsonVariantConst>())) {
    err = "wrong type for this setting";
    return false;
  }
  cur.set(val.as<JsonVariantConst>());
  return true;
}

bool sameValue(JsonVariantConst a, JsonVariantConst b) {
  if (a.is<float>() && b.is<float>() && !a.is<bool>() && !b.is<bool>()) {
    double x = a.as<double>(), y = b.as<double>();
    double scale = fabs(x) > 1 ? fabs(x) : 1;
    return fabs(x - y) <= 1e-4 * scale;
  }
  std::string sa, sb;
  serializeJson(a, sa);
  serializeJson(b, sb);
  return sa == sb;
}

JsonVariantConst getPath(const JsonDocument& doc, const char* path) {
  JsonVariantConst cur = doc.as<JsonVariantConst>();
  if (!path || !*path) return cur;
  for (const std::string& k : splitPath(path)) {
    if (cur.is<JsonArrayConst>()) {
      char* end;
      long idx = strtol(k.c_str(), &end, 10);
      if (k.empty() || *end || idx < 0) return JsonVariantConst();
      cur = cur[(size_t)idx];
    } else {
      cur = cur[k.c_str()];
    }
    if (cur.isNull()) return JsonVariantConst();
  }
  return cur;
}

}  // namespace storage
