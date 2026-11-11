# MashTouch: progress

Status on 2026-11-12. ESP32‑S3 touchscreen brew controller for single-vessel (BIAB/RIMS) brewing, GPL‑3.0‑or‑later, based on Open ArdBir behaviour. Rules and hardware decisions: [CLAUDE.md](CLAUDE.md). Full design: [docs/design.md](docs/design.md). Which files changed when is in `git log`; update this file in the same commit as the change it describes.

## Where things stand

- **Hardware:** the controller is built on the **Waveshare ESP32‑S3‑Touch‑LCD‑3.5** panel, with an MCU-less I/O backpack on its J8 header. That is the only board: the earlier DevKit + MSP3526 bench build was removed.
- **The firmware builds cleanly** (`pio run`, environment `ws35`: RAM 25 %, flash 25 %). **Nothing has run on hardware yet**; the parts have not arrived.
- **The web UI is tested on a PC mock** (`node tools/mock/server.js`, http://localhost:8080) with headless-browser screenshots. The mock is a simplified JavaScript model, not the firmware.
- **The touch UI (LVGL) has never been seen on a screen.** It compiles; layout and touch feel are unverified.

## What is done

| # | Area | Main files |
|---|---|---|
| 1 | Skeleton, display, touch, I/O, DS18B20 | `src/hal/`, `src/board/pins.h` |
| 2 | Brewing core: PID + autotune, heater, pump, automatic process, manual mode, simulator | `src/core/` |
| 3 | Settings, recipes, power-loss resume as JSON on LittleFS | `src/storage/` |
| 4 | Touch UI: home, brew, recipes, settings, sensors, tools, Wi‑Fi | `src/ui/` |
| 5 | Recipe import: BeerXML 1.0 and JSON | `src/storage/beerxml.*`, `textfold.*` |
| 6 | Wi‑Fi (home network or setup access point), JSON API, web UI, firmware update over the network | `src/net/`, `web/index.html` |
| 7 | BIAB UX pass: web UI rewrite, boil-over warning, manual timer start, strike calculator | `web/index.html`, `src/core/brew.*` |
| 8 | Brew history and logs, sparge heater, staged heaters, alarm and safety relay, runaway detection, I2S speaker, NTP clock | `src/app/history.*`, `src/storage/brewlog.*`, `src/hal/audio.*`, `src/app/controller.*` |
| 9 | Waveshare panel as the board: pin map, shared I2C bus, expanders, ES8311 codec, console `i2c` scan | `src/board/pins.h`, `src/hal/i2cbus.*`, `iox.*`, `es8311.*` |

Serial console (`help`), PC mock and docs exist for all of it.

## Key structural decisions

**Code layout**
- `src/core/` has no hardware calls. Time is passed in, so the same code runs on a scaled clock in fake-heating mode.
- `src/app/controller` is the single action layer for the front buttons, touch UI, console and web API. It also holds safety, fan, sparge, LEDs and event sounds.
- Everything runs from `loop()` without blocking (the loop watchdog is on). The only extra task is audio playback.
- The toolchain is C++11: structs with default member values cannot be brace-initialised.

**Brewing model**
- **Mash:** fixed *mash in* → add grain → any number of *rests* → fixed *mash out* → lift the bag. A recipe can also have no mash (boil only).
- **Steps:** each has a temperature, a rest time (0 = none) and a pump mode (off / on / rest cycles).
- **Pump:** on/off only, no speed control. Any voltage: 12 V on a PWR output, or 230 V through an SSR or relay on an OUT output.
- **Heaters 1–4** share the demanded power in stages. A mash power limit caps the power while the grain is in.
- **Boil:**
  - full power up to the boil, then the boil power
  - a warning shortly before the boil
  - if the temperature stops rising, the user can start the timer by hand (from 85 °C)
- **Sparge water heater:** a thermostat on its own sensor, independent of the brew; heated automatically when the recipe has a sparge temperature.

**Safety**
- Outputs are off at boot and on a sensor fault. Only kettle, RIMS and heatsink sensors count as critical; *aux* and *sparge* do not stop the brew.
- Heater cut on over-temperature, hot heatsink, level switch, and **runaway** (the kettle heats while the heater is off).
- Optional **safety relay** output: closed only while a mode runs without a fault.

**Storage and formats**
- JSON only. BeerXML is an import format, converted once.
- Recipe format v3 (`mashIn`, `rests`, `mashOut`, `spargeTempC`); v1 and v2 files convert on load.
- Brew logs are JSON lines in `/logs/NNNN.jsonl`, appended during the brew; the newest 30 are kept.
- Files are written atomically (temp file, then rename).

**Network and web**
- Only libraries from the Arduino core, plus ArduinoJson. The web UI polls `/api/status` once a second; there is no WebSocket.
- The web UI is one file, `web/index.html`, with no framework. It is gzipped into the firmware at build time, so a firmware update also updates it.
- `tools/mock/server.js` must be kept in step with `src/net/web.cpp` when the API changes.
- No code from BrewManiacEx: its web UI served only as a design reference.

**Hardware (controller head only)**
- **Board:** Waveshare ESP32‑S3‑Touch‑LCD‑3.5 (ST7796 + FT6336; ES8311 codec and speaker; front cover frame) + an **I/O backpack without an MCU** plugged into its 2×16 header J8. Pin map: `src/board/pins.h`, with the full table in `docs/design.md`.
  - OUT1–4 on GPIO 38–41, PWR1/2 on 42/21, 1‑Wire 17, level 18, MAX31865 on J8's SPI 11/10/9 with CS 47/48.
  - Buttons and LED rings on a TCA9535 (0x22) on the shared I2C bus. Safety I/O is never on an expander.
  - The shared I2C bus (7/8) belongs to `src/hal/i2cbus`, which uses LovyanGFX's driver so the touch driver can share it. Never use `Wire` on it.
- 4 signal outputs (OUT, for SSRs and relay coils) and 2 power outputs (PWR, 12 V loads up to about 3 A), 2 buttons with LED rings, 12 V input.
- Gate pull-downs on every output, and input protection on 12 V and the sensor lines.

## Open next steps

1. **Waveshare panel bring-up** ([docs/bringup-wiring.md](docs/bringup-wiring.md), Waveshare section):
   - the `i2c` scan
   - orientation, colours and touch
   - the chime through the ES8311 (the amplifier enable is still unknown)
   - power from J8 VBUS
   - the J8 pin order

   The open checks are listed under the Waveshare pin map in `docs/design.md`.
2. Run `import selftest` on the console.
3. **Check on hardware what could only be compiled:**
   - the ES8311 codec setup and the TCA9535 front-panel layout (written from datasheets)
   - the chimes through the speaker
   - the LCD reset through the expander
   - polled touch
   - brew logs on flash
   - NTP time
   - runaway detection, which may need tuning to avoid false alarms
4. **See the touch UI on the real screen** and fix layout and spacing. The brew history has no touch screen page yet.
5. **PT100/PT1000 via MAX31865** on J8's SPI lines (its own bus, separate from the display).
6. **KiCad I/O backpack PCB** (no MCU) to the Waveshare pin map. Take the outline and holes from the vendor STEP file, and keep clear of the panel's antenna.
7. **Smaller open items:**
   - fonts with Latvian letters (imported names are folded to plain Latin letters today)
   - the web chart mixes units if °C/°F is switched mid-brew
   - the time zone setting applies only after a restart
   - microSD is unused
