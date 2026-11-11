# MashTouch: project guide for Claude

ESP32‑S3 touchscreen brew controller for single-vessel (RIMS/BIAB) systems. New GPL firmware based on Open ArdBir, replacing the user's NodeMCU BrewManiacEx controller.

## Hard rules

- **License: GPL‑3.0‑or-later.** Keep Open ArdBir attribution (see NOTICE.md). Files that adapt Open ArdBir logic say so in their header.
- **Never copy code from BrewManiacEx** (C:\Users\daini\code\BrewManiacAI, CC BY‑NC‑SA, by Vito Tai): not its `src/`, not its `html/`, not tables ported from it (e.g. the button-label table in its `webui/src/brew/labels.ts`). Reproducing *behaviour* or a compatible JSON/web API is fine; the code must be written fresh.
- **Nothing is limited to one:** any number of temperature sensors and outputs, assigned by role in settings (heater 1..n, pump 1..n, sparge, fan, kettle sensor, heatsink sensor...).
- Heater and pump outputs must be off from boot and on any sensor fault.
- Builds use the PlatformIO Core CLI (`~/.platformio/penv/Scripts/pio.exe run`, environment `ws35`; `ws35_ota` uploads over Wi‑Fi) or the VS Code tasks in `.vscode/tasks.json`. The PlatformIO IDE extension is not used.

## Hardware decisions

- **Display panel: Waveshare ESP32‑S3‑Touch‑LCD‑3.5** (V2 PCB), chosen 2026‑11‑11 for a finished look (front cover frame):
  - ESP32‑S3R8 + 16 MB flash, native USB‑C
  - ST7796 SPI + FT6336 touch
  - ES8311 codec + amplifier, AXP2101, TCA9554 expander (LCD reset on EXIO1)
  - a 2×16 header J8
  - pin map `src/board/pins.h` (the only board; a DevKit + MSP3526 bench build was dropped on 2026‑11‑12)

  The other panels compared and the checks still open on real hardware are listed in `docs/design.md`.
- Front panel: touchscreen + **2 small (8–12 mm) buttons with LED rings: Start/Pause and Pump**. Start/Pause: short = pause/resume, long = stop/skip with on-screen confirm. Pump: short = toggle (manual override until next step), hold = prime.
- **Speaker, not a buzzer:** a small 8 Ω speaker on the panel's codec; chimes are synthesised in `src/hal/audio`.
- **The controller PCB is an I/O backpack with no MCU**, plugged into the panel's J8 and stacked behind it. It carries 12 V in → 5 V into J8 VBUS, the output drivers, 1‑Wire, level, 2× MAX31865 (on J8's SPI lines, the unused SD bus) and a TCA9535 (0x22) for the buttons and LEDs. Heater and pump outputs and the level switch are direct GPIOs; never put safety I/O on an I2C expander.
- The Waveshare board's I2C bus (7/8) is shared by touch, expanders, codec and PMU and is owned by `src/hal/i2cbus` (LovyanGFX's I2C driver). Never use Arduino `Wire` on it.
- **Hardware scope is only the controller head** (PCB + display + buttons). It fits into the user's existing NodeMCU box, which holds the SSRs, heatsink, cooling fan, PSU and mains wiring (documented, not designed). Connector types don't matter.
- Power: **12 V in** (pump and fan are 12 V); the backpack makes 5 V for the panel (J8 VBUS) and uses the panel's 3.3 V for its logic.
- Outputs: 4 signal outputs (low-side MOSFET, SSR drive, 5/12 V jumper) + 2 power outputs (12 V, ~3 A, PWM for the fan). Pump: on/off only, any voltage (12 V on a power output, or 230 V/24 V via SSR or relay on a signal output).
- Sensors: DS18B20 on 1‑Wire (must stay supported), PT100/PT1000 via 2× MAX31865 (planned; on J8's SPI lines, separate from the display bus), level switch.
- Remote use is via the web UI on phone/PC. A detachable screen was considered and dropped.
- Pin map: `src/board/pins.h`. GPIO 43/44 stay free (UART0 debug: 43 prints the boot log). GPIO 45/46 are strapping pins, spare.
- Every output MOSFET gate has a pull-down (outputs off in reset). Output roles: heater 1..4 (staged), pump, fan, sparge heater, alarm, safety relay (contactor).

## User's current setup

1 DS18B20, heater (SSR), 12 V pump, buzzer. Plans to add a heatsink fan.

## Software stack

espressif32 6.x (Arduino core 2.x: use the 2.x LEDC API `ledcSetup/ledcAttachPin`), LVGL 9.2 (`include/lv_conf.h`), LovyanGFX for the panel and touch, OneWire + DallasTemperature.

## Status and next steps

1. Done: project skeleton, display/touch/LVGL, I/O layer, DS18B20 reader, bring-up test screen (`src/ui/bringup.cpp`). Builds; **not yet tested on hardware** (parts not arrived). Wiring and checklist: `docs/bringup-wiring.md`.
2. Done (builds, untested on hardware): brewing core. `src/core/` is hardware-free (time passed in): settings/recipe model with defaults, PID + relay autotune, heater, pump, automatic process (`brew`), manual mode, kettle simulator. `src/hal/sensors` and `src/hal/outputs` map roles to channels. `src/app/controller` is the single action layer (buttons, console, later UI/web) with the safety gate, fan, LEDs and buzzer. `src/app/console` is a serial test console. Notes and deviations: end of `docs/open-ardbir-analysis.md`. Test steps: `docs/bringup-wiring.md`.
3. Done (builds, untested on hardware): settings, recipes and the power-loss resume point as JSON on LittleFS (`src/storage/`, ArduinoJson 7), with console commands `get/set`, `sensor`, `recipe*`, `rset/rjson`, `resume/discard`. File format and rules: `docs/storage.md`.
4. Done (builds, untested on hardware): touch UI in `src/ui/` (home, brew dashboard with prompts and chart, recipes + editor, settings, sensors, tools, output test), on the controller action layer. Layout and conventions: `docs/ui.md`.
5. Done (builds, untested on hardware): recipe import. BeerXML 1.0 (Brewfather, BeerSmith...) and MashTouch JSON via `storage::importRecipes()`; console `import` (multi-line paste) and `import selftest`. BeerXML is only an import format: it is converted once and stored as JSON (`src/storage/beerxml.*`, `textfold.*`). Mash model: fixed **mash in** → add malt → any number of **rests** → fixed **mash out** → remove malt (or no mash: boil only); every step has temperature, rest (0 = none), pump mode (off/on/rest cycles). Pump is on/off only (no speed control), on any output. Recipe format v3 (`mashIn`, `rests`, `mashOut`); v1/v2 files migrate on load via `settings::arrangeMash()`, which BeerXML import uses too. Brew screen: step strip and "+5 min". See `docs/storage.md`.
6. Done (builds, untested on hardware): network. Wi‑Fi joins the home network or opens a setup AP "MashTouch-XXXX" with captive DNS; mDNS `mashtouch.local`; JSON API and web UI on port 80 (core `WebServer`, served from `loop()`, no extra libraries; the UI polls `/api/status` at 1 Hz instead of a WebSocket). Web UI is `web/index.html`, gzipped into the firmware by `tools/embed_web.py`. Firmware update from the browser or `pio run -e ws35_ota -t upload`. Touch: Tools > Wi‑Fi (scan, connect, QR code). See `docs/web-api.md`.
7. Done (web UI checked on the PC mock with screenshots; firmware builds, untested on hardware): UX pass for BIAB. Web UI rewritten (`web/index.html`, design ideas from the BrewManiacAI webui, code written fresh): prompt card, Now/Next, brew plan, hold-to-confirm keys, recipe form editor with strike temperature calculator, settings with "Show advanced", System page. Process: boil-over warning (`process.boilWarnC`), boil-stall detection and `startTimerNow` (start a rest/boil timer by hand). PC mock for the web UI: `node tools/mock/server.js` (keep it in step with `src/net/web.cpp` when the API changes).
8. Done (web UI checked on the PC mock; firmware builds, untested on hardware): brew history (RAM ring for charts, `src/app/history`) and brew logs on flash (`src/storage/brewlog`, web History page); sparge water heater (output + sensor role, recipe `spargeTempC`); staged heaters 1..4 with a mash power limit; alarm and safety-relay outputs; runaway detection; NTP clock; I2S speaker. Summary and open points: `PROGRESS.md`.
9. Done (builds, untested on hardware): Waveshare ESP32‑S3‑Touch‑LCD‑3.5 as the board: pin map `src/board/pins.h`, `src/hal/i2cbus`, `src/hal/iox` (TCA9554 LCD reset, backpack TCA9535 buttons/LEDs), `src/hal/es8311`, console `i2c` scan. The DevKit + MSP3526 bench build was removed.
10. Next: bring up the Waveshare panel (checks in `docs/design.md`), PT100/PT1000 via MAX31865, then the KiCad I/O backpack PCB.

## Code conventions

- The toolchain is C++11 (gnu++11): no brace-init of structs that have default member values; use helper functions.
- Temperatures are in °C internally; °F is display-only.
- Nothing may block `loop()` (the loop watchdog is enabled).

## Reference

- `docs/design.md`: the full design plan (architecture, pin map, PCB, phases, verification). Keep it and this file up to date when decisions change.
- `reference/open-ardbir/`: original Open ArdBir 2.8.3‑RC sources (not compiled).
- `docs/open-ardbir-analysis.md`: process flow, control rules and function mapping.
