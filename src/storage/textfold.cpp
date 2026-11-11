// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "textfold.h"

namespace textfold {
namespace {

// U+00C0..U+00FF, one base letter each (specials handled before the table).
const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
// U+0100..U+017F (Latin Extended-A: Baltic, Central European...).
const char kExtA[] =
    "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiJjJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
static_assert(sizeof(kLatin1) == 64 + 1, "one letter per code point U+00C0..U+00FF");
static_assert(sizeof(kExtA) == 128 + 1, "one letter per code point U+0100..U+017F");

void append(std::string& out, uint32_t cp) {
  if (cp == '\t' || cp == '\n' || cp == '\r') {
    out += ' ';
    return;
  }
  if (cp < 0x20) return;
  if (cp < 0x80) {
    out += (char)cp;
    return;
  }
  switch (cp) {
    case 0xA0: out += ' '; return;             // no-break space
    case 0xB0: out += "\xC2\xB0"; return;      // degree sign: in the fonts
    case 0xB5: out += 'u'; return;             // micro
    case 0xBD: out += "1/2"; return;
    case 0xC6: out += "AE"; return;
    case 0xDF: out += "ss"; return;
    case 0xE6: out += "ae"; return;
    case 0x152: out += "OE"; return;
    case 0x153: out += "oe"; return;
    case 0x2018: case 0x2019: case 0x201A: out += '\''; return;
    case 0x201C: case 0x201D: case 0x201E: out += '"'; return;
    case 0x2013: case 0x2014: out += '-'; return;
    case 0x2026: out += "..."; return;
    case 0x20AC: out += "EUR"; return;
    default: break;
  }
  if (cp >= 0xC0 && cp <= 0xFF) out += kLatin1[cp - 0xC0];
  else if (cp >= 0x100 && cp <= 0x17F) out += kExtA[cp - 0x100];
  else out += '?';
}

// Windows-1252 code points for bytes 0x80..0x9F (0 = unused).
const uint16_t kCp1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                              0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                              0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

}  // namespace

std::string fromLatin1(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    uint32_t cp = c;
    if (c >= 0x80 && c <= 0x9F) cp = kCp1252[c - 0x80] ? kCp1252[c - 0x80] : '?';
    if (cp == 0x178) {  // Y with diaeresis is outside the Extended-A table order
      out += 'Y';
      continue;
    }
    append(out, cp);
  }
  return out;
}

std::string fromUtf8(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = s[i];
    uint32_t cp;
    int extra;
    if (c < 0x80) {
      cp = c;
      extra = 0;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else {
      out += '?';  // stray continuation byte
      i++;
      continue;
    }
    if (extra && i + extra >= s.size()) {  // truncated sequence
      out += '?';
      break;
    }
    bool ok = true;
    for (int k = 1; k <= extra; k++) {
      unsigned char cc = s[i + k];
      if ((cc & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) {
      out += '?';
      i++;
      continue;
    }
    append(out, cp);
    i += extra + 1;
  }
  return out;
}

}  // namespace textfold
