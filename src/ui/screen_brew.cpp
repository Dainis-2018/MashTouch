// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Brew dashboard for automatic, manual and autotune mode: temperature,
// setpoint, timer, heater/pump/fan state, chart, actions and prompts.
// Skip and Stop need a long press (hold to confirm).

#include <stdio.h>

#include <string>
#include <vector>

#include "app/controller.h"
#include "core/settings.h"
#include "kit.h"
#include "screens.h"
#include "ui.h"

namespace ui {
namespace brew {
namespace {

using controller::Mode;

Mode builtFor;
lv_obj_t* stepLabel;
lv_obj_t* tempLabel;
lv_obj_t* spLabel;
lv_obj_t* timerLabel;
lv_obj_t* timerNote;
lv_obj_t* bar;
lv_obj_t* heaterLabel;
lv_obj_t* pumpLabel;
lv_obj_t* fanLabel;
lv_obj_t* infoLabel;
lv_obj_t* chart;
lv_chart_series_t* tempSeries;
lv_chart_series_t* spSeries;
uint32_t chartSamples;
lv_obj_t* pauseBtn;   // auto
lv_obj_t* heatBtn;    // manual
lv_obj_t* pumpBtn;
lv_obj_t* overlay;
int shownPrompt;      // brew::Prompt, or 100 + controller::Confirm

// Step strip (automatic mode): Mash in > +malt > rests > Mash out > -malt >
// Boil > Cool. Keys identify a chip; the current one is highlighted.
enum ChipKey { kChipStep = 1000, kChipMaltIn = 2000, kChipMaltOut = 2001, kChipBoil = 3000, kChipCool = 3001,
               kChipAfterAll = 9999 };
struct Chip {
  lv_obj_t* obj;
  int key;
  bool marker;
};
std::vector<Chip> chips;
int shownChip;

lv_obj_t* actionButton(lv_obj_t* row, const char* text, kit::Fn fn) {
  lv_obj_t* b = kit::button(row, text, fn);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_height(b, 50);
  lv_obj_set_style_pad_hor(b, 4, 0);
  return b;
}

// Name of mash sequence step i (0 = mash in, last = mash out).
std::string seqName(const settings::Recipe& r, int i) {
  if (i == 0) return "Mash in";
  if (i == (int)r.rests.size() + 1) return "Mash out";
  if (i > 0 && i <= (int)r.rests.size()) return r.rests[i - 1].name;
  return "";
}

void addChip(lv_obj_t* strip, const std::string& text, int key, bool marker) {
  lv_obj_t* l = kit::label(strip, text.c_str(), marker ? kit::kSmall : kit::kNormal);
  if (!marker) {
    lv_obj_set_style_pad_hor(l, 8, 0);
    lv_obj_set_style_pad_ver(l, 1, 0);
    lv_obj_set_style_radius(l, 12, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  }
  chips.push_back(Chip{l, key, marker});
}

void buildStrip(lv_obj_t* strip) {
  chips.clear();
  const settings::Recipe& r = controller::brewRecipe();
  if (r.mash) {
    addChip(strip, "Mash in", kChipStep, false);
    addChip(strip, "+malt", kChipMaltIn, true);
    for (size_t i = 0; i < r.rests.size(); i++) addChip(strip, r.rests[i].name, kChipStep + 1 + (int)i, false);
    addChip(strip, "Mash out", kChipStep + 1 + (int)r.rests.size(), false);
    addChip(strip, "-malt", kChipMaltOut, true);
  }
  if (r.boilMinutes) addChip(strip, "Boil", kChipBoil, false);
  addChip(strip, "Cool", kChipCool, false);
}

int currentChipKey(const controller::Status& st) {
  using ::brew::Phase;
  switch (st.phase) {
    case Phase::Mash:
      return st.step == 0 && st.prompt == ::brew::Prompt::AddMalt ? kChipMaltIn : kChipStep + st.step;
    case Phase::IodineTest: return kChipStep + st.stepCount - 1;
    case Phase::RemoveMalt: return kChipMaltOut;
    case Phase::Boil: return kChipBoil;
    case Phase::HotWhirlpool:
    case Phase::Cooling:
    case Phase::ColdWhirlpool: return kChipCool;
    case Phase::Finished: return kChipAfterAll;
    default: return -1;  // water check, prime, delayed start: before the first
  }
}

void updateStrip(const controller::Status& st) {
  if (chips.empty()) return;
  int key = currentChipKey(st);
  int cur = key == kChipAfterAll ? (int)chips.size() : -1;
  for (size_t i = 0; i < chips.size(); i++)
    if (chips[i].key == key) cur = (int)i;
  if (cur == shownChip) return;
  shownChip = cur;
  for (int i = 0; i < (int)chips.size(); i++) {
    lv_obj_t* o = chips[i].obj;
    bool done = i < cur;
    lv_obj_set_style_text_color(o, done ? kit::dim() : (i == cur && chips[i].marker ? kit::warn() : lv_color_white()), 0);
    if (!chips[i].marker) lv_obj_set_style_bg_color(o, i == cur ? kit::accent() : kit::panel(), 0);
  }
  if (cur >= 0 && cur < (int)chips.size()) lv_obj_scroll_to_view(chips[cur].obj, LV_ANIM_ON);
}

void addFiveMinutes() {
  const controller::Status& st = controller::status();
  if (!st.remainingMs && !st.elapsedMs) {
    kit::toast("No timer in this step");
    return;
  }
  controller::setTimerMin((uint16_t)((st.remainingMs + 59999) / 60000 + 5));
  kit::toast("+5 min");
}

// Long press runs fn; a short tap only explains.
lv_obj_t* holdButton(lv_obj_t* row, const char* text, const char* hint, kit::Fn fn) {
  lv_obj_t* b = actionButton(row, text, kit::Fn());
  kit::on(b, LV_EVENT_LONG_PRESSED, fn);
  std::string h = hint;
  kit::on(b, LV_EVENT_SHORT_CLICKED, [h]() { kit::toast(h.c_str()); });
  return b;
}

float spMin() { return 5; }
float spMax() { return 105; }

void editSetpoint() {
  const controller::Status& st = controller::status();
  if (st.mode == Mode::Autotune) return;
  if (st.mode == Mode::Auto && (st.phase == ::brew::Phase::Idle || st.phase == ::brew::Phase::Finished))
    return;
  kit::editNumber("Setpoint", toDisplay(st.setpointC), toDisplay(spMin()), toDisplay(spMax()), 0.5f, 1,
                  unit(), [](float v) { controller::setSetpoint(fromDisplay(v)); });
}

void editRemaining() {
  const controller::Status& st = controller::status();
  kit::editNumber("Remaining time", (float)((st.remainingMs + 59999) / 60000), 0, 24 * 60, 1, 0, "min",
                  [](float v) { controller::setTimerMin((uint16_t)v); });
}

void editTimer() {
  const controller::Status& st = controller::status();
  if (st.mode == Mode::Manual) {
    kit::editNumber("Countdown (0 = off)", st.manualCountdown ? (float)(st.remainingMs / 60000) : 30, 0,
                    600, 1, 0, "min", [](float v) { controller::setTimerMin((uint16_t)v); });
  } else if (st.mode == Mode::Auto) {
    bool heatingUp = !st.reached && st.prompt == ::brew::Prompt::None &&
                     (st.phase == ::brew::Phase::Mash || (st.phase == ::brew::Phase::Boil && st.tempC >= 85));
    if (!heatingUp) {
      editRemaining();
      return;
    }
    // Still heating: the timer can be started by hand (a kettle that boils
    // below the boil setting, or "close enough" for a rest).
    kit::choose("Timer", {LV_SYMBOL_PLAY " Start the timer now", LV_SYMBOL_EDIT " Change the time"}, [](int i) {
      if (i == 0) controller::startTimerNow();
      else editRemaining();
    });
  }
}

void editBoilPower() {
  kit::editNumber("Boil power", controller::status().boilPowerPct, 0, 100, 5, 0, "%",
                  [](float v) { controller::setBoilPower((uint8_t)v); });
}

// ---- Prompt / confirmation overlay ------------------------------------------

struct PromptText {
  const char* text;
  const char* yes;
  const char* no;  // nullptr: no "no" button
};

PromptText promptText(::brew::Prompt p) {
  using ::brew::Prompt;
  switch (p) {
    case Prompt::WaterAdded: return {"Is the water in the kettle and the\nelement covered?", LV_SYMBOL_OK " Yes, start", "Stop"};
    case Prompt::AddMalt: return {"Strike temperature reached.\nAdd the grain and stir, then confirm.", LV_SYMBOL_OK " Grain is in", "Stop"};
    case Prompt::IodineTest: return {"Iodine test: is the conversion complete?", LV_SYMBOL_OK " Yes", "Not yet"};
    case Prompt::RemoveMalt: return {"Mash finished. Lift the bag or malt\npipe out, then confirm. Heating to\nthe boil starts at once.", LV_SYMBOL_OK " Bag is out", "Stop"};
    case Prompt::StartWhirlpool: return {"Start the whirlpool?", LV_SYMBOL_OK " Start", "Skip"};
    case Prompt::StartCooling: return {"Boil finished, heater off.\nStart the chiller, then confirm.", LV_SYMBOL_OK " Cooling", "Skip"};
    case Prompt::Finished: return {"Brew finished.", LV_SYMBOL_OK " OK", nullptr};
    default: return {"", "", nullptr};
  }
}

void showOverlay(lv_obj_t* root, const char* text, const char* yes, const char* no, bool danger) {
  overlay = kit::card(root);
  lv_obj_set_size(overlay, LV_PCT(100), 130);
  lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(overlay, danger ? lv_color_hex(0x5A1E1E) : lv_color_hex(0x1E3A5A), 0);
  lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(overlay, kit::kPad, 0);
  lv_obj_t* l = kit::label(overlay, text, kit::kNormal);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_flex_grow(l, 1);
  lv_obj_t* r = kit::row(overlay);
  if (no) {
    lv_obj_t* nb = actionButton(r, no, []() { controller::answer(false); });
    kit::setColor(nb, kit::dim());
  }
  lv_obj_t* yb = actionButton(r, yes, []() { controller::answer(true); });
  kit::setColor(yb, danger ? kit::bad() : kit::good());
}

void updateOverlay(lv_obj_t* root, const controller::Status& st) {
  int want = (int)st.prompt;
  if (st.confirm != controller::Confirm::None) want = 100 + (int)st.confirm;
  if (want == shownPrompt) return;
  shownPrompt = want;
  if (overlay) {
    lv_obj_delete(overlay);
    overlay = nullptr;
  }
  if (want == 100 + (int)controller::Confirm::Skip) {
    showOverlay(root, "Skip this step?", LV_SYMBOL_NEXT " Skip", "Cancel", true);
  } else if (want == 100 + (int)controller::Confirm::Stop) {
    showOverlay(root, "Stop and end this brew?", LV_SYMBOL_STOP " Stop", "Cancel", true);
  } else if (st.prompt != ::brew::Prompt::None) {
    PromptText t = promptText(st.prompt);
    showOverlay(root, t.text, t.yes, t.no, false);
  }
}

// ---- Text helpers -------------------------------------------------------------

void stepText(char* buf, size_t n, const controller::Status& st) {
  int len = 0;
  if (st.faults) {
    snprintf(buf, n, "%s%s", st.faults & controller::kFaultSensor ? LV_SYMBOL_WARNING " SENSOR FAULT" : LV_SYMBOL_WARNING " SAFETY CUT",
             st.paused ? " - paused" : "");
    return;
  }
  switch (st.mode) {
    case Mode::Auto:
      if (st.phase == ::brew::Phase::Mash && st.step == 0)
        len = snprintf(buf, n, "Mash in");
      else if (st.phase == ::brew::Phase::Mash && st.step == st.stepCount - 1)
        len = snprintf(buf, n, "Mash out");
      else if (st.phase == ::brew::Phase::Mash)
        len = snprintf(buf, n, "Rest %d/%d: %s", st.step, st.stepCount - 2, st.stepName);
      else
        len = snprintf(buf, n, "%s", st.stepName);
      break;
    case Mode::Manual: len = snprintf(buf, n, "Manual"); break;
    case Mode::Autotune: len = snprintf(buf, n, "PID autotune"); break;
    case Mode::Idle: len = snprintf(buf, n, "Not running"); break;
  }
  if (st.paused && len >= 0 && (size_t)len < n) snprintf(buf + len, n - len, "  - PAUSED");
}

void infoText(char* buf, size_t n, const controller::Status& st) {
  buf[0] = 0;
  if (st.spargeOn && st.mode != Mode::Autotune && !st.priming && !st.pumpResting) {
    // The sparge water takes the info line while its heater is enabled.
    char a[16], b[16];
    fmtTemp(a, sizeof(a), st.spargeTempC);
    fmtTemp(b, sizeof(b), st.spargeSetC);
    snprintf(buf, n, "Sparge %s " LV_SYMBOL_RIGHT " %s%s", st.spargeTempOk ? a : "--", b, st.spargeHeating ? " heating" : "");
    return;
  }
  if (st.priming) {
    snprintf(buf, n, "Priming the pump");
  } else if (st.pumpResting) {
    snprintf(buf, n, "Pump rest");
  } else if (st.mode == Mode::Autotune) {
    snprintf(buf, n, "Cycle %u of %u", st.tuneCycles, st.tuneCyclesWanted);
  } else if (st.mode == Mode::Auto && st.phase == ::brew::Phase::Boil) {
    const auto& adds = controller::brewRecipe().additions;
    int next = st.lastAddition + 1;
    if (next < (int)adds.size()) snprintf(buf, n, "Next: %s\nat %u min", adds[next].name.c_str(), adds[next].atMinutes);
  } else if (st.mode == Mode::Auto && st.phase == ::brew::Phase::Mash) {
    if (st.step == 0) snprintf(buf, n, "Then: add malt");
    else if (st.step + 1 < st.stepCount) snprintf(buf, n, "Next: %s", seqName(controller::brewRecipe(), st.step + 1).c_str());
    else snprintf(buf, n, "Then: remove malt");
  }
}

void updateChart() {
  if (ui::historySamples() == chartSamples) return;
  chartSamples = ui::historySamples();
  int32_t lo = INT32_MAX, hi = INT32_MIN;
  int32_t* tp = lv_chart_get_y_array(chart, tempSeries);
  int32_t* sp = lv_chart_get_y_array(chart, spSeries);
  for (int i = 0; i < kHistory; i++) {
    int16_t t = historyTemp(i);
    int16_t s = historySetpoint(i);
    tp[i] = t == INT16_MIN ? LV_CHART_POINT_NONE : t;
    sp[i] = s == INT16_MIN ? LV_CHART_POINT_NONE : s;
    if (t != INT16_MIN) {
      if (t < lo) lo = t;
      if (t > hi) hi = t;
    }
    if (s != INT16_MIN) {
      if (s < lo) lo = s;
      if (s > hi) hi = s;
    }
  }
  if (lo > hi) lo = 0, hi = 1000;
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo - 10, hi + 10);
  lv_chart_refresh(chart);
}

}  // namespace

void create(lv_obj_t* root) {
  const controller::Status& st = controller::status();
  builtFor = st.mode;
  overlay = nullptr;
  shownPrompt = -1;
  chartSamples = 0;
  pauseBtn = heatBtn = pumpBtn = nullptr;
  chips.clear();
  shownChip = -2;
  // Layout (268 px): cards 138, strip 28 (automatic mode only), chart, actions 50.
  const int cardH = 138;
  const bool strip = st.mode == Mode::Auto;
  const int chartY = cardH + kit::kPad + (strip ? 28 + 4 : 0);
  const int chartH = 268 - 50 - kit::kPad - chartY;

  // ---- Main card: step, temperature, setpoint, timer
  lv_obj_t* main = kit::card(root);
  lv_obj_set_size(main, 292, cardH);
  lv_obj_set_pos(main, 0, 0);
  kit::onClick(main, editSetpoint);
  stepLabel = kit::label(main, "", kit::kNormal);
  lv_label_set_long_mode(stepLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(stepLabel, LV_PCT(100));
  tempLabel = kit::label(main, "--.-", kit::kHuge);
  lv_obj_set_pos(tempLabel, 0, 26);
  spLabel = kit::label(main, "", kit::kNormal);
  lv_obj_set_style_text_color(spLabel, kit::dim(), 0);
  lv_obj_align(spLabel, LV_ALIGN_TOP_RIGHT, 0, 44);

  lv_obj_t* tbox = kit::box(main);
  lv_obj_set_size(tbox, LV_PCT(100), 40);
  lv_obj_align(tbox, LV_ALIGN_BOTTOM_LEFT, 0, -8);
  lv_obj_add_flag(tbox, LV_OBJ_FLAG_CLICKABLE);
  kit::onClick(tbox, editTimer);
  timerLabel = kit::label(tbox, "", kit::kLarge);
  lv_obj_align(timerLabel, LV_ALIGN_LEFT_MID, 0, 0);
  timerNote = kit::label(tbox, "", kit::kSmall);
  lv_obj_set_style_text_color(timerNote, kit::dim(), 0);
  lv_obj_align(timerNote, LV_ALIGN_RIGHT_MID, 0, 0);
  bar = lv_bar_create(main);
  lv_obj_set_size(bar, LV_PCT(100), 6);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(bar, 0, 1000);

  // ---- Status card: heater, pump, fan, info
  lv_obj_t* side = kit::card(root);
  lv_obj_set_size(side, 468 - 292 - kit::kPad, cardH);
  lv_obj_set_pos(side, 292 + kit::kPad, 0);
  lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(side, 4, 0);
  heaterLabel = kit::label(side, "", kit::kNormal);
  lv_obj_add_flag(heaterLabel, LV_OBJ_FLAG_CLICKABLE);
  kit::onClick(heaterLabel, editBoilPower);
  pumpLabel = kit::label(side, "", kit::kNormal);
  fanLabel = kit::label(side, "", kit::kSmall);
  infoLabel = kit::label(side, "", kit::kSmall);
  lv_label_set_long_mode(infoLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(infoLabel, LV_PCT(100));

  // ---- Step strip
  if (strip) {
    lv_obj_t* s = kit::box(root);
    lv_obj_set_size(s, LV_PCT(100), 28);
    lv_obj_set_pos(s, 0, cardH + kit::kPad);
    lv_obj_set_flex_flow(s, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s, 4, 0);
    lv_obj_add_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s, LV_SCROLLBAR_MODE_OFF);
    buildStrip(s);
  }

  // ---- Chart
  chart = lv_chart_create(root);
  lv_obj_set_size(chart, LV_PCT(100), chartH);
  lv_obj_set_pos(chart, 0, chartY);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, kHistory);
  lv_chart_set_div_line_count(chart, 3, 0);
  lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);  // no point markers
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_pad_all(chart, 2, 0);
  lv_obj_set_style_bg_color(chart, kit::panel(), 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  spSeries = lv_chart_add_series(chart, kit::dim(), LV_CHART_AXIS_PRIMARY_Y);
  tempSeries = lv_chart_add_series(chart, kit::heat(), LV_CHART_AXIS_PRIMARY_Y);

  // ---- Actions
  lv_obj_t* actions = kit::row(root);
  lv_obj_set_align(actions, LV_ALIGN_BOTTOM_MID);
  switch (st.mode) {
    case Mode::Auto:
      pauseBtn = actionButton(actions, LV_SYMBOL_PAUSE, []() { controller::pauseResume(); });
      pumpBtn = actionButton(actions, LV_SYMBOL_REFRESH " Pump", []() { controller::pumpToggle(); });
      actionButton(actions, "+5 min", addFiveMinutes);
      holdButton(actions, LV_SYMBOL_NEXT " Skip", "Hold to skip the step", []() { controller::skip(); });
      kit::setColor(holdButton(actions, LV_SYMBOL_STOP " Stop", "Hold to stop the brew", []() { controller::stop(); }),
                    kit::bad());
      break;
    case Mode::Manual:
      heatBtn = actionButton(actions, LV_SYMBOL_CHARGE " Heat", []() { controller::heaterToggle(); });
      pumpBtn = actionButton(actions, LV_SYMBOL_REFRESH " Pump", []() { controller::pumpToggle(); });
      actionButton(actions, LV_SYMBOL_BELL " Timer", editTimer);
      kit::setColor(holdButton(actions, LV_SYMBOL_STOP " Stop", "Hold to stop", []() { controller::stop(); }),
                    kit::bad());
      break;
    case Mode::Autotune:
      pumpBtn = actionButton(actions, LV_SYMBOL_REFRESH " Pump", []() { controller::pumpToggle(); });
      kit::setColor(holdButton(actions, LV_SYMBOL_STOP " Stop", "Hold to stop autotune", []() { controller::stop(); }),
                    kit::bad());
      break;
    case Mode::Idle:
      actionButton(actions, LV_SYMBOL_HOME " Home", []() { go(Screen::Home); });
      break;
  }

  update();
}

void update() {
  const controller::Status& st = controller::status();
  lv_obj_t* root = lv_obj_get_parent(chart);
  if (st.mode != builtFor) {
    if (st.mode != Mode::Idle) go(Screen::Brew);  // rebuild for the new mode
    return;
  }

  char buf[96];
  stepText(buf, sizeof(buf), st);
  lv_label_set_text(stepLabel, buf);
  lv_obj_set_style_text_color(stepLabel, st.faults ? kit::bad() : st.paused ? kit::warn() : lv_color_white(), 0);

  if (st.tempOk) fmtTemp(buf, sizeof(buf), st.tempC);
  else snprintf(buf, sizeof(buf), "--.-%s", unit());
  lv_label_set_text(tempLabel, buf);
  lv_obj_set_style_text_color(tempLabel, st.reached ? kit::good() : lv_color_white(), 0);

  if (st.mode == Mode::Idle) {
    lv_label_set_text(spLabel, "");
  } else {
    char t[16];
    fmtTemp(t, sizeof(t), st.setpointC);
    snprintf(buf, sizeof(buf), LV_SYMBOL_RIGHT " %s", t);
    lv_label_set_text(spLabel, buf);
  }

  // Timer
  const char* note = "";
  uint32_t shown = 0;
  int32_t progress = 0;
  if (st.mode == Mode::Auto) {
    bool countsUp = st.phase == ::brew::Phase::Cooling;
    shown = countsUp ? st.elapsedMs : st.remainingMs;
    uint32_t total = st.elapsedMs + st.remainingMs;
    progress = total ? (int32_t)((uint64_t)st.elapsedMs * 1000 / total) : 0;
    if (st.prompt != ::brew::Prompt::None) note = "waiting for you";
    else if (countsUp) note = "elapsed";
    else if (!st.timerCounting && total) note = "heating up";
    else if (total) note = "left";
  } else if (st.mode == Mode::Manual) {
    shown = st.manualCountdown ? st.remainingMs : st.elapsedMs;
    note = st.manualCountdown ? "countdown" : (st.reached ? "at temperature" : "heating up");
  }
  if (st.mode == Mode::Autotune) lv_label_set_text(timerLabel, "--:--");
  else {
    fmtTime(buf, sizeof(buf), shown);
    lv_label_set_text(timerLabel, buf);
  }
  lv_label_set_text(timerNote, note);
  lv_bar_set_value(bar, progress, LV_ANIM_OFF);

  // Heater / pump / fan
  snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %s %.0f%%", controller::zoneName(st.heaterZone), st.heaterDuty);
  if (st.heaterZone == Heater::Zone::Boil) snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " boil %u%%", st.boilPowerPct);
  lv_label_set_text(heaterLabel, buf);
  lv_obj_set_style_text_color(heaterLabel, st.heaterOn ? kit::heat() : kit::dim(), 0);

  if (st.pumpOn) snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " on%s", st.pumpOverride ? " (manual)" : "");
  else snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH " off%s", st.pumpOverride ? " (manual)" : "");
  lv_label_set_text(pumpLabel, buf);
  lv_obj_set_style_text_color(pumpLabel, st.pumpOn ? kit::pump() : kit::dim(), 0);

  snprintf(buf, sizeof(buf), "Fan %u%%%s", st.fanPct, st.sim ? "   SIM" : "");
  lv_label_set_text(fanLabel, buf);
  infoText(buf, sizeof(buf), st);
  lv_label_set_text(infoLabel, buf);

  // Buttons
  if (pauseBtn) kit::setText(pauseBtn, st.paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
  if (heatBtn) {
    kit::setText(heatBtn, st.manualHeater ? LV_SYMBOL_CHARGE " Heat on" : LV_SYMBOL_CHARGE " Heat off");
    kit::setColor(heatBtn, st.manualHeater ? kit::heat() : kit::accent());
  }
  if (pumpBtn) kit::setColor(pumpBtn, st.pumpOn ? lv_palette_darken(LV_PALETTE_CYAN, 2) : kit::accent());

  updateChart();
  updateStrip(st);
  updateOverlay(root, st);
}

}  // namespace brew
}  // namespace ui
