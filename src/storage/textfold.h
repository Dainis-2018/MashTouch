// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text cleanup for imported names. The screen fonts contain ASCII and the
// degree sign only, so accented Latin letters are folded to their base
// letter (a with macron -> a, s with caron -> s, sharp s -> ss), typographic
// quotes and dashes to ASCII, anything else to '?'.
#pragma once
#include <stdint.h>

#include <string>

namespace textfold {

// Input in UTF-8.
std::string fromUtf8(const std::string& s);
// Input in ISO-8859-1 / Windows-1252 (one byte per character).
std::string fromLatin1(const std::string& s);

}  // namespace textfold
