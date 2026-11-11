// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "beerxml.h"
#include "files.h"
#include "schema.h"
#include "textfold.h"

namespace storage {
namespace {

constexpr const char* kSettingsPath = "/settings.json";
constexpr const char* kRecipeDir = "/recipes";
constexpr const char* kResumePath = "/resume.json";
constexpr int kResumeVersion = 3;  // 3: step = index in mash in / rests / mash out
constexpr size_t kMaxIdLen = 32;

std::string recipePath(const std::string& id) { return std::string(kRecipeDir) + "/" + id + ".json"; }

template <class T>
void clampMax(T& v, T max) {
  if (v > max) v = max;
}

// Keep values inside what the control code can handle.
void sanitize(settings::Settings& s) {
  clampMax<uint8_t>(s.heater.boilPowerPct, 100);
  clampMax<uint8_t>(s.heater.mashPowerPct, 100);
  if (s.heater.mashPowerPct < 10) s.heater.mashPowerPct = 10;
  clampMax<uint8_t>(s.sound.volume, 100);
  clampMax<float>(s.sparge.tempC, 99);
  for (auto& o : s.outputs) {
    if (o.index < 1) o.index = 1;
    if (o.role == settings::OutputRole::Heater) clampMax<uint8_t>(o.index, settings::kMaxHeaters);
  }
  if (s.heater.sampleMs < 100) s.heater.sampleMs = 100;
  if (s.heater.windowMs < 500) s.heater.windowMs = 500;
  clampMax<float>(s.autotune.outputHighPct, 100);
  if (s.autotune.outputLowPct < 0) s.autotune.outputLowPct = 0;
  if (s.autotune.cycles == 0) s.autotune.cycles = 1;
  clampMax<uint8_t>(s.pump.primePulses, 20);
  clampMax<uint8_t>(s.fan.minPct, 100);
  if (s.brightness < 10) s.brightness = 10;
  if (s.sim.speed < 1) s.sim.speed = 1;
  clampMax<float>(s.sim.speed, 100);
}

// After a set: the value read back must be the one asked for. Otherwise it
// was out of range, an unknown name, or clamped by sanitize().
template <class T>
bool accepted(const JsonDocument& wanted, const T& result, const char* path, std::string& err) {
  JsonVariantConst w = getPath(wanted, path);
  // Whole objects/arrays gain default fields on the way through: not compared.
  if (w.is<JsonObjectConst>() || w.is<JsonArrayConst>()) return true;
  JsonDocument check;
  toJson(result, check.to<JsonObject>());
  if (sameValue(w, getPath(check, path))) return true;
  err = "value not accepted (out of range or unknown name)";
  return false;
}

std::string toText(const JsonDocument& doc, JsonVariantConst v, bool pretty) {
  std::string out;
  if (pretty) serializeJsonPretty(v, out);
  else serializeJson(v, out);
  (void)doc;
  return out;
}

}  // namespace

std::string recipeId(const std::string& name) {
  std::string id;
  for (char c : name) {
    if (id.size() >= kMaxIdLen) break;
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) id += c;
    else if (!id.empty() && id.back() != '-') id += '-';
  }
  while (!id.empty() && id.back() == '-') id.pop_back();
  return id.empty() ? "recipe" : id;
}

// ---- Settings ------------------------------------------------------------

bool saveSettings(std::string& err) {
  JsonDocument doc;
  toJson(settings::current(), doc.to<JsonObject>());
  return files::writeJson(kSettingsPath, doc, err);
}

bool resetSettings(std::string& err) {
  std::string active = settings::current().activeRecipe;
  settings::current() = settings::defaults();
  settings::current().activeRecipe = active;
  return saveSettings(err);
}

bool setSetting(const char* path, const char* value, std::string& err) {
  JsonDocument doc;
  toJson(settings::current(), doc.to<JsonObject>());
  if (!strcmp(path, "version") || !strcmp(path, "activeRecipe")) {
    err = "read-only here";
    return false;
  }
  if (!setPath(doc, path, value, err)) return false;
  settings::Settings s = settings::current();
  fromJson(doc.as<JsonObjectConst>(), s);
  sanitize(s);
  if (!accepted(doc, s, path, err)) return false;
  settings::current() = s;
  return saveSettings(err);
}

std::string settingsJson(const char* path, bool pretty) {
  JsonDocument doc;
  toJson(settings::current(), doc.to<JsonObject>());
  JsonVariantConst v = getPath(doc, path);
  return v.isNull() ? std::string() : toText(doc, v, pretty);
}

// ---- Recipes -------------------------------------------------------------

std::vector<RecipeInfo> listRecipes() {
  std::vector<RecipeInfo> out;
  for (const std::string& f : files::list(kRecipeDir)) {
    if (f.size() < 6 || f.compare(f.size() - 5, 5, ".json") != 0) continue;
    RecipeInfo info;
    info.id = f.substr(0, f.size() - 5);
    JsonDocument doc;
    std::string err;
    if (files::readJson(recipePath(info.id).c_str(), doc, err))
      info.name = doc["name"] | info.id.c_str();
    else
      info.name = "(unreadable)";
    out.push_back(info);
  }
  return out;
}

bool loadRecipe(const std::string& id, settings::Recipe& out, std::string& err) {
  JsonDocument doc;
  if (!files::readJson(recipePath(id).c_str(), doc, err)) return false;
  settings::Recipe r;  // missing fields keep the struct defaults
  fromJson(doc.as<JsonObjectConst>(), r);
  if (!validate(r, err)) return false;
  out = r;
  return true;
}

bool saveRecipe(const settings::Recipe& r, std::string& id, std::string& err) {
  settings::Recipe copy = r;
  if (!validate(copy, err)) return false;
  if (!files::ensureDir(kRecipeDir)) {
    err = "no filesystem";
    return false;
  }
  if (id.empty()) id = recipeId(copy.name);
  JsonDocument doc;
  toJson(copy, doc.to<JsonObject>());
  return files::writeJson(recipePath(id).c_str(), doc, err);
}

bool deleteRecipe(const std::string& id, std::string& err) {
  if (id == settings::current().activeRecipe) {
    err = "cannot delete the active recipe";
    return false;
  }
  if (!files::remove(recipePath(id).c_str())) {
    err = "no recipe '" + id + "'";
    return false;
  }
  return true;
}

bool useRecipe(const std::string& id, std::string& err) {
  settings::Recipe r;
  if (!loadRecipe(id, r, err)) return false;
  settings::recipe() = r;
  settings::current().activeRecipe = id;
  return saveSettings(err);
}

bool setRecipeField(const char* path, const char* value, std::string& err) {
  JsonDocument doc;
  toJson(settings::recipe(), doc.to<JsonObject>());
  if (!setPath(doc, path, value, err)) return false;
  settings::Recipe r;
  fromJson(doc.as<JsonObjectConst>(), r);
  if (!validate(r, err)) return false;
  if (!accepted(doc, r, path, err)) return false;
  std::string id = settings::current().activeRecipe;
  if (!saveRecipe(r, id, err)) return false;
  settings::recipe() = r;
  return true;
}

bool setActiveRecipe(const settings::Recipe& rec, std::string& err) {
  settings::Recipe r = rec;
  if (!validate(r, err)) return false;
  std::string id = settings::current().activeRecipe;
  if (!saveRecipe(r, id, err)) return false;
  if (settings::current().activeRecipe.empty()) {
    settings::current().activeRecipe = id;
    saveSettings(err);
  }
  settings::recipe() = r;
  return true;
}

bool replaceRecipe(const char* json, std::string& err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) {
    err = std::string("JSON: ") + e.c_str();
    return false;
  }
  if (!doc.is<JsonObject>()) {
    err = "expected a JSON object";
    return false;
  }
  settings::Recipe r;
  fromJson(doc.as<JsonObjectConst>(), r);
  if (!validate(r, err)) return false;
  std::string id = settings::current().activeRecipe;
  if (!saveRecipe(r, id, err)) return false;
  settings::recipe() = r;
  return true;
}

std::string recipeJson(const settings::Recipe& r, const char* path, bool pretty) {
  JsonDocument doc;
  toJson(r, doc.to<JsonObject>());
  JsonVariantConst v = getPath(doc, path);
  return v.isNull() ? std::string() : toText(doc, v, pretty);
}

// ---- Import --------------------------------------------------------------

namespace {

// Names from JSON may be any UTF-8: fold them to what the screen can show.
void foldNames(settings::Recipe& r) {
  r.name = textfold::fromUtf8(r.name);
  r.mashIn.name = textfold::fromUtf8(r.mashIn.name);
  r.mashOut.name = textfold::fromUtf8(r.mashOut.name);
  for (auto& s : r.rests) s.name = textfold::fromUtf8(s.name);
  for (auto& a : r.additions) {
    a.name = textfold::fromUtf8(a.name);
    a.unit = textfold::fromUtf8(a.unit);
  }
}

std::string freeId(const std::string& name) {
  std::string base = recipeId(name);
  std::vector<RecipeInfo> existing = listRecipes();
  auto taken = [&existing](const std::string& id) {
    for (const auto& r : existing)
      if (r.id == id) return true;
    return false;
  };
  if (!taken(base)) return base;
  for (int n = 2; n < 1000; n++) {
    std::string id = base + "-" + std::to_string(n);
    if (!taken(id)) return id;
  }
  return base + "-x";
}

}  // namespace

bool importRecipes(const std::string& text, std::vector<std::string>& ids, std::vector<std::string>& notes,
                   std::string& err) {
  static const char* kBlank = " \t\r\n";
  size_t p = text.find_first_not_of(kBlank);
  if (p != std::string::npos && text.compare(p, 3, "\xEF\xBB\xBF") == 0) p = text.find_first_not_of(kBlank, p + 3);
  if (p == std::string::npos) {
    err = "nothing to import";
    return false;
  }

  std::vector<settings::Recipe> recipes;
  if (text[p] == '<') {
    beerxml::Result res;
    if (!beerxml::parse(text, res, err)) return false;
    recipes = res.recipes;
    notes = res.notes;
  } else if (text[p] == '{' || text[p] == '[') {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, text);
    if (e) {
      err = std::string("JSON: ") + e.c_str();
      return false;
    }
    std::vector<JsonObjectConst> objs;
    if (doc.is<JsonArrayConst>()) {
      for (JsonVariantConst v : doc.as<JsonArrayConst>())
        if (v.is<JsonObjectConst>()) objs.push_back(v.as<JsonObjectConst>());
    } else if (doc.is<JsonObjectConst>()) {
      objs.push_back(doc.as<JsonObjectConst>());
    }
    for (JsonObjectConst o : objs) {
      settings::Recipe r;
      fromJson(o, r);
      foldNames(r);
      recipes.push_back(r);
      notes.push_back(r.name + ": " + (r.mash ? std::to_string(r.rests.size()) + " rest(s), " : std::string("no mash, ")) +
                      std::to_string(r.additions.size()) + " additions");
    }
  } else {
    err = "neither BeerXML nor MashTouch JSON";
    return false;
  }

  for (size_t i = 0; i < recipes.size(); i++) {
    settings::Recipe& r = recipes[i];
    std::string why;
    if (!validate(r, why)) {
      notes.push_back("not imported: " + r.name + ": " + why);
      continue;
    }
    std::string id = freeId(r.name);
    if (!saveRecipe(r, id, why)) {
      notes.push_back("not saved: " + r.name + ": " + why);
      continue;
    }
    ids.push_back(id);
  }
  if (ids.empty()) {
    err = "no recipe imported";
    return false;
  }
  return true;
}

// ---- Resume --------------------------------------------------------------

bool saveResume(const brew::Snapshot& s, const settings::Recipe& r) {
  JsonDocument doc;
  doc["version"] = kResumeVersion;
  toJson(s, doc["snapshot"].to<JsonObject>());
  toJson(r, doc["recipe"].to<JsonObject>());
  std::string err;
  return files::writeJson(kResumePath, doc, err);
}

bool loadResume(brew::Snapshot& s, settings::Recipe& r) {
  if (!files::exists(kResumePath)) return false;
  JsonDocument doc;
  std::string err;
  if (!files::readJson(kResumePath, doc, err)) return false;
  if ((doc["version"] | 0) != kResumeVersion) return false;  // phase numbers differ
  brew::Snapshot snap;
  fromJson(doc["snapshot"].as<JsonObjectConst>(), snap);
  settings::Recipe rec;
  fromJson(doc["recipe"].as<JsonObjectConst>(), rec);
  if (snap.phase == brew::Phase::Idle || snap.phase == brew::Phase::Finished) return false;
  if (!validate(rec, err)) return false;
  s = snap;
  r = rec;
  return true;
}

void clearResume() { files::remove(kResumePath); }

// ---- Boot ----------------------------------------------------------------

bool begin() {
  bool ok = files::begin();
  if (!ok) {
    Serial.println("LittleFS mount failed: running on defaults");
    return false;
  }
  std::string err;
  settings::Settings& s = settings::current();
  bool dirty = false;

  if (files::exists(kSettingsPath)) {
    JsonDocument doc;
    if (files::readJson(kSettingsPath, doc, err)) {
      settings::Settings loaded = settings::defaults();
      fromJson(doc.as<JsonObjectConst>(), loaded);
      sanitize(loaded);
      s = loaded;
    } else {
      // Keep the broken file for inspection; run on defaults.
      Serial.printf("Settings not loaded (%s): using defaults\n", err.c_str());
      files::rename(kSettingsPath, "/settings.bad.json");
      dirty = true;
    }
  } else {
    dirty = true;  // first boot: write the defaults so they can be inspected
  }

  files::ensureDir(kRecipeDir);
  std::vector<RecipeInfo> recipes = listRecipes();
  if (recipes.empty()) {
    std::string id;
    if (saveRecipe(settings::defaultRecipe(), id, err)) recipes.push_back({id, ""});
  }
  bool loaded = false;
  if (!s.activeRecipe.empty()) loaded = loadRecipe(s.activeRecipe, settings::recipe(), err);
  if (!loaded && !recipes.empty()) {
    if (!s.activeRecipe.empty())
      Serial.printf("Recipe '%s' not loaded (%s)\n", s.activeRecipe.c_str(), err.c_str());
    for (const auto& r : recipes) {
      if (loadRecipe(r.id, settings::recipe(), err)) {
        s.activeRecipe = r.id;
        dirty = true;
        break;
      }
    }
  }

  if (dirty && !saveSettings(err)) Serial.printf("Settings not saved: %s\n", err.c_str());
  Serial.printf("LittleFS %u/%u kB, recipe '%s'\n", (unsigned)(files::usedBytes() / 1024),
                (unsigned)(files::totalBytes() / 1024), settings::recipe().name.c_str());
  return true;
}

}  // namespace storage

namespace storage {

std::string recipeFileJson(const std::string& id, bool pretty) {
  settings::Recipe r;
  std::string err;
  if (!loadRecipe(id, r, err)) return std::string();
  return recipeJson(r, "", pretty);
}

bool saveRecipeJson(const std::string& id, const std::string& json, std::string& err) {
  if (id.empty() || recipeId(id) != id) {
    err = "invalid recipe id";
    return false;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) {
    err = std::string("JSON: ") + e.c_str();
    return false;
  }
  if (!doc.is<JsonObject>()) {
    err = "expected a JSON object";
    return false;
  }
  settings::Recipe r;
  fromJson(doc.as<JsonObjectConst>(), r);
  if (!validate(r, err)) return false;
  std::string target = id;
  if (!saveRecipe(r, target, err)) return false;
  if (id == settings::current().activeRecipe) settings::recipe() = r;
  return true;
}

}  // namespace storage
