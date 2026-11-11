// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console.h"

#include <Arduino.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <string>
#include <vector>

#include "controller.h"
#include "core/settings.h"
#include "hal/i2cbus.h"
#include "hal/sensors.h"
#include "net/wifi.h"
#include "storage/beerxml.h"
#include "storage/files.h"
#include "storage/storage.h"

namespace console {
namespace {

char line[1024];  // long enough for a pasted recipe JSON
size_t len = 0;
bool overflow = false;
uint32_t logEveryMs = 0;
uint32_t lastLog = 0;
bool resumeAnnounced = false;

// ---- Recipe import (multi-line paste) ----------------------------------------
constexpr size_t kMaxImport = 256 * 1024;  // in PSRAM; BeerSmith exports can be large
bool importing = false;
std::string importBuf;
char importKind = 0;  // '<' BeerXML, '{' or '[' JSON
bool importHasRoot = false;  // <RECIPES> seen: wait for </RECIPES>
int jsonDepth = 0;
bool jsonInString = false, jsonEscape = false;

bool endsWithI(const std::string& s, const char* tail) {
  size_t n = strlen(tail);
  if (s.size() < n) return false;
  return strncasecmp(s.c_str() + s.size() - n, tail, n) == 0;
}

void stopImport() {
  importing = false;
  std::string().swap(importBuf);  // give the memory back
}

void finishImport() {
  std::vector<std::string> ids, notes;
  std::string err;
  Serial.printf("received %u bytes\n", (unsigned)importBuf.size());
  bool ok = storage::importRecipes(importBuf, ids, notes, err);
  stopImport();
  for (const auto& n : notes) Serial.printf("  %s\n", n.c_str());
  if (!ok) {
    Serial.printf("import failed: %s\n", err.c_str());
    return;
  }
  for (const auto& id : ids) Serial.printf("imported: %s\n", id.c_str());
  Serial.printf("make one active with: recipe use %s\n", ids[0].c_str());
}

void startImport() {
  importing = true;
  importBuf.clear();
  importKind = 0;
  importHasRoot = false;
  jsonDepth = 0;
  jsonInString = jsonEscape = false;
  Serial.println("Paste BeerXML or MashTouch JSON now. It ends by itself after </RECIPES>,");
  Serial.println("</RECIPE> or the closing bracket. Type cancel on its own line to stop.");
}

void importChar(char ch) {
  if (ch == '\r') return;
  if (!importKind) {
    // Before the file starts: skip blanks and a UTF-8 byte order mark.
    if (isspace((unsigned char)ch) || (unsigned char)ch >= 0x80) return;
    if (ch != '<' && ch != '{' && ch != '[') {
      importBuf += ch;  // maybe "cancel"
      if (ch == '\n' || importBuf.size() >= 6) {
        Serial.println(strncasecmp(importBuf.c_str(), "cancel", 6) == 0
                           ? "import cancelled"
                           : "expected BeerXML (<...) or JSON ({...}): import cancelled");
        stopImport();
      }
      return;
    }
    importBuf.clear();
    importKind = ch;
  }
  importBuf += ch;
  if (importBuf.size() > kMaxImport) {
    Serial.println("import too large (256 kB max)");
    stopImport();
    return;
  }
  if (ch == '\n') {
    size_t prev = importBuf.size() >= 2 ? importBuf.find_last_of('\n', importBuf.size() - 2) : std::string::npos;
    std::string last = importBuf.substr(prev == std::string::npos ? 0 : prev + 1);
    while (!last.empty() && isspace((unsigned char)last.back())) last.pop_back();
    if (last == "cancel") {
      Serial.println("import cancelled");
      stopImport();
    }
    return;
  }
  if (importKind == '<') {
    if (ch != '>') return;
    if (!importHasRoot) {
      size_t from = importBuf.size() > 80 ? importBuf.size() - 80 : 0;
      std::string tail = importBuf.substr(from);
      for (char& c : tail) c = (char)toupper((unsigned char)c);
      if (tail.find("<RECIPES") != std::string::npos) importHasRoot = true;
    }
    if (endsWithI(importBuf, "</RECIPES>") || (!importHasRoot && endsWithI(importBuf, "</RECIPE>"))) finishImport();
    return;
  }
  // JSON: count brackets outside strings.
  if (jsonInString) {
    if (jsonEscape) jsonEscape = false;
    else if (ch == '\\') jsonEscape = true;
    else if (ch == '"') jsonInString = false;
    return;
  }
  if (ch == '"') jsonInString = true;
  else if (ch == '{' || ch == '[') jsonDepth++;
  else if (ch == '}' || ch == ']') {
    if (--jsonDepth == 0) finishImport();
  }
}

const char* kHelp =
    "Brewing:\n"
    "  status               one status line (log <s> repeats it, log 0 stops)\n"
    "  auto [delay_min]     start the automatic process with the active recipe\n"
    "  resume | discard     continue / forget the brew interrupted by power loss\n"
    "  manual               start manual mode\n"
    "  tune <temp>          PID autotune around <temp> C (gains are saved)\n"
    "  stop | pause | skip  stop, pause/resume, skip the current step\n"
    "  starttimer           start the rest/boil timer now (setpoint not reached)\n"
    "  yes | no             answer the open prompt or confirmation\n"
    "  heat | pump          toggle heater (manual) / pump (manual, or override)\n"
    "  prime                run the priming pulses\n"
    "  sp <temp>            setpoint (manual) or current step temperature (auto)\n"
    "  timer <min>          countdown (manual) or remaining minutes (auto)\n"
    "  boil <pct>           boil power\n"
    "  sparge on | off | <temp>   sparge water heater (needs output + sensor role)\n"
    "  sim on [speed] | off | temp <c>   fake heating\n"
    "Settings (saved at once):\n"
    "  get [path]           show settings as JSON, e.g. get heater\n"
    "  set <path> <value>   e.g. set heater.kp 25, set outputs.1.role heater\n"
    "  pid [kp ki kd]       show or set PID gains\n"
    "  defaults             reset all settings (keeps the active recipe)\n"
    "  sensors | rescan     list / rescan temperature sensors\n"
    "  sensor <n> role <none|kettle|rims|heatsink|aux|sparge>\n"
    "  sensor <n> offset <c> | name <text> | forget\n"
    "Recipes:\n"
    "  recipes              list stored recipes (* = active)\n"
    "  recipe [json]        show the active recipe\n"
    "  recipe use <id>      make a stored recipe active\n"
    "  recipe new <name>    new recipe from the default template, made active\n"
    "  recipe copy <name>   copy the active recipe under a new name, made active\n"
    "  recipe delete <id>\n"
    "  rset <path> <value>  edit the active recipe, e.g. rset rests.0.minutes 45\n"
    "  rjson <json>         replace the active recipe with a JSON object\n"
    "  fs                   filesystem usage\n"
    "  i2c                  scan the shared I2C bus (Waveshare board)\n"
    "Network:\n"
    "  wifi                 status (IP, setup access point)\n"
    "  wifi scan | list     scan, then list networks\n"
    "  set net.ssid \"My net\", set net.password \"secret\", then: wifi connect\n"
    "  wifi forget          drop the home network (setup access point only)\n";

// Known parts on the Waveshare board and the backpack, for the scan listing.
const char* i2cName(uint8_t a) {
  switch (a) {
    case 0x18: case 0x19: return "ES8311 codec";
    case 0x20: return "TCA9554 on-board expander";
    case 0x22: return "TCA9535 backpack front panel";
    case 0x34: return "AXP2101 power";
    case 0x38: return "FT6336 touch";
    case 0x51: return "PCF85063 clock";
    case 0x6A: case 0x6B: return "QMI8658 IMU";
  }
  return "";
}

void i2cScan() {
  if (!i2cbus::available() || !i2cbus::begin()) {
    Serial.println("no shared I2C bus on this board");
    return;
  }
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (!i2cbus::probe(a)) continue;
    Serial.printf("  0x%02X  %s\n", a, i2cName(a));
    found++;
  }
  Serial.printf("%d device(s)\n", found);
}

void printTime(uint32_t ms) {
  uint32_t s = ms / 1000;
  Serial.printf("%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

void printStatus() {
  const auto& s = controller::status();
  Serial.printf("[%s%s", controller::modeName(s.mode), s.paused ? " PAUSED" : "");
  if (s.sim) Serial.printf(" SIM x%.0f", s.simSpeed);
  Serial.print("] ");
  if (s.tempOk) Serial.printf("T %.2f", s.tempC);
  else Serial.print("T --.--");
  if (s.mode != controller::Mode::Idle) Serial.printf(" -> %.2f", s.setpointC);
  Serial.printf(" | heat %s %.0f%% %s | pump %s%s%s | fan %u%%", controller::zoneName(s.heaterZone),
                s.heaterDuty, s.heaterOn ? "ON " : "off", s.pumpOn ? "ON" : "off",
                s.pumpResting ? " rest" : "", s.pumpOverride ? " ovr" : "", s.fanPct);
  if (s.mode == controller::Mode::Auto) {
    Serial.printf(" | %s", s.stepName);
    if (s.phase == brew::Phase::Mash) Serial.printf(" %d/%d", s.step + 1, s.stepCount);
    Serial.print(s.reached ? " reached " : " heating ");
    printTime(s.remainingMs);
  } else if (s.mode == controller::Mode::Manual) {
    Serial.printf(" | heater %s pump %s ", s.manualHeater ? "on" : "off", s.manualPump ? "on" : "off");
    printTime(s.manualCountdown ? s.remainingMs : s.elapsedMs);
    if (s.manualCountdown) Serial.print(" left");
  } else if (s.mode == controller::Mode::Autotune) {
    Serial.printf(" | cycle %u/%u", s.tuneCycles, s.tuneCyclesWanted);
  }
  if (s.faults) Serial.printf(" | FAULT 0x%02X", s.faults);
  if (s.prompt != brew::Prompt::None) Serial.printf(" | ? %s (yes/no)", brew::promptName(s.prompt));
  if (s.confirm == controller::Confirm::Skip) Serial.print(" | ? skip step (yes/no)");
  if (s.confirm == controller::Confirm::Stop) Serial.print(" | ? stop (yes/no)");
  if (s.resumeOffered) Serial.print(" | resume available");
  Serial.println();
}

void printSensors() {
  int n = sensors::count();
  if (!n) Serial.println("no sensors");
  for (int i = 0; i < n; i++) {
    const auto& s = sensors::info(i);
    Serial.printf("%d: %-16s %-8s ", i, s.name.c_str(), settings::roleName(s.role));
    for (int b = 0; b < 8; b++) Serial.printf("%02X", s.rom[b]);
    if (s.busIndex < 0) Serial.print("  MISSING");
    else if (s.ok) Serial.printf("  %.2f C (offset %.1f)", s.tempC, s.offsetC);
    else Serial.printf("  FAULT (%u errors)", s.errors);
    Serial.println();
  }
}

const char* pumpName(settings::PumpMode m) {
  switch (m) {
    case settings::PumpMode::Off: return "off";
    case settings::PumpMode::On: return "on";
    case settings::PumpMode::Cycle: return "cycle";
  }
  return "?";
}

void printStep(const char* label, const settings::MashStep& st) {
  Serial.printf("  %-9s %-16s %.1f C, ", label, st.name.c_str(), st.tempC);
  if (st.minutes) Serial.printf("%u min", st.minutes);
  else Serial.print("no rest");
  Serial.printf(", pump %s", pumpName(st.pump));
  Serial.println();
}

void printRecipe() {
  const auto& r = settings::recipe();
  Serial.printf("%s  [%s]\n", r.name.c_str(), settings::current().activeRecipe.c_str());
  if (r.mash) {
    printStep("mashIn", r.mashIn);
    Serial.println("            -> add malt");
    for (size_t i = 0; i < r.rests.size(); i++) {
      char label[12];
      snprintf(label, sizeof(label), "rests.%u", (unsigned)i);
      printStep(label, r.rests[i]);
    }
    printStep("mashOut", r.mashOut);
    Serial.println("            -> remove malt");
  } else {
    Serial.println("  no mash (boil only)");
  }
  Serial.printf("  boil %u min\n", r.boilMinutes);
  for (size_t i = 0; i < r.additions.size(); i++) {
    const auto& a = r.additions[i];
    Serial.printf("  additions.%u %-16s at %u min", (unsigned)i, a.name.c_str(), a.atMinutes);
    if (a.amount > 0) Serial.printf(", %g %s", a.amount, a.unit.c_str());
    Serial.println();
  }
}

void printRecipes() {
  auto list = storage::listRecipes();
  if (list.empty()) Serial.println("no recipes stored");
  for (const auto& r : list)
    Serial.printf("%c %-24s %s\n", r.id == settings::current().activeRecipe ? '*' : ' ', r.id.c_str(),
                  r.name.c_str());
}

void report(bool ok) {
  if (ok) Serial.println("ok");
  else Serial.printf("refused: %s\n", controller::lastError());
}

void reportErr(bool ok, const std::string& err) {
  if (ok) Serial.println("ok");
  else Serial.printf("error: %s\n", err.c_str());
}

void saveSettings() {
  std::string err;
  if (!storage::saveSettings(err)) Serial.printf("not saved: %s\n", err.c_str());
}

bool parseRole(const char* s, settings::SensorRole& out) {
  for (int i = 0; i <= (int)settings::SensorRole::Sparge; i++) {
    if (!strcmp(s, settings::roleName((settings::SensorRole)i))) {
      out = (settings::SensorRole)i;
      return true;
    }
  }
  return false;
}

void onEvent(const events::Event& e) {
  Serial.printf("* %s", events::name(e.type));
  switch (e.type) {
    case events::Type::Addition: {
      const auto& adds = settings::recipe().additions;
      if (e.arg >= 0 && e.arg < (int)adds.size()) Serial.printf(": %s", adds[e.arg].name.c_str());
      break;
    }
    case events::Type::PromptOpened:
      Serial.printf(": %s", brew::promptName((brew::Prompt)e.arg));
      break;
    case events::Type::StepStarted:
      Serial.printf(": %s", brew::phaseName((brew::Phase)e.arg));
      break;
    case events::Type::AutotuneDone: {
      const auto& h = settings::current().heater;
      Serial.printf(": kp %.3f ki %.5f kd %.2f (saved)", h.kp, h.ki, h.kd);
      break;
    }
    default:
      break;
  }
  Serial.println();
}

// Splits off the next space-separated word; returns "" at the end.
char* word(char*& rest) {
  while (*rest == ' ') rest++;
  char* w = rest;
  while (*rest && *rest != ' ') rest++;
  if (*rest) *rest++ = 0;
  while (*rest == ' ') rest++;
  return w;
}

void sensorCommand(char* rest) {
  char* idx = word(rest);
  char* what = word(rest);
  if (!*idx || !*what) {
    Serial.println("sensor <n> role <r> | offset <c> | name <text> | forget");
    return;
  }
  int i = atoi(idx);
  if (i < 0 || i >= sensors::count()) {
    Serial.println("no such sensor (see: sensors)");
    return;
  }
  sensors::Info s = sensors::info(i);
  bool ok;
  if (!strcmp(what, "forget")) {
    ok = sensors::forget(i);
  } else if (!strcmp(what, "role")) {
    settings::SensorRole role;
    if (!parseRole(rest, role)) {
      Serial.println("roles: none kettle rims heatsink aux sparge");
      return;
    }
    ok = sensors::configure(i, role, s.offsetC, s.name);
  } else if (!strcmp(what, "offset")) {
    float off = atof(rest);
    if (off < -10 || off > 10) {
      Serial.println("offset must be -10..10 C");
      return;
    }
    ok = sensors::configure(i, s.role, off, s.name);
  } else if (!strcmp(what, "name")) {
    ok = sensors::configure(i, s.role, s.offsetC, rest);
  } else {
    Serial.println("sensor <n> role <r> | offset <c> | name <text> | forget");
    return;
  }
  if (ok) saveSettings();
  printSensors();
}

void recipeCommand(char* rest) {
  char* sub = word(rest);
  std::string err;
  if (!*sub) {
    printRecipe();
  } else if (!strcmp(sub, "json")) {
    Serial.println(storage::recipeJson(settings::recipe(), rest, true).c_str());
  } else if (!strcmp(sub, "use")) {
    reportErr(storage::useRecipe(rest, err), err);
  } else if (!strcmp(sub, "new") || !strcmp(sub, "copy")) {
    if (!*rest) {
      Serial.println("give a name");
      return;
    }
    settings::Recipe r = !strcmp(sub, "new") ? settings::defaultRecipe() : settings::recipe();
    r.name = rest;
    std::string id = storage::recipeId(r.name);
    for (const auto& existing : storage::listRecipes()) {
      if (existing.id == id) {
        Serial.printf("recipe '%s' exists already\n", id.c_str());
        return;
      }
    }
    bool ok = storage::saveRecipe(r, id, err) && storage::useRecipe(id, err);
    reportErr(ok, err);
    if (ok) Serial.printf("active recipe: %s\n", id.c_str());
  } else if (!strcmp(sub, "delete")) {
    reportErr(storage::deleteRecipe(rest, err), err);
  } else {
    Serial.println("recipe [json] | use <id> | new <name> | copy <name> | delete <id>");
  }
}

void wifiCommand(char* rest) {
  char* sub = word(rest);
  if (!*sub) {
    wifi::Status w = wifi::status();
    if (w.connected)
      Serial.printf("connected to %s, %d dBm, http://%s.local or http://%s\n", w.ssid.c_str(), w.rssi,
                    w.hostname.c_str(), w.ip.c_str());
    else
      Serial.printf("not connected%s%s\n", w.ssid.empty() ? " (no home network set)" : " to ", w.ssid.c_str());
    if (w.apOn)
      Serial.printf("setup access point %s (%s) at http://%s, %d client(s)\n", w.apSsid.c_str(),
                    w.apOpen ? "open" : "password in net.apPassword", w.apIp.c_str(), w.apClients);
  } else if (!strcmp(sub, "scan")) {
    wifi::startScan();
    Serial.println("scanning... then: wifi list");
  } else if (!strcmp(sub, "list")) {
    if (wifi::scanning()) Serial.println("still scanning");
    for (const auto& n : wifi::scanResults())
      Serial.printf("  %4d dBm  %s%s\n", n.rssi, n.ssid.c_str(), n.open ? " (open)" : "");
  } else if (!strcmp(sub, "connect")) {
    const auto& n = settings::current().net;
    if (n.ssid.empty()) Serial.println("set net.ssid first");
    else {
      wifi::connect(n.ssid, n.password);
      Serial.printf("connecting to %s\n", n.ssid.c_str());
    }
  } else if (!strcmp(sub, "forget")) {
    wifi::forget();
    Serial.println("home network forgotten");
  } else {
    Serial.println("wifi [scan | list | connect | forget]");
  }
}

void run(char* cmdLine) {
  char* rest = cmdLine;
  char* cmd = word(rest);
  if (!*cmd) return;
  std::string err;

  if (!strcmp(cmd, "help")) Serial.print(kHelp);
  else if (!strcmp(cmd, "status")) printStatus();
  else if (!strcmp(cmd, "log")) logEveryMs = *rest ? atoi(rest) * 1000 : 5000;
  else if (!strcmp(cmd, "auto")) report(controller::startAuto(atoi(rest)));
  else if (!strcmp(cmd, "resume")) report(controller::resume());
  else if (!strcmp(cmd, "discard")) controller::discardResume();
  else if (!strcmp(cmd, "manual")) report(controller::startManual());
  else if (!strcmp(cmd, "tune")) report(*rest && controller::startAutotune(atof(rest)));
  else if (!strcmp(cmd, "stop")) controller::stop();
  else if (!strcmp(cmd, "pause")) controller::pauseResume();
  else if (!strcmp(cmd, "skip")) controller::skip();
  else if (!strcmp(cmd, "starttimer")) controller::startTimerNow();
  else if (!strcmp(cmd, "yes")) controller::answer(true);
  else if (!strcmp(cmd, "no")) controller::answer(false);
  else if (!strcmp(cmd, "heat")) controller::heaterToggle();
  else if (!strcmp(cmd, "pump")) controller::pumpToggle();
  else if (!strcmp(cmd, "prime")) controller::pumpPrime();
  else if (!strcmp(cmd, "sp") && *rest) controller::setSetpoint(atof(rest));
  else if (!strcmp(cmd, "timer") && *rest) controller::setTimerMin(atoi(rest));
  else if (!strcmp(cmd, "boil") && *rest) controller::setBoilPower(atoi(rest));
  else if (!strcmp(cmd, "sparge")) {
    if (!strcmp(rest, "on")) report(controller::setSparge(true));
    else if (!strcmp(rest, "off")) controller::setSparge(false);
    else if (*rest) controller::setSpargeTemp(atof(rest));
    const auto& s = controller::status();
    if (!s.spargeAvailable) Serial.println("sparge heater: needs an output and a sensor with the sparge role");
    else Serial.printf("sparge heater %s, %.1f -> %.1f C%s\n", s.spargeOn ? "on" : "off", s.spargeTempC, s.spargeSetC,
                       s.spargeHeating ? " (heating)" : "");
  }
  else if (!strcmp(cmd, "pid")) {
    auto& h = settings::current().heater;
    char* a = word(rest);
    char* b = word(rest);
    char* c = word(rest);
    if (*a && *b && *c) {
      h.kp = atof(a);
      h.ki = atof(b);
      h.kd = atof(c);
      saveSettings();
    }
    Serial.printf("kp %.3f ki %.5f kd %.2f (applies at next start)\n", h.kp, h.ki, h.kd);
  } else if (!strcmp(cmd, "sim")) {
    char* a = word(rest);
    if (!strcmp(a, "on")) controller::setSim(true, *rest ? atof(rest) : settings::current().sim.speed);
    else if (!strcmp(a, "off")) controller::setSim(false);
    else if (!strcmp(a, "temp") && *rest) controller::setSimTemp(atof(rest));
    else Serial.println("sim on [speed] | off | temp <c>");
  } else if (!strcmp(cmd, "get")) {
    std::string out = storage::settingsJson(rest, true);
    Serial.println(out.empty() ? "no such setting" : out.c_str());
  } else if (!strcmp(cmd, "set")) {
    char* path = word(rest);
    if (!*path || !*rest) {
      Serial.println("set <path> <value>");
      return;
    }
    bool ok = storage::setSetting(path, rest, err);
    if (ok) {
      sensors::reload();  // roles or offsets may have changed
      Serial.printf("%s = %s\n", path, storage::settingsJson(path, false).c_str());
    } else {
      Serial.printf("error: %s\n", err.c_str());
    }
  } else if (!strcmp(cmd, "defaults")) {
    reportErr(storage::resetSettings(err), err);
    sensors::reload();
  } else if (!strcmp(cmd, "sensors")) printSensors();
  else if (!strcmp(cmd, "rescan")) {
    sensors::rescan();
    printSensors();
  } else if (!strcmp(cmd, "sensor")) sensorCommand(rest);
  else if (!strcmp(cmd, "recipes")) printRecipes();
  else if (!strcmp(cmd, "recipe")) recipeCommand(rest);
  else if (!strcmp(cmd, "rset")) {
    char* path = word(rest);
    if (!*path || !*rest) {
      Serial.println("rset <path> <value>");
      return;
    }
    bool ok = storage::setRecipeField(path, rest, err);
    if (ok) Serial.printf("%s = %s\n", path, storage::recipeJson(settings::recipe(), path, false).c_str());
    else Serial.printf("error: %s\n", err.c_str());
  } else if (!strcmp(cmd, "import")) {
    if (!strcmp(rest, "selftest")) {
      std::string report;
      beerxml::selfTest(report);
      Serial.print(report.c_str());
    } else {
      startImport();
    }
  } else if (!strcmp(cmd, "rjson")) {
    reportErr(storage::replaceRecipe(rest, err), err);
  } else if (!strcmp(cmd, "wifi")) {
    wifiCommand(rest);
  } else if (!strcmp(cmd, "i2c")) {
    i2cScan();
  } else if (!strcmp(cmd, "fs")) {
    Serial.printf("LittleFS %s, %u of %u kB used\n", files::mounted() ? "mounted" : "NOT mounted",
                  (unsigned)(files::usedBytes() / 1024), (unsigned)(files::totalBytes() / 1024));
  } else {
    Serial.println("unknown command, try: help");
  }
}

}  // namespace

void begin() { controller::addEventListener(onEvent); }

void loop() {
  if (!resumeAnnounced) {
    resumeAnnounced = true;
    const auto& s = controller::status();
    if (s.resumeOffered) {
      Serial.printf("Unfinished brew found (%s", brew::phaseName(s.resumePhase));
      if (s.resumePhase == brew::Phase::Mash) Serial.printf(", step %d", s.resumeStep + 1);
      Serial.println("). Type 'resume' or press Start to continue, 'discard' to forget it.");
    }
  }

  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (importing) {
      importChar(ch);
      continue;
    }
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[len] = 0;
      if (overflow) Serial.println("line too long");
      else if (len) run(line);
      len = 0;
      overflow = false;
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    } else {
      overflow = true;
    }
  }
  if (logEveryMs && millis() - lastLog >= logEveryMs) {
    lastLog = millis();
    printStatus();
  }
}

}  // namespace console
