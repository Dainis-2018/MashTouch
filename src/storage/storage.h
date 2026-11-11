// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Settings, recipes and the power-loss resume point on LittleFS:
//   /settings.json         all settings (roles, PID, pump, process...)
//   /recipes/<id>.json     one file per recipe; id = name in lower case
//   /resume.json           brew position + recipe copy, while a brew runs
// Without a filesystem everything still works from defaults in RAM;
// saving then fails with an error.
#pragma once
#include <string>
#include <vector>

#include "core/brew.h"
#include "core/settings.h"

namespace storage {

// Mount, load the settings and the active recipe (creating the default
// files on first boot). Call before anything reads settings.
bool begin();

// ---- Settings ------------------------------------------------------------
bool saveSettings(std::string& err);
bool resetSettings(std::string& err);  // back to defaults, saved
// Change one setting by dotted path ("heater.kp", "outputs.1.role") and save.
bool setSetting(const char* path, const char* value, std::string& err);
// Settings (or the part at path) as JSON text.
std::string settingsJson(const char* path, bool pretty);

// ---- Recipes -------------------------------------------------------------
struct RecipeInfo {
  std::string id;
  std::string name;
};
std::vector<RecipeInfo> listRecipes();
bool loadRecipe(const std::string& id, settings::Recipe& out, std::string& err);
// Saves under the id made from the recipe name; returns the id.
bool saveRecipe(const settings::Recipe& r, std::string& id, std::string& err);
bool deleteRecipe(const std::string& id, std::string& err);
// Make a stored recipe the active one (settings::recipe()).
bool useRecipe(const std::string& id, std::string& err);
// Edit the active recipe by dotted path ("steps.0.minutes") and save it.
bool setRecipeField(const char* path, const char* value, std::string& err);
// Replace the active recipe (validated, saved under the active id).
bool setActiveRecipe(const settings::Recipe& r, std::string& err);
// A stored recipe as JSON text (empty if missing).
std::string recipeFileJson(const std::string& id, bool pretty);
// Save a whole recipe JSON object under an existing or new id; updates the
// active recipe when it is that id.
bool saveRecipeJson(const std::string& id, const std::string& json, std::string& err);
// Replace the active recipe with a whole JSON object and save it.
bool replaceRecipe(const char* json, std::string& err);
std::string recipeJson(const settings::Recipe& r, const char* path, bool pretty);
std::string recipeId(const std::string& name);

// ---- Import ----------------------------------------------------------------
// Recipes from BeerXML 1.0 (see beerxml.h) or MashTouch JSON (one recipe
// object or an array). Each becomes a new stored recipe; a name already in
// use gets "-2", "-3"... The active recipe does not change. ids: the new
// recipe ids; notes: one line per recipe (what was imported or skipped).
bool importRecipes(const std::string& text, std::vector<std::string>& ids, std::vector<std::string>& notes,
                   std::string& err);

// ---- Resume after power loss ---------------------------------------------
bool saveResume(const brew::Snapshot& s, const settings::Recipe& r);
bool loadResume(brew::Snapshot& s, settings::Recipe& r);
void clearResume();

}  // namespace storage
