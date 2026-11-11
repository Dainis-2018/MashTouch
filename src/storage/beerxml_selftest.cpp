// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// On-device self-test for the BeerXML importer and text folding
// (console: "import selftest"). There is no host test build yet, so this
// checks the parser on the real target.

#include <math.h>
#include <stdio.h>

#include "beerxml.h"
#include "textfold.h"

namespace beerxml {

namespace {

// ISO-8859-1 file: \xE9 is e-acute, \xDF sharp s. Covers entities, a
// self-closing tag, a comment, all hop uses, boil and secondary miscs.
const char kLatin1Sample[] =
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
    "<!-- test -->\n"
    "<RECIPES><RECIPE>\n"
    "  <NAME>Test Pale &amp; Porter</NAME><VERSION>1</VERSION><BOIL_TIME>60</BOIL_TIME><NOTES/>\n"
    "  <STYLE><NAME>Style name must not win</NAME></STYLE>\n"
    "  <HOPS>\n"
    "    <HOP><NAME>Magnum</NAME><AMOUNT>0.02</AMOUNT><USE>First Wort</USE><TIME>60</TIME></HOP>\n"
    "    <HOP><NAME>Mosaic</NAME><AMOUNT>0.011</AMOUNT><USE>Boil</USE><TIME>15</TIME></HOP>\n"
    "    <HOP><NAME>Citra</NAME><AMOUNT>0.03</AMOUNT><USE>Aroma</USE><TIME>20</TIME></HOP>\n"
    "    <HOP><NAME>Galaxy</NAME><AMOUNT>0.05</AMOUNT><USE>Dry Hop</USE><TIME>4320</TIME></HOP>\n"
    "  </HOPS>\n"
    "  <MISCS>\n"
    "    <MISC><NAME>Irish moss</NAME><AMOUNT>0.005</AMOUNT><DISPLAY_AMOUNT>5 g</DISPLAY_AMOUNT>"
    "<AMOUNT_IS_WEIGHT>true</AMOUNT_IS_WEIGHT><USE>Boil</USE><TIME>10</TIME></MISC>\n"
    "    <MISC><NAME>Vanilla</NAME><AMOUNT>2</AMOUNT><USE>Secondary</USE><TIME>20160</TIME></MISC>\n"
    "  </MISCS>\n"
    "  <MASH><MASH_STEPS>\n"
    "    <MASH_STEP><NAME>Mash in</NAME><STEP_TEMP>45</STEP_TEMP><STEP_TIME>10</STEP_TIME></MASH_STEP>\n"
    "    <MASH_STEP><NAME>Prot\xE9in Ma\xDF</NAME><STEP_TEMP>67</STEP_TEMP><STEP_TIME>60</STEP_TIME></MASH_STEP>\n"
    "    <MASH_STEP><NAME>Mash Out</NAME><STEP_TEMP>75</STEP_TEMP><STEP_TIME>10</STEP_TIME></MASH_STEP>\n"
    "  </MASH_STEPS></MASH>\n"
    "</RECIPE></RECIPES>\n";

// UTF-8 file without a <RECIPES> root; a-macron and s-caron in the name.
const char kUtf8Sample[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<RECIPE><NAME>Rudens \xC4\x81lis \xC5\xA1ok</NAME><BOIL_TIME>90</BOIL_TIME></RECIPE>";

struct Checker {
  std::string report;
  int failed = 0;
  void check(bool ok, const char* what) {
    report += ok ? "PASS " : "FAIL ";
    report += what;
    report += '\n';
    if (!ok) failed++;
  }
};

bool near(float a, float b) { return fabsf(a - b) < 0.01f; }

}  // namespace

bool selfTest(std::string& report) {
  Checker c;

  c.check(textfold::fromUtf8("\xC5\xA1\xC4\x81\xC5\xBE") == "saz", "fold UTF-8 Latvian letters");
  c.check(textfold::fromLatin1("Stra\xDF" "e") == "Strasse", "fold Latin-1 sharp s");
  c.check(textfold::fromUtf8("25\xC2\xB0") == "25\xC2\xB0", "keep the degree sign");

  Result r;
  std::string err;
  bool ok = parse(kLatin1Sample, r, err);
  c.check(ok && r.recipes.size() == 1, "Latin-1 file: one recipe");
  if (ok && r.recipes.size() == 1) {
    const settings::Recipe& rec = r.recipes[0];
    c.check(rec.name == "Test Pale & Porter", "recipe name with &amp;, not the style name");
    c.check(rec.boilMinutes == 60, "boil 60 min");
    c.check(rec.mash && rec.rests.size() == 1, "mash in + 1 rest + mash out");
    c.check(near(rec.mashIn.tempC, 45) && rec.mashIn.minutes == 10, "mash in: 45 C with a 10 min rest");
    c.check(rec.rests.size() == 1 && rec.rests[0].name == "Protein Mass", "Latin-1 rest name folded");
    c.check(near(rec.mashOut.tempC, 75) && rec.mashOut.minutes == 10, "mash out from the step name: 75 C 10 min");
    c.check(rec.additions.size() == 4, "4 additions (dry hop and secondary misc skipped)");
    if (rec.additions.size() == 4) {
      const auto& a = rec.additions;
      c.check(a[0].atMinutes == 60 && a[0].name == "Magnum (first wort)", "first wort at boil start");
      c.check(a[1].atMinutes == 15 && near(a[1].amount, 11) && a[1].unit == "g", "boil hop 11 g at 15 min");
      c.check(a[2].atMinutes == 10 && near(a[2].amount, 5) && a[2].unit == "g", "boil misc from DISPLAY_AMOUNT");
      c.check(a[3].atMinutes == 0 && a[3].name == "Citra (aroma)", "aroma hop at flameout");
    }
    c.check(!r.notes.empty() && r.notes[0].find("Galaxy") != std::string::npos &&
                r.notes[0].find("Vanilla") != std::string::npos,
            "skipped items listed in the notes");
  } else {
    c.check(false, err.c_str());
  }

  Result u;
  ok = parse(kUtf8Sample, u, err);
  c.check(ok && u.recipes.size() == 1 && u.recipes[0].name == "Rudens alis sok", "UTF-8 file, no RECIPES root");
  c.check(ok && u.recipes.size() == 1 && !u.recipes[0].mash && u.recipes[0].boilMinutes == 90,
          "no mash: boil only");

  // Single infusion: one step only.
  Result one;
  ok = parse("<RECIPE><NAME>One</NAME><BOIL_TIME>60</BOIL_TIME><MASH><MASH_STEPS><MASH_STEP>"
             "<NAME>Sacch</NAME><STEP_TEMP>67</STEP_TEMP><STEP_TIME>60</STEP_TIME></MASH_STEP>"
             "</MASH_STEPS></MASH></RECIPE>",
             one, err);
  if (ok && one.recipes.size() == 1) {
    const settings::Recipe& o = one.recipes[0];
    c.check(o.mash && near(o.mashIn.tempC, 67) && o.mashIn.minutes == 60 && o.rests.empty(),
            "one step: mash in 67 C with the 60 min rest");
    c.check(near(o.mashOut.tempC, 67) && o.mashOut.minutes == 0, "no mash out step: hold 67 C, 0 min");
  } else {
    c.check(false, "single-step file parsed");
  }

  Result bad;
  c.check(!parse("<RECIPES><RECIPE><NAME>cut", bad, err), "cut-off file is refused");

  char sum[48];
  snprintf(sum, sizeof(sum), "%s: %d failed\n", c.failed ? "FAILED" : "ALL PASSED", c.failed);
  report = c.report + sum;
  return c.failed == 0;
}

}  // namespace beerxml
