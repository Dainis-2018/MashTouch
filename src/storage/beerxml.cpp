// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "beerxml.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include "textfold.h"

namespace beerxml {
namespace {

using settings::Addition;
using settings::MashStep;
using settings::Recipe;

constexpr size_t kMaxAdditions = 32;  // brew::Process alarm bitmask

std::string upper(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = (char)toupper((unsigned char)c);
  return out;
}

std::string lower(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = (char)tolower((unsigned char)c);
  return out;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

bool contains(const std::string& haystackLower, const char* needleLower) {
  return haystackLower.find(needleLower) != std::string::npos;
}

// Numbers: BeerXML uses '.', some exporters write ','.
double number(const std::string& s) {
  std::string t = s;
  std::replace(t.begin(), t.end(), ',', '.');
  return strtod(t.c_str(), nullptr);
}

void appendCodepoint(std::string& out, unsigned long cp) {
  // Re-encode as UTF-8; folding happens afterwards for the whole text.
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xE0 | ((cp >> 12) & 0x0F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// Decodes &amp; &lt; &gt; &quot; &apos; &#NN; &#xNN;. Numeric references
// become UTF-8, so the result is UTF-8 only if the input was.
std::string entities(const std::string& s, bool& hasNumeric) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out += '&';
      continue;
    }
    std::string e = s.substr(i + 1, semi - i - 1);
    if (e == "amp") out += '&';
    else if (e == "lt") out += '<';
    else if (e == "gt") out += '>';
    else if (e == "quot") out += '"';
    else if (e == "apos") out += '\'';
    else if (!e.empty() && e[0] == '#') {
      unsigned long cp = (e.size() > 1 && (e[1] == 'x' || e[1] == 'X')) ? strtoul(e.c_str() + 2, nullptr, 16)
                                                                         : strtoul(e.c_str() + 1, nullptr, 10);
      appendCodepoint(out, cp);
      hasNumeric = true;
    } else {
      out += s.substr(i, semi - i + 1);  // unknown entity: keep as is
    }
    i = semi;
  }
  return out;
}

// ---- Mapping ----------------------------------------------------------------

class Importer {
 public:
  Importer(Result& res, bool latin1) : res_(res), latin1_(latin1) {}

  void start(const std::string& tag, const std::string& parent) {
    if (tag == "RECIPE" && (parent.empty() || parent == "RECIPES")) {
      inRecipe_ = true;
      rec_ = Recipe();
      rec_.additions.clear();
      boil_ = -1;
      skipped_.clear();
    } else if (!inRecipe_) {
      return;
    } else if (tag == "MASH_STEP") {
      step_ = MashStep();
      haveTemp_ = false;
    } else if (tag == "HOP" && parent == "HOPS") {
      item_ = Item();
      kind_ = Kind::Hop;
    } else if (tag == "MISC" && parent == "MISCS") {
      item_ = Item();
      kind_ = Kind::Misc;
    }
  }

  void end(const std::string& tag, const std::string& parent, const std::string& rawText) {
    if (!inRecipe_) return;
    if (parent == "RECIPE") {
      if (tag == "NAME") rec_.name = text(rawText);
      else if (tag == "BOIL_TIME") boil_ = (int)lround(number(rawText));
    } else if (parent == "MASH_STEP") {
      if (tag == "NAME") step_.name = text(rawText);
      else if (tag == "STEP_TEMP") {
        step_.tempC = (float)number(rawText);
        haveTemp_ = true;
      } else if (tag == "STEP_TIME") {
        long m = lround(number(rawText));
        step_.minutes = (uint16_t)(m < 0 ? 0 : m);
      }
    } else if ((parent == "HOP" && kind_ == Kind::Hop) || (parent == "MISC" && kind_ == Kind::Misc)) {
      if (tag == "NAME") item_.name = text(rawText);
      else if (tag == "AMOUNT") item_.amount = number(rawText);
      else if (tag == "USE") item_.use = lower(text(rawText));
      else if (tag == "TIME") item_.time = number(rawText);
      else if (tag == "DISPLAY_AMOUNT") item_.display = text(rawText);
      else if (tag == "AMOUNT_IS_WEIGHT") item_.isWeight = lower(trim(rawText)) == "true";
    }

    if (tag == "MASH_STEP") {
      if (haveTemp_) steps_.push_back(step_);
    } else if (tag == "HOP" && kind_ == Kind::Hop) {
      commitHop();
      kind_ = Kind::None;
    } else if (tag == "MISC" && kind_ == Kind::Misc) {
      commitMisc();
      kind_ = Kind::None;
    } else if (tag == "RECIPE") {
      finish();
      inRecipe_ = false;
    }
  }

 private:
  enum class Kind : uint8_t { None, Hop, Misc };
  struct Item {
    std::string name, use, display;
    double amount = 0;
    double time = 0;
    bool isWeight = true;
  };
  struct Pending {  // addition before the boil time is known
    Addition a;
    int where;  // 0: at TIME, 1: boil start, 2: flameout
  };

  std::string text(const std::string& raw) {
    bool numeric = false;
    std::string s = entities(raw, numeric);
    // Numeric references in a Latin-1 file produce UTF-8 bytes; fold those
    // texts as UTF-8 (rare: exporters write Latin-1 bytes directly).
    return trim(latin1_ && !numeric ? textfold::fromLatin1(s) : textfold::fromUtf8(s));
  }

  void skip(const Item& it) {
    skipped_.push_back(it.name + " (" + (it.use.empty() ? "no use" : it.use) + ")");
  }

  void commitHop() {
    Pending p;
    p.a.name = item_.name.empty() ? "Hops" : item_.name;
    p.a.amount = (float)(lround(item_.amount * 10000) / 10.0);  // kg -> g, 0.1 g
    p.a.unit = "g";
    if (item_.use == "boil") {
      p.where = 0;
    } else if (contains(item_.use, "first wort")) {
      p.where = 1;
      p.a.name += " (first wort)";
    } else if (item_.use == "aroma") {
      p.where = 2;
      p.a.name += " (aroma)";
    } else {
      skip(item_);  // dry hop, mash
      return;
    }
    p.a.atMinutes = (uint16_t)(item_.time > 0 ? lround(item_.time) : 0);
    pending_.push_back(p);
  }

  void commitMisc() {
    if (item_.use != "boil") {
      skip(item_);
      return;
    }
    Pending p;
    p.where = 0;
    p.a.name = item_.name.empty() ? "Addition" : item_.name;
    p.a.atMinutes = (uint16_t)(item_.time > 0 ? lround(item_.time) : 0);
    // DISPLAY_AMOUNT ("150 g", "2 each") keeps the unit the brewer chose.
    char* end = nullptr;
    std::string ds = item_.display;
    std::replace(ds.begin(), ds.end(), ',', '.');
    double v = strtod(ds.c_str(), &end);
    if (!item_.display.empty() && end != ds.c_str()) {
      p.a.amount = (float)v;
      p.a.unit = trim(std::string(end));
      if (p.a.unit.empty()) p.a.unit = "pcs";
    } else if (item_.isWeight) {
      p.a.amount = (float)(item_.amount * 1000);
      p.a.unit = "g";
    } else {
      p.a.amount = (float)(item_.amount * 1000);
      p.a.unit = "ml";
    }
    pending_.push_back(p);
  }

  void finish() {
    Recipe& r = rec_;
    if (r.name.empty()) r.name = "Imported recipe";
    r.boilMinutes = (uint16_t)(boil_ >= 0 ? (boil_ > 600 ? 600 : boil_) : 60);

    // First step = mash in; the last step named "...out" = mash out
    // (arrangeMash decides by temperature when none is named so).
    int outIndex = -1;
    for (size_t i = 0; i < steps_.size(); i++) {
      if (steps_[i].name.empty()) steps_[i].name = "Step " + std::to_string(i + 1);
      if (i > 0 && contains(lower(steps_[i].name), "out")) outIndex = (int)i;
    }
    settings::arrangeMash(r, steps_, outIndex);

    for (const Pending& p : pending_) {
      Addition a = p.a;
      if (p.where == 1) a.atMinutes = r.boilMinutes;
      else if (p.where == 2) a.atMinutes = 0;
      if (a.atMinutes > r.boilMinutes) a.atMinutes = r.boilMinutes;
      r.additions.push_back(a);
    }
    std::stable_sort(r.additions.begin(), r.additions.end(),
                     [](const Addition& x, const Addition& y) { return x.atMinutes > y.atMinutes; });
    size_t dropped = 0;
    if (r.additions.size() > kMaxAdditions) {
      dropped = r.additions.size() - kMaxAdditions;
      r.additions.resize(kMaxAdditions);
    }

    char buf[160];
    if (r.mash)
      snprintf(buf, sizeof(buf), "%s: mash in %.1f C, %u rest(s), mash out %.1f C, boil %u min, %u additions",
               r.name.c_str(), r.mashIn.tempC, (unsigned)r.rests.size(), r.mashOut.tempC, r.boilMinutes,
               (unsigned)r.additions.size());
    else
      snprintf(buf, sizeof(buf), "%s: no mash, boil %u min, %u additions", r.name.c_str(), r.boilMinutes,
               (unsigned)r.additions.size());
    std::string note = buf;
    if (dropped) note += "; " + std::to_string(dropped) + " additions over the limit of 32 dropped";
    if (!skipped_.empty()) {
      note += "; not for brew day: ";
      for (size_t i = 0; i < skipped_.size(); i++) note += (i ? ", " : "") + skipped_[i];
    }
    res_.notes.push_back(note);
    res_.recipes.push_back(r);

    steps_.clear();
    pending_.clear();
  }

  Result& res_;
  bool latin1_;
  bool inRecipe_ = false;
  Recipe rec_;
  int boil_ = -1;
  MashStep step_;
  bool haveTemp_ = false;
  std::vector<MashStep> steps_;
  Item item_;
  Kind kind_ = Kind::None;
  std::vector<Pending> pending_;
  std::vector<std::string> skipped_;
};

// ---- Tag stream -------------------------------------------------------------

bool startsWith(const std::string& s, size_t at, const char* p) { return s.compare(at, strlen(p), p) == 0; }

bool isLatin1Prolog(const std::string& prolog) {
  std::string p = lower(prolog);
  return contains(p, "8859-1") || contains(p, "1252") || contains(p, "latin1") || contains(p, "latin-1");
}

}  // namespace

bool parse(const std::string& xml, Result& out, std::string& err) {
  size_t i = 0, n = xml.size();
  if (startsWith(xml, 0, "\xEF\xBB\xBF")) i = 3;  // UTF-8 byte order mark

  // The prolog decides the encoding (BeerXML files are often ISO-8859-1).
  bool latin1 = false;
  size_t p = xml.find("<?xml", i);
  if (p != std::string::npos && p < i + 64) {
    size_t e = xml.find("?>", p);
    if (e != std::string::npos) latin1 = isLatin1Prolog(xml.substr(p, e - p));
  }

  Importer imp(out, latin1);
  std::vector<std::string> stack;
  std::string text;
  while (i < n) {
    char c = xml[i];
    if (c != '<') {
      text += c;
      i++;
      continue;
    }
    if (startsWith(xml, i, "<?")) {
      size_t e = xml.find("?>", i);
      if (e == std::string::npos) break;
      i = e + 2;
      continue;
    }
    if (startsWith(xml, i, "<!--")) {
      size_t e = xml.find("-->", i);
      if (e == std::string::npos) break;
      i = e + 3;
      continue;
    }
    if (startsWith(xml, i, "<![CDATA[")) {
      size_t e = xml.find("]]>", i);
      if (e == std::string::npos) break;
      text += xml.substr(i + 9, e - i - 9);
      i = e + 3;
      continue;
    }
    if (startsWith(xml, i, "<!")) {
      size_t e = xml.find('>', i);
      if (e == std::string::npos) break;
      i = e + 1;
      continue;
    }

    // Find the end of the tag, ignoring '>' inside quoted attribute values.
    size_t e = i + 1;
    char quote = 0;
    while (e < n && (quote || xml[e] != '>')) {
      if (quote && xml[e] == quote) quote = 0;
      else if (!quote && (xml[e] == '"' || xml[e] == '\'')) quote = xml[e];
      e++;
    }
    if (e >= n) {
      err = "the XML ends inside a tag (file cut off?)";
      return false;
    }

    if (xml[i + 1] == '/') {
      std::string name = upper(trim(xml.substr(i + 2, e - i - 2)));
      if (!stack.empty() && stack.back() == name) stack.pop_back();
      imp.end(name, stack.empty() ? "" : stack.back(), text);
    } else {
      size_t nameEnd = i + 1;
      while (nameEnd < e && !isspace((unsigned char)xml[nameEnd]) && xml[nameEnd] != '/') nameEnd++;
      std::string name = upper(xml.substr(i + 1, nameEnd - i - 1));
      bool selfClosing = xml[e - 1] == '/';
      std::string parent = stack.empty() ? "" : stack.back();
      imp.start(name, parent);
      if (selfClosing) imp.end(name, parent, "");
      else stack.push_back(name);
    }
    text.clear();
    i = e + 1;
  }

  if (out.recipes.empty()) {
    err = "no <RECIPE> found (is this a BeerXML file?)";
    return false;
  }
  return true;
}

}  // namespace beerxml
