// MashTouch - ESP32-S3 touchscreen brew controller
// Copyright (C) 2026 MashTouch contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wi-Fi: connection state with a QR code (join the setup access point, or
// open the web UI), network scan and connect, forget.

#include <stdio.h>

#include <string>

#include "core/settings.h"
#include "kit.h"
#include "net/wifi.h"
#include "screens.h"
#include "ui.h"

namespace ui {
namespace wifi_screen {
namespace {

lv_obj_t* statusLabel;
lv_obj_t* qr;
lv_obj_t* qrNote;
lv_obj_t* netList;
std::string shownQr;
std::string shownStatus;
bool waitingScan = false;

void showNetworks() {
  lv_obj_clean(netList);
  std::vector<wifi::Network> nets = wifi::scanResults();
  if (nets.empty()) kit::label(netList, "No networks found.", kit::kSmall);
  for (const auto& n : nets) {
    char val[24];
    snprintf(val, sizeof(val), "%d dBm%s", n.rssi, n.open ? " open" : "");
    std::string ssid = n.ssid;
    bool open = n.open;
    kit::rowValue(netList, n.ssid.c_str(), val, [ssid, open]() {
      if (open) {
        wifi::connect(ssid, "");
        kit::toast("Connecting...");
        return;
      }
      kit::editText(("Password for " + ssid).c_str(), "", [ssid](const std::string& pw) {
        wifi::connect(ssid, pw);
        kit::toast("Connecting...");
      });
    });
  }
}

}  // namespace

void create(lv_obj_t* root) {
  shownQr.clear();
  shownStatus.clear();
  lv_obj_t* list = kit::scrollList(root);

  lv_obj_t* top = kit::card(list);
  lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(top, 10, 0);
  lv_obj_t* left = kit::column(top);
  lv_obj_set_flex_grow(left, 1);
  lv_obj_set_width(left, LV_SIZE_CONTENT);
  statusLabel = kit::label(left, "", kit::kNormal);
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(statusLabel, 290);
  qrNote = kit::label(left, "", kit::kSmall);
  lv_obj_set_style_text_color(qrNote, kit::dim(), 0);
  qr = lv_qrcode_create(top);
  lv_qrcode_set_size(qr, 130);
  lv_qrcode_set_dark_color(qr, lv_color_black());
  lv_qrcode_set_light_color(qr, lv_color_white());
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 6, 0);

  lv_obj_t* r = kit::row(list);
  lv_obj_t* scan = kit::button(r, LV_SYMBOL_REFRESH " Scan networks", []() {
    wifi::startScan();
    waitingScan = true;
    lv_obj_clean(netList);
    kit::label(netList, "Scanning...", kit::kSmall);
  });
  lv_obj_set_flex_grow(scan, 1);
  lv_obj_t* forget = kit::button(r, LV_SYMBOL_TRASH " Forget", []() {
    kit::confirm("Forget the home network? The setup access point opens.", "Forget", []() { wifi::forget(); });
  });
  kit::setColor(forget, kit::dim());

  netList = kit::column(list);
  update();
}

void update() {
  wifi::Status w = wifi::status();
  char buf[200];
  std::string qrText, note;
  if (w.connected) {
    snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s  (%d dBm)\nhttp://%s.local\nhttp://%s", w.ssid.c_str(), w.rssi,
             w.hostname.c_str(), w.ip.c_str());
    qrText = "http://" + w.ip + "/";
    note = "Scan to open the web UI";
  } else if (w.apOn) {
    const std::string& pw = settings::current().net.apPassword;
    if (w.apOpen)
      snprintf(buf, sizeof(buf), "Setup access point:\n%s (open)\nthen open http://%s", w.apSsid.c_str(), w.apIp.c_str());
    else
      snprintf(buf, sizeof(buf), "Setup access point:\n%s\npassword: %s\nthen open http://%s", w.apSsid.c_str(),
               pw.c_str(), w.apIp.c_str());
    qrText = "WIFI:T:" + std::string(w.apOpen ? "nopass" : "WPA") + ";S:" + w.apSsid + ";P:" + (w.apOpen ? "" : pw) + ";;";
    note = w.ssid.empty() ? "Scan to join, or choose a home network below"
                          : "Could not reach " + w.ssid + "; retrying";
  } else {
    snprintf(buf, sizeof(buf), "Connecting to %s...", w.ssid.c_str());
  }
  if (buf != shownStatus) {
    shownStatus = buf;
    lv_label_set_text(statusLabel, buf);
    lv_label_set_text(qrNote, note.c_str());
  }
  if (qrText != shownQr) {
    shownQr = qrText;
    if (qrText.empty()) {
      lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(qr, LV_OBJ_FLAG_HIDDEN);
      lv_qrcode_update(qr, qrText.c_str(), qrText.size());
    }
  }
  if (waitingScan && !wifi::scanning()) {
    waitingScan = false;
    showNetworks();
  }
}

}  // namespace wifi_screen
}  // namespace ui
