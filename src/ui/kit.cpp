// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kit.h"

#include <math.h>
#include <stdio.h>

#include <vector>

namespace kit {

const lv_font_t* const kSmall = &lv_font_montserrat_14;
const lv_font_t* const kNormal = &lv_font_montserrat_20;
const lv_font_t* const kLarge = &lv_font_montserrat_28;
const lv_font_t* const kHuge = &lv_font_montserrat_48;

lv_color_t accent() { return lv_palette_main(LV_PALETTE_BLUE); }
lv_color_t heat() { return lv_palette_main(LV_PALETTE_DEEP_ORANGE); }
lv_color_t pump() { return lv_palette_main(LV_PALETTE_CYAN); }
lv_color_t good() { return lv_palette_main(LV_PALETTE_GREEN); }
lv_color_t warn() { return lv_palette_main(LV_PALETTE_AMBER); }
lv_color_t bad() { return lv_palette_main(LV_PALETTE_RED); }
lv_color_t dim() { return lv_palette_main(LV_PALETTE_GREY); }
lv_color_t panel() { return lv_color_hex(0x1E2329); }

namespace {

struct Handler {
  Fn fn;
};

void runHandler(lv_event_t* e) { static_cast<Handler*>(lv_event_get_user_data(e))->fn(); }
void freeHandler(lv_event_t* e) { delete static_cast<Handler*>(lv_event_get_user_data(e)); }

void runLater(void* p) {
  Fn* fn = static_cast<Fn*>(p);
  (*fn)();
  delete fn;
}

std::vector<lv_obj_t*> popups;

void forget(lv_obj_t* bg) {
  for (size_t i = 0; i < popups.size(); i++) {
    if (popups[i] == bg) {
      popups.erase(popups.begin() + i);
      return;
    }
  }
}

// Modal background with a centred card; returns the card.
lv_obj_t* popupCard(lv_obj_t*& bgOut, int width) {
  lv_obj_t* bg = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_60, 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);  // blocks touches below
  popups.push_back(bg);
  lv_obj_add_event_cb(bg, [](lv_event_t* e) { forget(static_cast<lv_obj_t*>(lv_event_get_target(e))); },
                      LV_EVENT_DELETE, nullptr);

  lv_obj_t* c = lv_obj_create(bg);
  lv_obj_set_width(c, width);
  lv_obj_set_height(c, LV_SIZE_CONTENT);
  lv_obj_center(c);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(c, 12, 0);
  lv_obj_set_style_pad_row(c, 10, 0);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  bgOut = bg;
  return c;
}

void closeLater(lv_obj_t* bg) { lv_obj_delete_async(bg); }

void formatNumber(char* buf, size_t len, float v, int decimals, const char* unit) {
  snprintf(buf, len, "%.*f%s%s", decimals, v, unit && *unit ? " " : "", unit ? unit : "");
}

}  // namespace

void on(lv_obj_t* obj, lv_event_code_t code, Fn fn) {
  Handler* h = new Handler{fn};
  lv_obj_add_event_cb(obj, runHandler, code, h);
  lv_obj_add_event_cb(obj, freeHandler, LV_EVENT_DELETE, h);
}

void onClick(lv_obj_t* obj, Fn fn) { on(obj, LV_EVENT_CLICKED, fn); }

void later(Fn fn) { lv_async_call(runLater, new Fn(fn)); }

// ---- Layout ---------------------------------------------------------------

lv_obj_t* box(lv_obj_t* parent) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

lv_obj_t* row(lv_obj_t* parent) {
  lv_obj_t* r = box(parent);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, kPad, 0);
  return r;
}

lv_obj_t* column(lv_obj_t* parent) {
  lv_obj_t* c = box(parent);
  lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, kPad, 0);
  return c;
}

lv_obj_t* card(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_style_bg_color(c, panel(), 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 8, 0);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* scrollList(lv_obj_t* parent) {
  lv_obj_t* l = lv_obj_create(parent);
  lv_obj_remove_style_all(l);
  lv_obj_set_size(l, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(l, 4, 0);
  lv_obj_set_style_pad_right(l, 6, 0);
  lv_obj_set_scroll_dir(l, LV_DIR_VER);
  return l;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  if (font) lv_obj_set_style_text_font(l, font, 0);
  return l;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, Fn fn, const lv_font_t* font) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, kRowH);
  lv_obj_set_style_pad_hor(b, 12, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font ? font : kNormal, 0);
  lv_obj_center(l);
  if (fn) onClick(b, fn);
  return b;
}

void setText(lv_obj_t* obj, const char* text) {
  if (lv_obj_check_type(obj, &lv_label_class)) {
    lv_label_set_text(obj, text);
  } else {
    lv_obj_t* l = lv_obj_get_child(obj, 0);
    if (l) lv_label_set_text(l, text);
  }
}

void setColor(lv_obj_t* b, lv_color_t c) { lv_obj_set_style_bg_color(b, c, 0); }

// ---- Form rows --------------------------------------------------------------

namespace {
lv_obj_t* formRow(lv_obj_t* list, const char* text) {
  lv_obj_t* r = row(list);
  lv_obj_set_style_min_height(r, kRowH, 0);
  lv_obj_t* l = label(r, text, kNormal);
  lv_obj_set_flex_grow(l, 1);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  return r;
}
}  // namespace

lv_obj_t* rowValue(lv_obj_t* list, const char* text, const char* value, Fn onTap) {
  lv_obj_t* r = formRow(list, text);
  lv_obj_t* b = button(r, value, onTap);
  lv_obj_set_style_min_width(b, 120, 0);
  lv_obj_set_style_bg_color(b, panel(), 0);
  return lv_obj_get_child(b, 0);
}

lv_obj_t* rowSwitch(lv_obj_t* list, const char* text, bool isOn, std::function<void(bool)> changed) {
  lv_obj_t* r = formRow(list, text);
  lv_obj_t* sw = lv_switch_create(r);
  lv_obj_set_size(sw, 64, 34);
  if (isOn) lv_obj_add_state(sw, LV_STATE_CHECKED);
  on(sw, LV_EVENT_VALUE_CHANGED, [sw, changed]() { changed(lv_obj_has_state(sw, LV_STATE_CHECKED)); });
  return sw;
}

lv_obj_t* rowChoice(lv_obj_t* list, const char* text, const char* options, int selected,
                    std::function<void(int)> changed) {
  lv_obj_t* r = formRow(list, text);
  lv_obj_t* dd = lv_dropdown_create(r);
  lv_obj_set_width(dd, 150);
  lv_obj_set_style_text_font(dd, kNormal, 0);
  lv_dropdown_set_options(dd, options);
  lv_dropdown_set_selected(dd, selected < 0 ? 0 : selected);
  lv_obj_t* l = lv_dropdown_get_list(dd);
  if (l) lv_obj_set_style_text_font(l, kNormal, 0);
  on(dd, LV_EVENT_VALUE_CHANGED, [dd, changed]() { changed((int)lv_dropdown_get_selected(dd)); });
  return dd;
}

lv_obj_t* rowHeader(lv_obj_t* list, const char* text) {
  lv_obj_t* l = label(list, text, kSmall);
  lv_obj_set_style_text_color(l, accent(), 0);
  lv_obj_set_style_pad_top(l, 8, 0);
  return l;
}

// ---- Popups -------------------------------------------------------------------

void editNumber(const char* title, float value, float min, float max, float step, int decimals,
                const char* unit, std::function<void(float)> ok) {
  struct State {
    float v, min, max, step;
    int decimals;
    std::string unit;
    lv_obj_t* label;
  };
  State* st = new State{value, min, max, step > 0 ? step : 1, decimals, unit ? unit : "", nullptr};

  lv_obj_t* bg;
  lv_obj_t* c = popupCard(bg, 400);
  lv_obj_add_event_cb(bg, [](lv_event_t* e) { delete static_cast<State*>(lv_event_get_user_data(e)); },
                      LV_EVENT_DELETE, st);
  label(c, title, kNormal);
  st->label = label(c, "", kHuge);

  auto show = [st]() {
    char buf[32];
    formatNumber(buf, sizeof(buf), st->v, st->decimals, st->unit.c_str());
    lv_label_set_text(st->label, buf);
  };
  auto bump = [st, show](float times) {
    float v = st->v + st->step * times;
    v = roundf(v / st->step) * st->step;  // no drift from repeated float adds
    st->v = v < st->min ? st->min : (v > st->max ? st->max : v);
    show();
  };
  show();

  lv_obj_t* r = row(c);
  const float steps[4] = {-10, -1, 1, 10};
  const char* texts[4] = {LV_SYMBOL_MINUS LV_SYMBOL_MINUS, LV_SYMBOL_MINUS, LV_SYMBOL_PLUS,
                          LV_SYMBOL_PLUS LV_SYMBOL_PLUS};
  for (int i = 0; i < 4; i++) {
    float t = steps[i];
    lv_obj_t* b = button(r, texts[i], [bump, t]() { bump(t); });
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 56);
    on(b, LV_EVENT_LONG_PRESSED_REPEAT, [bump, t]() { bump(t); });
  }

  r = row(c);
  lv_obj_t* cancel = button(r, LV_SYMBOL_CLOSE " Cancel", [bg]() { closeLater(bg); });
  lv_obj_set_flex_grow(cancel, 1);
  lv_obj_t* okBtn = button(r, LV_SYMBOL_OK " OK", [bg, st, ok]() {
    float v = st->v;
    closeLater(bg);
    ok(v);
  });
  lv_obj_set_flex_grow(okBtn, 1);
  setColor(okBtn, good());
}

void editText(const char* title, const std::string& value,
              std::function<void(const std::string&)> ok) {
  lv_obj_t* bg = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_80, 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  popups.push_back(bg);
  lv_obj_add_event_cb(bg, [](lv_event_t* e) { forget(static_cast<lv_obj_t*>(lv_event_get_target(e))); },
                      LV_EVENT_DELETE, nullptr);

  lv_obj_t* t = label(bg, title, kNormal);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 10, 8);
  lv_obj_t* ta = lv_textarea_create(bg);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_max_length(ta, 32);
  lv_textarea_set_text(ta, value.c_str());
  lv_obj_set_style_text_font(ta, kNormal, 0);
  lv_obj_set_width(ta, 460);
  lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 36);

  lv_obj_t* kb = lv_keyboard_create(bg);
  lv_obj_set_size(kb, LV_PCT(100), 200);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, ta);
  on(kb, LV_EVENT_READY, [bg, ta, ok]() {
    std::string v = lv_textarea_get_text(ta);
    closeLater(bg);
    ok(v);
  });
  on(kb, LV_EVENT_CANCEL, [bg]() { closeLater(bg); });
}

void confirm(const char* text, const char* yesText, Fn yes, Fn no) {
  lv_obj_t* bg;
  lv_obj_t* c = popupCard(bg, 420);
  lv_obj_t* l = label(c, text, kNormal);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_t* r = row(c);
  lv_obj_t* nb = button(r, LV_SYMBOL_CLOSE " No", [bg, no]() {
    closeLater(bg);
    if (no) no();
  });
  lv_obj_set_flex_grow(nb, 1);
  lv_obj_t* yb = button(r, yesText, [bg, yes]() {
    closeLater(bg);
    yes();
  });
  lv_obj_set_flex_grow(yb, 1);
  setColor(yb, good());
}

void choose(const char* title, const std::vector<std::string>& options,
            std::function<void(int)> picked) {
  lv_obj_t* bg;
  lv_obj_t* c = popupCard(bg, 360);
  lv_obj_set_style_max_height(c, 300, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  label(c, title, kNormal);
  for (size_t i = 0; i < options.size(); i++) {
    int idx = (int)i;
    lv_obj_t* b = button(c, options[i].c_str(), [bg, picked, idx]() {
      closeLater(bg);
      picked(idx);
    });
    lv_obj_set_width(b, LV_PCT(100));
  }
  lv_obj_t* cancel = button(c, LV_SYMBOL_CLOSE " Cancel", [bg]() { closeLater(bg); });
  lv_obj_set_width(cancel, LV_PCT(100));
  setColor(cancel, dim());
}

void message(const char* title, const char* text, Fn ok) {
  lv_obj_t* bg;
  lv_obj_t* c = popupCard(bg, 420);
  label(c, title, kLarge);
  lv_obj_t* l = label(c, text, kNormal);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_t* b = button(c, LV_SYMBOL_OK " OK", [bg, ok]() {
    closeLater(bg);
    if (ok) ok();
  });
  lv_obj_set_width(b, LV_PCT(100));
  setColor(b, good());
}

void toast(const char* text) {
  lv_obj_t* t = lv_label_create(lv_layer_top());
  lv_label_set_text(t, text);
  lv_obj_set_style_text_font(t, kNormal, 0);
  lv_obj_set_style_bg_color(t, lv_color_hex(0x333A44), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(t, 10, 0);
  lv_obj_set_style_radius(t, 8, 0);
  lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -70);
  lv_obj_delete_delayed(t, 2500);
}

void closePopups() {
  std::vector<lv_obj_t*> all = popups;
  for (lv_obj_t* p : all) lv_obj_delete(p);
}

}  // namespace kit
