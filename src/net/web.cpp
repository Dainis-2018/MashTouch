// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "web.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <uri/UriBraces.h>

#include <string>
#include <vector>

#include "app/controller.h"
#include "app/history.h"
#include "core/events.h"
#include "core/settings.h"
#include "hal/display.h"
#include "hal/io.h"
#include "hal/sensors.h"
#include "storage/brewlog.h"
#include "storage/files.h"
#include "storage/schema.h"
#include "storage/storage.h"
#include "webui_data.h"  // generated from web/index.html by tools/embed_web.py
#include "wifi.h"

#ifndef MT_VERSION
#define MT_VERSION "0.1.0-dev"
#endif

namespace web {
namespace {

WebServer server(80);
uint32_t rebootAt = 0;
std::string otaError;
bool otaStarted = false;

// ---- Events for the web UI (polled with the status) -------------------------
struct WebEvent {
  uint32_t seq;
  std::string type;
  std::string text;
};
constexpr size_t kEvents = 12;
std::vector<WebEvent> eventLog;
uint32_t eventSeq = 0;

void onEvent(const events::Event& e) {
  using events::Type;
  std::string text;
  switch (e.type) {
    case Type::Addition: {
      const auto& adds = controller::brewRecipe().additions;
      if (e.arg >= 0 && e.arg < (int)adds.size()) {
        const auto& a = adds[e.arg];
        text = a.name;
        if (a.amount > 0) {
          char buf[32];
          snprintf(buf, sizeof(buf), ": %g %s", a.amount, a.unit.c_str());
          text += buf;
        }
      }
      break;
    }
    case Type::PromptOpened: text = brew::promptName((brew::Prompt)e.arg); break;
    case Type::StepStarted: text = brew::phaseName((brew::Phase)e.arg); break;
    case Type::AutotuneDone: {
      const auto& h = settings::current().heater;
      char buf[64];
      snprintf(buf, sizeof(buf), "kp %.2f ki %.4f kd %.1f (saved)", h.kp, h.ki, h.kd);
      text = buf;
      break;
    }
    default: break;
  }
  eventLog.push_back(WebEvent{++eventSeq, events::name(e.type), text});
  if (eventLog.size() > kEvents) eventLog.erase(eventLog.begin());
}

// ---- Helpers ----------------------------------------------------------------

const char* modeId(controller::Mode m) { return controller::modeName(m); }

const char* phaseId(brew::Phase p) {
  switch (p) {
    case brew::Phase::Idle: return "idle";
    case brew::Phase::WaterCheck: return "waterCheck";
    case brew::Phase::Prime: return "prime";
    case brew::Phase::DelayedStart: return "delayedStart";
    case brew::Phase::Mash: return "mash";
    case brew::Phase::IodineTest: return "iodineTest";
    case brew::Phase::RemoveMalt: return "removeMalt";
    case brew::Phase::Boil: return "boil";
    case brew::Phase::HotWhirlpool: return "hotWhirlpool";
    case brew::Phase::Cooling: return "cooling";
    case brew::Phase::ColdWhirlpool: return "coldWhirlpool";
    case brew::Phase::Finished: return "finished";
  }
  return "?";
}

const char* promptId(brew::Prompt p) {
  switch (p) {
    case brew::Prompt::None: return "";
    case brew::Prompt::WaterAdded: return "waterAdded";
    case brew::Prompt::AddMalt: return "addMalt";
    case brew::Prompt::IodineTest: return "iodineTest";
    case brew::Prompt::RemoveMalt: return "removeMalt";
    case brew::Prompt::StartWhirlpool: return "startWhirlpool";
    case brew::Prompt::StartCooling: return "startCooling";
    case brew::Prompt::Finished: return "finished";
  }
  return "";
}

double round2(float v) { return (double)lroundf(v * 100) / 100.0; }

void sendJson(int code, const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  server.send(code, "application/json", out.c_str());
}

void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

void sendError(int code, const std::string& msg) {
  JsonDocument doc;
  doc["error"] = msg;
  sendJson(code, doc);
}

// Writes need the web password when one is set (HTTP basic auth, user "admin").
bool authorized() {
  const std::string& pw = settings::current().net.webPassword;
  if (pw.empty()) return true;
  if (server.authenticate("admin", pw.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, "MashTouch");
  return false;
}

bool parseBody(JsonDocument& doc) {
  DeserializationError e = deserializeJson(doc, server.arg("plain").c_str());
  if (e || !doc.is<JsonObject>()) {
    sendError(400, "expected a JSON object");
    return false;
  }
  return true;
}

// ---- Status -------------------------------------------------------------------

void handleStatus() {
  const controller::Status& st = controller::status();
  JsonDocument doc;
  doc["mode"] = modeId(st.mode);
  doc["paused"] = st.paused;
  doc["faults"] = st.faults;
  JsonArray faults = doc["faultText"].to<JsonArray>();
  if (st.faults & controller::kFaultSensor) faults.add("sensor fault: heater and pump off");
  if (st.faults & controller::kFaultOverTemp) faults.add("over temperature: heater off");
  if (st.faults & controller::kFaultHeatsink) faults.add("heatsink too hot: heater off");
  if (st.faults & controller::kFaultLevel) faults.add("level switch: heater off");
  if (st.faults & controller::kFaultRunaway) faults.add("runaway: the kettle heats with the heater off (check the SSR)");
  doc["confirm"] = st.confirm == controller::Confirm::Skip ? "skip" : st.confirm == controller::Confirm::Stop ? "stop" : "";

  if (st.tempOk) doc["temp"] = round2(st.tempC);
  else doc["temp"] = nullptr;
  doc["setpoint"] = round2(st.setpointC);
  doc["reached"] = st.reached;

  JsonObject h = doc["heater"].to<JsonObject>();
  h["zone"] = controller::zoneName(st.heaterZone);
  h["duty"] = lroundf(st.heaterDuty);
  h["on"] = st.heaterOn;
  h["boilPower"] = st.boilPowerPct;
  JsonObject p = doc["pump"].to<JsonObject>();
  p["on"] = st.pumpOn;
  p["resting"] = st.pumpResting;
  p["override"] = st.pumpOverride;
  p["priming"] = st.priming;
  doc["fan"] = st.fanPct;
  h["count"] = st.heaters;
  JsonArray stages = h["stages"].to<JsonArray>();
  for (int i = 0; i < st.heaters; i++) stages.add((st.heatersOn >> i) & 1 ? true : false);
  JsonObject sg = doc["sparge"].to<JsonObject>();
  sg["available"] = st.spargeAvailable;
  sg["on"] = st.spargeOn;
  sg["heating"] = st.spargeHeating;
  if (st.spargeTempOk) sg["temp"] = round2(st.spargeTempC);
  else sg["temp"] = nullptr;
  sg["setpoint"] = round2(st.spargeSetC);
  doc["clock"] = st.clockS;
  doc["time"] = st.epoch;

  doc["phase"] = phaseId(st.phase);
  doc["phaseName"] = brew::phaseName(st.phase);
  doc["step"] = st.step;
  doc["stepCount"] = st.stepCount;
  doc["stepName"] = st.stepName;
  JsonObject t = doc["timer"].to<JsonObject>();
  t["remainingMs"] = st.remainingMs;
  t["elapsedMs"] = st.elapsedMs;
  t["counting"] = st.timerCounting;
  doc["prompt"] = promptId(st.prompt);
  doc["promptText"] = brew::promptName(st.prompt);
  doc["lastAddition"] = st.lastAddition;

  JsonObject m = doc["manual"].to<JsonObject>();
  m["heater"] = st.manualHeater;
  m["pump"] = st.manualPump;
  m["countdown"] = st.manualCountdown;
  JsonObject tu = doc["tune"].to<JsonObject>();
  tu["cycles"] = st.tuneCycles;
  tu["of"] = st.tuneCyclesWanted;
  JsonObject r = doc["resume"].to<JsonObject>();
  r["offered"] = st.resumeOffered;
  r["phase"] = brew::phaseName(st.resumePhase);
  r["step"] = st.resumeStep;
  JsonObject s = doc["sim"].to<JsonObject>();
  s["on"] = st.sim;
  s["speed"] = st.simSpeed;

  doc["fahrenheit"] = settings::current().fahrenheit;
  doc["recipe"] = settings::recipe().name;
  doc["activeRecipe"] = settings::current().activeRecipe;

  JsonArray sens = doc["sensors"].to<JsonArray>();
  for (int i = 0; i < sensors::count(); i++) {
    const sensors::Info& in = sensors::info(i);
    JsonObject o = sens.add<JsonObject>();
    o["name"] = in.name;
    o["role"] = settings::roleName(in.role);
    o["present"] = in.busIndex >= 0;
    o["ok"] = in.ok;
    if (in.ok) o["temp"] = round2(in.tempC);
    o["offsetC"] = round2(in.offsetC);
    o["errors"] = in.errors;
    char rom[17];
    for (int b = 0; b < 8; b++) snprintf(rom + b * 2, 3, "%02X", in.rom[b]);
    o["rom"] = rom;
  }

  doc["eventSeq"] = eventSeq;
  JsonArray ev = doc["events"].to<JsonArray>();
  for (const WebEvent& e : eventLog) {
    JsonObject o = ev.add<JsonObject>();
    o["seq"] = e.seq;
    o["type"] = e.type;
    o["text"] = e.text;
  }
  wifi::Status w = wifi::status();
  JsonObject wf = doc["wifi"].to<JsonObject>();
  wf["connected"] = w.connected;
  wf["rssi"] = w.rssi;
  wf["apOn"] = w.apOn;
  doc["uptime"] = millis() / 1000;
  sendJson(200, doc);
}

void handleBrewRecipe() {
  std::string out = storage::recipeJson(controller::brewRecipe(), "", false);
  server.send(200, "application/json", out.c_str());
}

void handleInfo() {
  JsonDocument doc;
  doc["version"] = MT_VERSION;
  doc["build"] = __DATE__ " " __TIME__;
  doc["heapFree"] = ESP.getFreeHeap();
  doc["psramFree"] = ESP.getFreePsram();
  doc["fsUsed"] = files::usedBytes();
  doc["fsTotal"] = files::totalBytes();
  doc["uptime"] = millis() / 1000;
  doc["sketchFree"] = ESP.getFreeSketchSpace();
  sendJson(200, doc);
}

// ---- Actions ------------------------------------------------------------------

void handleAction() {
  if (!authorized()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  std::string a = doc["action"] | "";
  bool ok = true;
  if (a == "startAuto") ok = controller::startAuto(doc["delayMin"] | 0);
  else if (a == "startManual") ok = controller::startManual();
  else if (a == "startAutotune") ok = controller::startAutotune(doc["tempC"] | 65.0f);
  else if (a == "resume") ok = controller::resume();
  else if (a == "discardResume") controller::discardResume();
  else if (a == "stop") controller::stop();
  else if (a == "pauseResume") controller::pauseResume();
  else if (a == "skip") controller::skip();
  else if (a == "answer") controller::answer(doc["yes"] | false);
  else if (a == "pumpToggle") controller::pumpToggle();
  else if (a == "pumpPrime") controller::pumpPrime();
  else if (a == "heaterToggle") controller::heaterToggle();
  else if (a == "setpoint") controller::setSetpoint(doc["tempC"] | 0.0f);
  else if (a == "boilPower") controller::setBoilPower(doc["pct"] | 0);
  else if (a == "timer") controller::setTimerMin(doc["min"] | 0);
  else if (a == "startTimer") controller::startTimerNow();
  else if (a == "sparge") {
    if (doc["tempC"].is<float>()) controller::setSpargeTemp(doc["tempC"]);
    if (doc["on"].is<bool>()) ok = controller::setSparge(doc["on"]);
  }
  else if (a == "addMinutes") {
    const controller::Status& st = controller::status();
    uint32_t left = (st.remainingMs + 59999) / 60000;
    controller::setTimerMin((uint16_t)(left + (doc["min"] | 5)));
  } else if (a == "sim") controller::setSim(doc["on"] | false, doc["speed"] | settings::current().sim.speed);
  else if (a == "simTemp") controller::setSimTemp(doc["tempC"] | 20.0f);
  else {
    sendError(400, "unknown action '" + a + "'");
    return;
  }
  if (ok) sendOk();
  else sendError(409, controller::lastError());
}

// ---- Settings -----------------------------------------------------------------

void handleGetSettings() {
  if (!authorized()) return;
  JsonDocument doc;
  storage::toJson(settings::current(), doc.to<JsonObject>());
  // Passwords never leave the controller; only whether one is set.
  const settings::Net& n = settings::current().net;
  JsonObject net = doc["net"];
  net["password"] = "";
  net["apPassword"] = "";
  net["webPassword"] = "";
  net["passwordSet"] = !n.password.empty();
  net["apPasswordSet"] = !n.apPassword.empty();
  net["webPasswordSet"] = !n.webPassword.empty();
  sendJson(200, doc);
}

// {"path":"heater.kp","value":25} or {"changes":[{"path":...,"value":...},...]}
void handleSetSettings() {
  if (!authorized()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  std::vector<std::pair<std::string, std::string>> changes;
  auto collect = [&changes](JsonObjectConst c) {
    std::string value;
    serializeJson(c["value"], value);
    changes.push_back(std::make_pair(std::string(c["path"] | ""), value));
  };
  if (doc["changes"].is<JsonArrayConst>()) {
    for (JsonVariantConst c : doc["changes"].as<JsonArrayConst>()) collect(c.as<JsonObjectConst>());
  } else {
    collect(doc.as<JsonObjectConst>());
  }
  JsonDocument res;
  JsonArray errors = res["errors"].to<JsonArray>();
  for (const auto& c : changes) {
    std::string err;
    if (c.first.empty() || !storage::setSetting(c.first.c_str(), c.second.c_str(), err)) {
      JsonObject e = errors.add<JsonObject>();
      e["path"] = c.first;
      e["error"] = c.first.empty() ? std::string("missing path") : err;
    }
  }
  sensors::reload();
  display::setBrightness(settings::current().brightness);
  res["ok"] = errors.size() == 0;
  sendJson(errors.size() == 0 ? 200 : 400, res);
}

void handleResetSettings() {
  if (!authorized()) return;
  // Keep the network settings, or the controller would drop off the network.
  settings::Net keep = settings::current().net;
  std::string err;
  bool ok = storage::resetSettings(err);
  settings::current().net = keep;
  ok = ok && storage::saveSettings(err);
  sensors::reload();
  if (ok) sendOk();
  else sendError(500, err);
}

// ---- Recipes ------------------------------------------------------------------

void handleRecipes() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& r : storage::listRecipes()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = r.id;
    o["name"] = r.name;
    o["active"] = r.id == settings::current().activeRecipe;
  }
  sendJson(200, doc);
}

void handleGetRecipe() {
  std::string json = storage::recipeFileJson(server.pathArg(0).c_str(), false);
  if (json.empty()) sendError(404, "no such recipe");
  else server.send(200, "application/json", json.c_str());
}

void handleSaveRecipe() {
  if (!authorized()) return;
  std::string err;
  if (storage::saveRecipeJson(server.pathArg(0).c_str(), server.arg("plain").c_str(), err)) sendOk();
  else sendError(400, err);
}

void handleDeleteRecipe() {
  if (!authorized()) return;
  std::string err;
  if (storage::deleteRecipe(server.pathArg(0).c_str(), err)) sendOk();
  else sendError(400, err);
}

void handleUseRecipe() {
  if (!authorized()) return;
  std::string err;
  if (storage::useRecipe(server.pathArg(0).c_str(), err)) sendOk();
  else sendError(400, err);
}

// Body: a BeerXML file or MashTouch recipe JSON, as plain text.
void handleImport() {
  if (!authorized()) return;
  std::vector<std::string> ids, notes;
  std::string err;
  bool ok = storage::importRecipes(server.arg("plain").c_str(), ids, notes, err);
  JsonDocument doc;
  doc["ok"] = ok;
  if (!ok) doc["error"] = err;
  JsonArray a = doc["ids"].to<JsonArray>();
  for (const auto& id : ids) a.add(id);
  JsonArray n = doc["notes"].to<JsonArray>();
  for (const auto& s : notes) n.add(s);
  sendJson(ok ? 200 : 400, doc);
}

// ---- Sensors ------------------------------------------------------------------

bool parseRole(const char* s, settings::SensorRole& out) {
  for (int i = 0; i <= (int)settings::SensorRole::Sparge; i++) {
    if (!strcmp(s, settings::roleName((settings::SensorRole)i))) {
      out = (settings::SensorRole)i;
      return true;
    }
  }
  return false;
}

void handleSensor() {
  if (!authorized()) return;
  int i = atoi(server.pathArg(0).c_str());
  if (i < 0 || i >= sensors::count()) {
    sendError(404, "no such sensor");
    return;
  }
  JsonDocument doc;
  if (!parseBody(doc)) return;
  sensors::Info s = sensors::info(i);
  settings::SensorRole role = s.role;
  if (doc["role"].is<const char*>() && !parseRole(doc["role"], role)) {
    sendError(400, "role: none, kettle, rims, heatsink, aux or sparge");
    return;
  }
  float offset = doc["offsetC"] | s.offsetC;
  if (offset < -10 || offset > 10) {
    sendError(400, "offset must be -10..10 C");
    return;
  }
  std::string name = doc["name"] | s.name.c_str();
  sensors::configure(i, role, offset, name);
  std::string err;
  if (storage::saveSettings(err)) sendOk();
  else sendError(500, err);
}

void handleSensorForget() {
  if (!authorized()) return;
  int i = atoi(server.pathArg(0).c_str());
  if (!sensors::forget(i)) {
    sendError(404, "no stored settings for this sensor");
    return;
  }
  std::string err;
  storage::saveSettings(err);
  sendOk();
}

void handleRescan() {
  if (!authorized()) return;
  sensors::rescan();
  sendOk();
}

// ---- Wi-Fi --------------------------------------------------------------------

void handleWifi() {
  wifi::Status w = wifi::status();
  JsonDocument doc;
  doc["connected"] = w.connected;
  doc["ssid"] = w.ssid;
  doc["ip"] = w.ip;
  doc["rssi"] = w.rssi;
  doc["hostname"] = w.hostname;
  doc["apOn"] = w.apOn;
  doc["apSsid"] = w.apSsid;
  doc["apIp"] = w.apIp;
  doc["apClients"] = w.apClients;
  doc["scanning"] = wifi::scanning();
  JsonArray nets = doc["networks"].to<JsonArray>();
  for (const auto& n : wifi::scanResults()) {
    JsonObject o = nets.add<JsonObject>();
    o["ssid"] = n.ssid;
    o["rssi"] = n.rssi;
    o["open"] = n.open;
  }
  sendJson(200, doc);
}

void handleWifiScan() {
  if (!authorized()) return;
  wifi::startScan();
  sendOk();
}

void handleWifiConnect() {
  if (!authorized()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  std::string ssid = doc["ssid"] | "";
  if (ssid.empty()) {
    sendError(400, "ssid missing");
    return;
  }
  sendOk();  // answer first: the connection may move to another network
  wifi::connect(ssid, doc["password"] | "");
}

void handleWifiForget() {
  if (!authorized()) return;
  sendOk();
  wifi::forget();
}

// ---- Firmware update and reboot -------------------------------------------------

void handleOtaUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaError.clear();
    otaStarted = true;
    const std::string& pw = settings::current().net.webPassword;
    if (!pw.empty() && !server.authenticate("admin", pw.c_str())) otaError = "login required";
    else if (controller::status().mode != controller::Mode::Idle) otaError = "stop the running mode first";
    else if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaError = Update.errorString();
    if (otaError.empty()) Serial.printf("OTA: receiving %s\n", up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaError.empty() && Update.write(up.buf, up.currentSize) != up.currentSize) otaError = Update.errorString();
    esp_task_wdt_reset();  // the upload runs inside one loop() pass
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaError.empty() && !Update.end(true)) otaError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaError = "upload aborted";
  }
}

void handleOtaDone() {
  if (!otaStarted) {
    sendError(400, "send the firmware .bin as multipart form field 'firmware'");
    return;
  }
  otaStarted = false;
  if (!otaError.empty()) {
    if (Update.isRunning()) Update.abort();
    sendError(otaError == "login required" ? 401 : 400, otaError);
    return;
  }
  sendOk();
  Serial.println("OTA: done, restarting");
  rebootAt = millis() + 1000;
}

// ---- History and brew logs ------------------------------------------------------

// The rolling history as parallel arrays (tenths of a degree, null = none),
// sent in pieces so no large buffer is needed.
void handleHistory() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  char buf[64];
  int n = history::count();
  snprintf(buf, sizeof(buf), "{\"clock\":%u,\"step\":%u,\"t\":[", (unsigned)controller::status().clockS,
           (unsigned)history::kStepS);
  server.sendContent(buf);
  std::string chunk;
  const char* keys[4] = {"t", "temp", "sp", "duty"};
  for (int k = 0; k < 4; k++) {
    if (k) {
      snprintf(buf, sizeof(buf), "],\"%s\":[", keys[k]);
      chunk += buf;
    }
    for (int i = 0; i < n; i++) {
      const history::Sample& s = history::at(i);
      if (i) chunk += ',';
      int v = k == 0 ? (int)s.t : k == 1 ? s.temp10 : k == 2 ? s.sp10 : s.duty;
      if ((k == 1 || k == 2) && v == history::kNone) chunk += "null";
      else {
        snprintf(buf, sizeof(buf), "%d", v);
        chunk += buf;
      }
      if (chunk.size() > 1200) {
        server.sendContent(chunk.c_str(), chunk.size());
        chunk.clear();
      }
    }
  }
  chunk += "]}";
  server.sendContent(chunk.c_str(), chunk.size());
  server.sendContent("", 0);
}

void handleLogs() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& l : brewlog::list()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = l.id;
    o["recipe"] = l.recipe;
    o["start"] = l.start;
    o["sim"] = l.sim;
    o["durationS"] = l.durationS;
    o["result"] = l.result;
    o["size"] = l.size;
    o["active"] = l.id == brewlog::activeId();
  }
  sendJson(200, doc);
}

void handleGetLog() {
  File f = LittleFS.open(brewlog::path(atoi(server.pathArg(0).c_str())).c_str(), "r");
  if (!f) {
    sendError(404, "no such log");
    return;
  }
  server.streamFile(f, "application/x-ndjson");
  f.close();
}

void handleDeleteLog() {
  if (!authorized()) return;
  if (brewlog::remove(atoi(server.pathArg(0).c_str()))) sendOk();
  else sendError(400, "no such log, or it is still being written");
}

// {"epoch": 1794300000}: set the clock from a browser when there is no NTP
// (setup access point). Ignored once the clock is set.
void handleSetTime() {
  if (!authorized()) return;
  JsonDocument doc;
  if (!parseBody(doc)) return;
  uint32_t epoch = doc["epoch"] | (uint32_t)0;
  if (epoch > 1700000000 && controller::epochNow() == 0) controller::setClock(epoch);
  sendOk();
}

void handleReboot() {
  if (!authorized()) return;
  if (controller::status().mode != controller::Mode::Idle) {
    sendError(409, "stop the running mode first");
    return;
  }
  sendOk();
  rebootAt = millis() + 500;
}

// ---- Web UI -------------------------------------------------------------------

void handleUi() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (PGM_P)kWebUi, kWebUiLen);
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    sendError(404, "no such API endpoint");
    return;
  }
  // Every other path (also phone captive-portal checks) gets the UI.
  handleUi();
}

}  // namespace

void begin() {
  controller::addEventListener(onEvent);

  server.on("/", HTTP_GET, handleUi);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handleSetSettings);
  server.on("/api/settings/reset", HTTP_POST, handleResetSettings);
  server.on("/api/brew/recipe", HTTP_GET, handleBrewRecipe);
  server.on("/api/recipes", HTTP_GET, handleRecipes);
  server.on("/api/import", HTTP_POST, handleImport);
  // Longer patterns first: "{}" also matches across '/'.
  server.on(UriBraces("/api/recipes/{}/use"), HTTP_POST, handleUseRecipe);
  server.on(UriBraces("/api/recipes/{}"), HTTP_GET, handleGetRecipe);
  server.on(UriBraces("/api/recipes/{}"), HTTP_POST, handleSaveRecipe);
  server.on(UriBraces("/api/recipes/{}"), HTTP_DELETE, handleDeleteRecipe);
  server.on("/api/sensors/rescan", HTTP_POST, handleRescan);
  server.on(UriBraces("/api/sensors/{}/forget"), HTTP_POST, handleSensorForget);
  server.on(UriBraces("/api/sensors/{}"), HTTP_POST, handleSensor);
  server.on("/api/wifi", HTTP_GET, handleWifi);
  server.on("/api/wifi", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/scan", HTTP_POST, handleWifiScan);
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/logs", HTTP_GET, handleLogs);
  server.on(UriBraces("/api/logs/{}"), HTTP_GET, handleGetLog);
  server.on(UriBraces("/api/logs/{}"), HTTP_DELETE, handleDeleteLog);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) >= 0) {
    io::allOff();
    delay(100);
    ESP.restart();
  }
}

}  // namespace web
