// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small LVGL toolkit for the MashTouch screens: fonts, colours, buttons
// and form rows with C++ callbacks, and modal popups (number editor, text
// editor, confirm, toast). Touch targets are at least 44 px high.
#pragma once
#include <lvgl.h>

#include <functional>
#include <string>
#include <vector>

namespace kit {

using Fn = std::function<void()>;

// ---- Look ---------------------------------------------------------------
extern const lv_font_t* const kSmall;   // 14
extern const lv_font_t* const kNormal;  // 20
extern const lv_font_t* const kLarge;   // 28
extern const lv_font_t* const kHuge;    // 48

lv_color_t accent();
lv_color_t heat();
lv_color_t pump();
lv_color_t good();
lv_color_t warn();
lv_color_t bad();
lv_color_t dim();
lv_color_t panel();

constexpr int kRowH = 48;
constexpr int kPad = 6;

// ---- Callbacks ----------------------------------------------------------
// Runs fn on the event; fn is freed with the object.
void on(lv_obj_t* obj, lv_event_code_t code, Fn fn);
void onClick(lv_obj_t* obj, Fn fn);
// Runs fn after the current event has finished (safe to delete widgets).
void later(Fn fn);

// ---- Layout -------------------------------------------------------------
lv_obj_t* box(lv_obj_t* parent);  // invisible container, no padding
lv_obj_t* row(lv_obj_t* parent);  // full-width flex row, content height
lv_obj_t* column(lv_obj_t* parent);
lv_obj_t* card(lv_obj_t* parent);  // panel with background and padding
lv_obj_t* scrollList(lv_obj_t* parent);  // full-size scrolling column

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font = nullptr);
lv_obj_t* button(lv_obj_t* parent, const char* text, Fn onClick = Fn(),
                 const lv_font_t* font = nullptr);
void setText(lv_obj_t* buttonOrLabel, const char* text);
void setColor(lv_obj_t* button, lv_color_t c);

// ---- Form rows (label left, control right) ------------------------------
// Value shown on a button; tap runs onTap. Returns the value label.
lv_obj_t* rowValue(lv_obj_t* list, const char* text, const char* value, Fn onTap);
lv_obj_t* rowSwitch(lv_obj_t* list, const char* text, bool on, std::function<void(bool)> changed);
// options separated by '\n'
lv_obj_t* rowChoice(lv_obj_t* list, const char* text, const char* options, int selected,
                    std::function<void(int)> changed);
lv_obj_t* rowHeader(lv_obj_t* list, const char* text);

// ---- Popups (on the top layer, modal) -----------------------------------
// Number editor with -/+ (hold to repeat) and coarse --/++ (x10).
void editNumber(const char* title, float value, float min, float max, float step, int decimals,
                const char* unit, std::function<void(float)> ok);
// On-screen keyboard.
void editText(const char* title, const std::string& value, std::function<void(const std::string&)> ok);
void confirm(const char* text, const char* yesText, Fn yes, Fn no = Fn());
// A list of choices plus Cancel; picked gets the index.
void choose(const char* title, const std::vector<std::string>& options, std::function<void(int)> picked);
// Information with an OK button.
void message(const char* title, const char* text, Fn ok = Fn());
// Short notice that disappears by itself.
void toast(const char* text);
// Close all popups now (not from inside a popup event).
void closePopups();

}  // namespace kit
