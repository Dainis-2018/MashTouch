// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wi-Fi without blocking: joins the home network from the settings; when
// there is none, or it cannot be reached, opens a setup access point
// ("MashTouch-XXXX", password in settings) with a captive DNS so phones
// open the web UI by themselves. mDNS name: <hostname>.local.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace wifi {

struct Status {
  bool connected = false;  // to the home network
  std::string ssid;
  std::string ip;
  int rssi = 0;
  bool apOn = false;
  std::string apSsid;
  std::string apIp;
  bool apOpen = false;  // no password (apPassword shorter than 8)
  int apClients = 0;
  std::string hostname;
};

struct Network {
  std::string ssid;
  int rssi;
  bool open;
};

void begin();
void loop();
Status status();

// Save new credentials and connect (keeps the access point until connected).
void connect(const std::string& ssid, const std::string& password);
// Forget the home network: access point only.
void forget();

// Network scan, non-blocking: start, then poll scanning() / scanResults().
void startScan();
bool scanning();
std::vector<Network> scanResults();

}  // namespace wifi
