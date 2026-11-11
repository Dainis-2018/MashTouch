// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BeerXML 1.0 import (Brewfather, BeerSmith, Brewer's Friend... exports).
// BeerXML is only an exchange format: recipes are converted once to the
// MashTouch recipe model and stored as JSON like any other recipe.
//
// Mapping:
//   RECIPE/NAME, BOIL_TIME          -> name, boil minutes
//   MASH_STEP (in order)             -> first = mash in, last one named
//                                      "...out" (or a last step >= 75 C) =
//                                      mash out, the others = rests;
//                                      STEP_TEMP, STEP_TIME (0 = no rest).
//                                      No mash out: one is added at the last
//                                      temperature, 0 min. No steps: no mash.
//                                      Pump: default (on).
//   HOP, USE = Boil                  -> addition at TIME, AMOUNT kg -> g
//   HOP, USE = First Wort / Aroma    -> addition at boil start / flameout
//   MISC, USE = Boil                 -> addition at TIME (DISPLAY_AMOUNT)
//   Dry hop, mash hops, other miscs, fermentables, yeast, water, style,
//   fermentation                     -> not used by the controller (listed
//                                      in the notes when they are skipped)
// The parser reads the text as a stream of tags: no document tree is kept,
// so large exports fit. Accented letters are folded (see textfold.h).
#pragma once
#include <string>
#include <vector>

#include "core/settings.h"

namespace beerxml {

struct Result {
  std::vector<settings::Recipe> recipes;
  std::vector<std::string> notes;  // per recipe: what was imported / skipped
};

bool parse(const std::string& xml, Result& out, std::string& err);

// Checks the parser and text folding on built-in samples (console:
// "import selftest"). Returns true if every check passed.
bool selfTest(std::string& report);

}  // namespace beerxml
