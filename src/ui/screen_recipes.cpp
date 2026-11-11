// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recipe list (use, new, copy, delete) and the editor for the active
// recipe: mash-in, steps, boil time, additions. Every change is validated
// and saved at once.

#include <stdio.h>

#include "core/settings.h"
#include "kit.h"
#include "screens.h"
#include "storage/storage.h"
#include "ui.h"

namespace ui {

namespace {

using settings::PumpMode;
using settings::Recipe;


const char* pumpText(PumpMode m) {
  switch (m) {
    case PumpMode::Off: return "off";
    case PumpMode::On: return "on";
    case PumpMode::Cycle: return "rest cycles";
  }
  return "?";
}

bool idTaken(const std::string& id) {
  for (const auto& r : storage::listRecipes())
    if (r.id == id) return true;
  return false;
}

// Save `r` (validated) as a new recipe and make it active.
void createRecipe(Recipe r, const std::string& name) {
  r.name = name;
  std::string id = storage::recipeId(name);
  if (idTaken(id)) {
    kit::message("Recipe exists", "Choose another name.");
    return;
  }
  std::string err;
  if (!storage::saveRecipe(r, id, err) || !storage::useRecipe(id, err)) {
    kit::message("Not saved", err.c_str());
    return;
  }
  go(Screen::RecipeEdit);
}

}  // namespace

// ============================================================================
namespace recipes {

void create(lv_obj_t* root) {
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(root, kit::kPad, 0);

  lv_obj_t* bar = kit::row(root);
  lv_obj_t* nb = kit::button(bar, LV_SYMBOL_PLUS " New", []() {
    kit::editText("Name of the new recipe", "", [](const std::string& name) {
      if (!name.empty()) createRecipe(settings::defaultRecipe(), name);
    });
  });
  lv_obj_set_flex_grow(nb, 1);
  lv_obj_t* cb = kit::button(bar, LV_SYMBOL_COPY " Copy active", []() {
    kit::editText("Name of the copy", settings::recipe().name + " 2", [](const std::string& name) {
      if (!name.empty()) createRecipe(settings::recipe(), name);
    });
  });
  lv_obj_set_flex_grow(cb, 1);
  lv_obj_t* eb = kit::button(bar, LV_SYMBOL_EDIT " Edit active", []() { go(Screen::RecipeEdit); });
  lv_obj_set_flex_grow(eb, 1);

  lv_obj_t* list = kit::scrollList(root);
  lv_obj_set_flex_grow(list, 1);
  for (const auto& info : storage::listRecipes()) {
    bool active = info.id == settings::current().activeRecipe;
    std::string text = (active ? LV_SYMBOL_OK "  " : "     ") + info.name;
    std::string id = info.id;
    std::string name = info.name;
    lv_obj_t* b = kit::button(list, text.c_str(), [id, name, active]() {
      std::vector<std::string> opts;
      opts.push_back(LV_SYMBOL_OK " Use");
      opts.push_back(LV_SYMBOL_EDIT " Use and edit");
      if (!active) opts.push_back(LV_SYMBOL_TRASH " Delete");
      kit::choose(name.c_str(), opts, [id, name](int i) {
        std::string err;
        if (i == 0 || i == 1) {
          if (!storage::useRecipe(id, err)) kit::message("Not loaded", err.c_str());
          else go(i == 1 ? Screen::RecipeEdit : Screen::Recipes);
        } else {
          kit::confirm(("Delete '" + name + "'?").c_str(), LV_SYMBOL_TRASH " Delete", [id]() {
            std::string e;
            if (!storage::deleteRecipe(id, e)) kit::message("Not deleted", e.c_str());
            go(Screen::Recipes);
          });
        }
      });
    });
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_t* l = lv_obj_get_child(b, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    if (!active) lv_obj_set_style_bg_color(b, kit::panel(), 0);
  }
}

void update() {}

}  // namespace recipes

// ============================================================================
namespace recipe_edit {
namespace {

lv_obj_t* list;
void render();

// Apply a change to a copy of the active recipe, save it and redraw.
void change(std::function<void(Recipe&)> edit) {
  Recipe r = settings::recipe();
  edit(r);
  std::string err;
  if (!storage::setActiveRecipe(r, err)) kit::message("Not saved", err.c_str());
  kit::later([]() {
    if (current() != Screen::RecipeEdit) return;  // left the screen meanwhile
    int32_t y = lv_obj_get_scroll_y(list);
    lv_obj_clean(list);
    render();
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, y, LV_ANIM_OFF);
  });
}

void editTemp(const char* title, float c, std::function<void(Recipe&, float)> set) {
  kit::editNumber(title, toDisplay(c), toDisplay(1), toDisplay(100), 0.5f, 1, unit(), [set](float v) {
    float cv = fromDisplay(v);
    change([set, cv](Recipe& r) { set(r, cv); });
  });
}

void editMinutes(const char* title, float min, float max, uint16_t v,
                 std::function<void(Recipe&, uint16_t)> set) {
  kit::editNumber(title, v, min, max, 1, 0, "min", [set](float m) {
    uint16_t mv = (uint16_t)m;
    change([set, mv](Recipe& r) { set(r, mv); });
  });
}

enum class Slot : uint8_t { MashIn, Rest, MashOut };

settings::MashStep& slot(Recipe& r, Slot s, int i) {
  return s == Slot::MashIn ? r.mashIn : s == Slot::MashOut ? r.mashOut : r.rests[i];
}

enum class StepAction : uint8_t { Name, Temp, Time, Pump, Up, Down, Delete };

void stepMenu(Slot sl, int i) {
  const settings::MashStep& s = slot(settings::recipe(), sl, i);
  bool rest = sl == Slot::Rest;
  char t[16], buf[64];
  fmtTemp(t, sizeof(t), s.tempC);
  std::vector<std::string> opts;
  std::vector<StepAction> acts;
  auto add = [&opts, &acts](const std::string& text, StepAction a) {
    opts.push_back(text);
    acts.push_back(a);
  };
  if (rest) add("Name: " + s.name, StepAction::Name);
  add(std::string("Temperature: ") + t, StepAction::Temp);
  if (s.minutes) snprintf(buf, sizeof(buf), "Rest: %u min", s.minutes);
  else snprintf(buf, sizeof(buf), "Rest: none (go on when reached)");
  add(buf, StepAction::Time);
  add(std::string("Pump: ") + pumpText(s.pump), StepAction::Pump);
  if (rest) {
    add(LV_SYMBOL_UP " Move up", StepAction::Up);
    add(LV_SYMBOL_DOWN " Move down", StepAction::Down);
    add(LV_SYMBOL_TRASH " Delete rest", StepAction::Delete);
  }
  const char* title = sl == Slot::MashIn ? "Mash in" : sl == Slot::MashOut ? "Mash out" : s.name.c_str();
  kit::choose(title, opts, [sl, i, acts](int o) {
    const settings::MashStep& st = slot(settings::recipe(), sl, i);
    switch (acts[o]) {
      case StepAction::Name:
        kit::editText("Rest name", st.name, [sl, i](const std::string& n) {
          change([sl, i, n](Recipe& r) { slot(r, sl, i).name = n; });
        });
        break;
      case StepAction::Temp:
        editTemp("Temperature", st.tempC, [sl, i](Recipe& r, float c) { slot(r, sl, i).tempC = c; });
        break;
      case StepAction::Time:
        editMinutes("Rest time (0 = none)", 0, 600, st.minutes,
                    [sl, i](Recipe& r, uint16_t m) { slot(r, sl, i).minutes = m; });
        break;
      case StepAction::Pump:
        kit::choose("Pump during this step", {"off", "on", "rest cycles"}, [sl, i](int m) {
          change([sl, i, m](Recipe& r) { slot(r, sl, i).pump = (PumpMode)m; });
        });
        break;
      case StepAction::Up:
        if (i > 0) change([i](Recipe& r) { std::swap(r.rests[i], r.rests[i - 1]); });
        break;
      case StepAction::Down:
        change([i](Recipe& r) {
          if (i + 1 < (int)r.rests.size()) std::swap(r.rests[i], r.rests[i + 1]);
        });
        break;
      case StepAction::Delete:
        kit::confirm(("Delete rest '" + st.name + "'?").c_str(), LV_SYMBOL_TRASH " Delete",
                     [i]() { change([i](Recipe& r) { r.rests.erase(r.rests.begin() + i); }); });
        break;
    }
  });
}

void stepRow(const char* label, const settings::MashStep& s, Slot sl, int i) {
  char t[16], buf[64];
  fmtTemp(t, sizeof(t), s.tempC);
  const char* pump = s.pump == PumpMode::Off ? "  pump off" : s.pump == PumpMode::Cycle ? "  cycles" : "";
  if (s.minutes) snprintf(buf, sizeof(buf), "%s  %u min%s", t, s.minutes, pump);
  else snprintf(buf, sizeof(buf), "%s  no rest%s", t, pump);
  kit::rowValue(list, label, buf, [sl, i]() { stepMenu(sl, i); });
}

// Dim "then: add malt" line between the timeline rows.
void note(const char* text) {
  lv_obj_t* l = kit::label(list, (std::string("      ") + LV_SYMBOL_DOWN " " + text).c_str(), kit::kSmall);
  lv_obj_set_style_text_color(l, kit::warn(), 0);
}

void additionMenu(int i) {
  const settings::Addition& a = settings::recipe().additions[i];
  char b1[48], b2[48], b3[48];
  snprintf(b1, sizeof(b1), "Name: %s", a.name.c_str());
  snprintf(b2, sizeof(b2), "At %u min before the end", a.atMinutes);
  snprintf(b3, sizeof(b3), "Amount: %g %s", a.amount, a.unit.c_str());
  std::vector<std::string> opts = {b1, b2, b3, "Unit: " + a.unit, LV_SYMBOL_TRASH " Delete"};
  kit::choose(a.name.c_str(), opts, [i](int o) {
    const settings::Addition& ad = settings::recipe().additions[i];
    switch (o) {
      case 0:
        kit::editText("Addition name", ad.name, [i](const std::string& n) {
          change([i, n](Recipe& r) { r.additions[i].name = n; });
        });
        break;
      case 1:
        editMinutes("Minutes before the end of the boil", 0, 600, ad.atMinutes,
                    [i](Recipe& r, uint16_t m) { r.additions[i].atMinutes = m; });
        break;
      case 2:
        kit::editNumber("Amount", ad.amount, 0, 10000, 1, 0, ad.unit.c_str(), [i](float v) {
          change([i, v](Recipe& r) { r.additions[i].amount = v; });
        });
        break;
      case 3:
        kit::choose("Unit", {"g", "kg", "ml", "l", "pcs"}, [i](int u) {
          static const char* units[] = {"g", "kg", "ml", "l", "pcs"};
          std::string unitText = units[u];
          change([i, unitText](Recipe& r) { r.additions[i].unit = unitText; });
        });
        break;
      case 4:
        kit::confirm(("Delete '" + ad.name + "'?").c_str(), LV_SYMBOL_TRASH " Delete",
                     [i]() { change([i](Recipe& r) { r.additions.erase(r.additions.begin() + i); }); });
        break;
    }
  });
}

void render() {
  const Recipe& r = settings::recipe();
  char buf[64], t[16];

  kit::rowValue(list, "Name", r.name.c_str(), []() {
    kit::editText("Recipe name", settings::recipe().name, [](const std::string& n) {
      if (!n.empty()) change([n](Recipe& rr) { rr.name = n; });
    });
  });

  kit::rowHeader(list, "MASH");
  kit::rowSwitch(list, "Mash (off = extract, boil only)", r.mash, [](bool on) {
    change([on](Recipe& rr) { rr.mash = on; });
  });
  if (r.mash) {
    stepRow(LV_SYMBOL_RIGHT " Mash in", r.mashIn, Slot::MashIn, 0);
    note("then: add malt");
    for (size_t i = 0; i < r.rests.size(); i++) {
      std::string label = std::to_string(i + 1) + ". " + r.rests[i].name;
      stepRow(label.c_str(), r.rests[i], Slot::Rest, (int)i);
    }
    lv_obj_t* add = kit::button(list, LV_SYMBOL_PLUS " Add rest", []() {
      change([](Recipe& rr) {
        settings::MashStep s;
        s.tempC = rr.rests.empty() ? rr.mashIn.tempC : rr.rests.back().tempC;
        s.minutes = 30;
        s.pump = rr.rests.empty() ? rr.mashIn.pump : rr.rests.back().pump;
        s.name = "Rest " + std::to_string(rr.rests.size() + 1);
        rr.rests.push_back(s);
      });
    });
    lv_obj_set_width(add, LV_PCT(100));
    stepRow(LV_SYMBOL_LEFT " Mash out", r.mashOut, Slot::MashOut, 0);
    note("then: remove malt");
  }

  if (r.mash) {
    if (r.spargeTempC > 0) fmtTemp(buf, sizeof(buf), r.spargeTempC);
    else snprintf(buf, sizeof(buf), "none");
    kit::rowValue(list, "Sparge water", buf, []() {
      float c = settings::recipe().spargeTempC;
      kit::editNumber("Sparge water (lowest = none)", toDisplay(c > 0 ? c : 0), toDisplay(0), toDisplay(99), 0.5f, 1,
                      unit(), [](float v) {
                        float cv = v <= toDisplay(0) + 0.01f ? 0 : fromDisplay(v);
                        change([cv](Recipe& rr) { rr.spargeTempC = cv; });
                      });
    });
  }

  kit::rowHeader(list, "BOIL");
  snprintf(buf, sizeof(buf), r.boilMinutes ? "%u min" : "no boil", r.boilMinutes);
  kit::rowValue(list, "Boil time", buf, []() {
    editMinutes("Boil time (0 = no boil)", 0, 600, settings::recipe().boilMinutes,
                [](Recipe& rr, uint16_t m) { rr.boilMinutes = m; });
  });
  for (size_t i = 0; i < r.additions.size(); i++) {
    const settings::Addition& a = r.additions[i];
    if (a.amount > 0) snprintf(buf, sizeof(buf), "%u min  %g %s", a.atMinutes, a.amount, a.unit.c_str());
    else snprintf(buf, sizeof(buf), "%u min", a.atMinutes);
    int idx = (int)i;
    kit::rowValue(list, a.name.c_str(), buf, [idx]() { additionMenu(idx); });
  }
  lv_obj_t* addA = kit::button(list, LV_SYMBOL_PLUS " Add addition", []() {
    change([](Recipe& rr) {
      settings::Addition a;
      a.name = "Hops";
      a.atMinutes = 10;
      a.unit = "g";
      rr.additions.push_back(a);
    });
  });
  lv_obj_set_width(addA, LV_PCT(100));
}

}  // namespace

void create(lv_obj_t* root) {
  list = kit::scrollList(root);
  render();
}

void update() {}

}  // namespace recipe_edit
}  // namespace ui
