// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <algorithm>

#include "core/settings.h"
#include "storage/storage.h"

namespace wifi {
namespace {

constexpr uint32_t kConnectTimeoutMs = 20000;  // then open the access point
constexpr uint32_t kRetryMs = 120000;          // retry the home network (no AP clients)
constexpr uint32_t kApLingerMs = 30000;        // keep the AP after connecting

DNSServer dns;
bool apOn = false;
bool dnsOn = false;
bool mdnsOn = false;
bool tryingSta = false;
uint32_t staStartAt = 0;
uint32_t connectedAt = 0;
uint32_t lostAt = 0;
bool wasConnected = false;
std::string apSsid;
bool scanRequested = false;

const settings::Net& cfg() { return settings::current().net; }

void startMdns() {
  if (mdnsOn) return;
  if (MDNS.begin(cfg().hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsOn = true;
  }
}

void startAp() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  const std::string& pw = cfg().apPassword;
  bool open = pw.size() < 8;  // WPA2 needs 8 characters
  WiFi.softAP(apSsid.c_str(), open ? nullptr : pw.c_str());
  dns.start(53, "*", WiFi.softAPIP());  // every name -> us (captive portal)
  dnsOn = true;
  apOn = true;
  startMdns();
  Serial.printf("Wi-Fi: access point %s (%s) at %s\n", apSsid.c_str(), open ? "open" : "password",
                WiFi.softAPIP().toString().c_str());
}

void stopAp() {
  if (!apOn) return;
  if (dnsOn) dns.stop();
  dnsOn = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
  Serial.println("Wi-Fi: access point closed");
}

void startSta() {
  if (cfg().ssid.empty()) return;
  WiFi.begin(cfg().ssid.c_str(), cfg().password.c_str());
  tryingSta = true;
  staStartAt = millis();
}

}  // namespace

void begin() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "MashTouch-%02X%02X", mac[4], mac[5]);
  apSsid = name;

  WiFi.persistent(false);  // credentials live in settings.json, not in NVS
  setenv("TZ", cfg().timezone.c_str(), 1);
  tzset();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg().hostname.c_str());
  WiFi.setAutoReconnect(true);
  if (cfg().ssid.empty()) startAp();
  else startSta();
}

void loop() {
  uint32_t now = millis();
  if (dnsOn) dns.processNextRequest();

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    connectedAt = now;
    tryingSta = false;
    startMdns();
    // Clock from the network (brew log dates, delayed start by clock time).
    configTzTime(cfg().timezone.c_str(), "pool.ntp.org", "time.google.com");
    Serial.printf("Wi-Fi: connected to %s, http://%s.local or http://%s\n", cfg().ssid.c_str(),
                  cfg().hostname.c_str(), WiFi.localIP().toString().c_str());
  }
  if (!connected && wasConnected) lostAt = now;
  wasConnected = connected;

  if (connected) {
    // Close the setup AP once the home network works and nobody uses it.
    if (apOn && now - connectedAt > kApLingerMs && WiFi.softAPgetStationNum() == 0) stopAp();
  } else if (!cfg().ssid.empty()) {
    if (tryingSta && now - staStartAt > kConnectTimeoutMs) {
      tryingSta = false;
      startAp();
    } else if (!tryingSta && !apOn && lostAt && now - lostAt > kConnectTimeoutMs) {
      startAp();  // lost the home network for a while
    }
    // Retrying the home network makes the AP hop channels: only when no
    // phone is connected to the AP.
    if (!tryingSta && apOn && now - staStartAt > kRetryMs && WiFi.softAPgetStationNum() == 0) startSta();
  }

  if (scanRequested && WiFi.scanComplete() != WIFI_SCAN_RUNNING) scanRequested = false;
}

Status status() {
  Status s;
  s.connected = WiFi.status() == WL_CONNECTED;
  s.ssid = cfg().ssid;
  if (s.connected) {
    s.ip = WiFi.localIP().toString().c_str();
    s.rssi = WiFi.RSSI();
  }
  s.apOn = apOn;
  s.apSsid = apSsid;
  s.apOpen = cfg().apPassword.size() < 8;
  if (apOn) {
    s.apIp = WiFi.softAPIP().toString().c_str();
    s.apClients = WiFi.softAPgetStationNum();
  }
  s.hostname = cfg().hostname;
  return s;
}

void connect(const std::string& ssid, const std::string& password) {
  settings::current().net.ssid = ssid;
  settings::current().net.password = password;
  std::string err;
  storage::saveSettings(err);
  WiFi.disconnect(false);
  startSta();
}

void forget() {
  settings::current().net.ssid.clear();
  settings::current().net.password.clear();
  std::string err;
  storage::saveSettings(err);
  WiFi.disconnect(false);
  tryingSta = false;
  startAp();
}

void startScan() {
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true);  // async
  scanRequested = true;
}

bool scanning() { return WiFi.scanComplete() == WIFI_SCAN_RUNNING; }

std::vector<Network> scanResults() {
  std::vector<Network> out;
  int n = WiFi.scanComplete();
  for (int i = 0; i < n; i++) {
    std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) continue;
    bool dup = false;
    for (auto& e : out) {
      if (e.ssid == ssid) {
        e.rssi = std::max(e.rssi, (int)WiFi.RSSI(i));
        dup = true;
      }
    }
    if (!dup) out.push_back(Network{ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
  }
  std::sort(out.begin(), out.end(), [](const Network& a, const Network& b) { return a.rssi > b.rssi; });
  return out;
}

}  // namespace wifi
