# MashTouch design

## Context
The goal is a single-vessel brew controller with a better screen, a better UI and a free license that also allows commercial use. MashTouch is a **new firmware based on Open‑ArdBir** (GPLv3‑or‑later in its source header; the repo LICENSE file says GPL‑2.0), with the missing parts built on top of it.

Decisions made so far:
- **One case for everything.** The user's existing NodeMCU controller box already holds the SSRs, power supply and wiring, and will get an SSR heatsink and a cooling fan. Only the **display / controller / buttons** part gets replaced.
- **The project's hardware scope is the controller head:** a display panel + an I/O backpack PCB + 2 buttons, with output and sensor connectors compatible with the existing box wiring. The rest of the box (SSRs, heatsink, PSU, mains) is the user's own build and is only documented, not designed.
- **Display panel (decided 2026‑11‑11):** **Waveshare ESP32‑S3‑Touch‑LCD‑3.5** (V2 PCB), chosen for a finished look: a 3.5" IPS 320×480 panel with a front cover frame, and the MCU on board. It has:
  - ESP32‑S3R8 (8 MB PSRAM) + 16 MB flash, native USB‑C
  - ST7796 over SPI + FT6336 touch
  - ES8311 codec + NS4150B amplifier and a speaker connector
  - AXP2101 PMU, RTC, IMU, microSD
  - a 2×16 2.54 mm header (J8) for expansion

  The -C version adds a case and an OV5640 camera; the camera is not used.
- **Buttons:** two small 8–12 mm panel-mount momentary buttons with LED rings: **Start/Pause** and **Pump**.
- **PCB:** an **I/O backpack without an MCU** that plugs straight into the panel's J8 header and stacks behind the panel. It carries 12 V input, the 5 V feed for the panel, output drivers, sensor inputs and the button connector.
- **Remote control:** a phone or PC through the web UI. No detachable screen.
- **Sensors:** stay compatible with the existing **DS18B20** (1‑Wire, multiple on one bus) and add more modern sensor options (see below).

## Legal ground rules
- New **separate repository**, licensed GPL‑3.0‑or‑later. Keep the Open‑ArdBir copyright notices (Mathison, Wilson, Nevi) and add a NOTICE file.
- **All code is original or adapted from Open‑ArdBir.** No code, web assets or ported tables come from projects whose license is not GPL‑compatible (for example non-commercial licenses). Common behaviour and the usual brewing workflows may be reproduced, written fresh (functionality and interfaces are not protected in the EU, per CJEU C‑406/10 *SAS Institute*).
- Check every library license: LVGL is MIT, ArduinoJson is MIT, the Arduino core libraries (WebServer, DNSServer, ESPmDNS, Update) are LGPL‑2.1. The PID and autotune are written fresh (no PID library). ESPAsyncWebServer/AsyncTCP were considered and are not used.

## Firmware architecture (PlatformIO, Arduino on ESP32‑S3)
| Module | Contents |
|---|---|
| `core/brew` | Open‑ArdBir state machine reworked to be non-blocking (it uses `delay()` and LCD loops today): manual mode, auto mash steps, boil + hop alarms, delayed start, cooling, pump rest, PID + autotune |
| `hal/outputs` | **generic outputs with assignable roles**: any physical output can be set as heater 1..n, pump 1..n, sparge heater, fan, or alarm light. All safe-off at boot. Inverted logic and PWM (power outputs) configurable per output |
| `hal/sensors` | sensor abstraction (`TempSensor`: read, fault, calibration offset). **Any number of sensors** (DS18B20 bus + MAX31865 channels), each assigned a role: kettle, RIMS/HERMS out, sparge, heatsink, or display only. A PID loop can use any sensor |
| `hal/io`, `hal/iox`, `hal/audio` | outputs, buttons with long-press and LED rings (on the backpack's I2C expander), the I2S speaker through the ES8311 codec |
| `hal/i2cbus` | the Waveshare board's shared I2C bus (touch, expanders, codec, PMU), one driver for all |
| `storage` | LittleFS: settings and recipes as JSON, brew logs. The microSD slot is unused (on the Waveshare board its lines are the sensor SPI bus) |
| `net` | Wi‑Fi STA with setup AP and captive DNS, mDNS, the core `WebServer` (served from `loop()`), JSON API, embedded web UI polling status at 1 Hz, OTA (see `docs/web-api.md`) |
| `api` | REST/WS API covering what the current webui uses (status, settings, automation/recipes, actions, logs), with action names instead of button masks |
| `ui` | LVGL 9 on ST7796S + FT6336U: dashboard (temp, setpoint, timer, step, heater/pump state, chart), step list, manual control, settings, sensor setup. Large touch targets, hold to confirm for stop/skip, touch lock |

- Physical buttons: **Start/Pause** (short press = pause/resume; long press = stop/skip with on-screen confirmation) and **Pump** (short press = toggle; hold = prime pulse). A manual pump toggle in auto mode overrides until the next step. Screen, buttons and web UI share one action layer.
- **Current user setup to support from day one:** 1× DS18B20, heater, pump, buzzer. Nothing in the firmware or PCB is limited to one sensor or one of each output.
- **Pump:** on/off only, any voltage. A 12 V DC pump connects to a power output (PWR1). A 230 V pump (e.g. KegLand/RoboBrew magnetic drive) or a 24 V pump is switched by an SSR or relay on a signal output, like the heater. Flow is set with a valve on the pump outlet. No speed control: pump models vary too much (AC pumps have no speed input; brushless DC pumps need a separate control wire).
- **Fan control:** on while any SSR is active plus a run-on time. If a heatsink sensor is assigned, use thermostatic control instead, with an over-temperature alarm and heater cut.
- Safety: heater off on boot and on sensor fault, overheat/dry-run limits, and a watchdog. The existing hardware switch in the box stays the independent cut-off.

### Temperature sensors
| Sensor | Interface | Why |
|---|---|---|
| **DS18B20** (existing) | 1‑Wire, several on one bus, 4.7 k pull-up to 3.3 V | must keep working with the existing probes and connector; scan, name and assign roles in the UI |
| **PT100 / PT1000 via MAX31865** | SPI on J8 (the unused SD bus, separate from the LCD), own CS, 2 channels | the modern standard for brewing probes (stainless, tri-clamp, fast and accurate, 2/3/4‑wire); MAX31865 boards are cheap and widely available |
| Thermocouple via **MAX31856** (optional, later) | SPI, own CS | only if someone needs very high temperatures; low priority |

DS18B20 clones are common. Detect them and show read errors and fault counters in the UI, not just a stale value.

## Pin map: Waveshare ESP32‑S3‑Touch‑LCD‑3.5 + backpack
The source of truth is `src/board/pins.h` (build `pio run`, environment `ws35`). Not usable: GPIO 19/20 (native USB), 26–32 (flash), 33–37 (octal PSRAM in the ESP32‑S3R8). Sources:
- the Waveshare V2 schematic: its "PIN_OUT" table and header J8
- the vendor examples [`11_lvgl_arduino_v8`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5) (LCD defines, TCA9554 at 0x20, LCD reset on EXIO1), `01_audio_out` and `04_es8311_example` (I2S)
- the datasheets on the vendor [Resources page](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5/Resources-And-Documents)

**On the panel (fixed):**

| Function | GPIO |
|---|---|
| LCD SPI: SCLK / MOSI / MISO / DC / backlight | 5 / 1 / 2 / 3 / 6. CS is not wired to the MCU |
| LCD reset | TCA9554 EXIO1 (pulsed before the panel init) |
| Shared I2C SCL / SDA | 7 / 8: FT6336 0x38, TCA9554 0x20, ES8311 0x18, AXP2101 0x34, PCF85063 0x51, QMI8658, backpack TCA9535 0x22 |
| TCA9554 interrupt | 4 (unused, polled) |
| TCA9554 EXIO0…7 | CAM_PWDN, LCD_RST, TP_INT, SD_CD (or IMU_INT1, resistor option), RTC_INT, AXP_IRQ, SYS_OUT, PA_CTRL (NS4150B amplifier enable, set high after the codec init) |
| ES8311 I2S: MCLK / BCLK / LRCK / to codec / from codec | 12 / 13 / 15 / 16 / 14 |
| microSD (SD_MMC 1‑bit) CLK / CMD / D0 | 11 / 10 / 9 (unused; the same lines are J8's SPI) |
| USB | 19 / 20 (native USB, console and upload) |

**J8 (2×16, 2.54 mm)**, pin numbers from the V2 schematic's PinOut sheet. VBUS is the 5 V input (a diode keeps it off the USB‑C port). GPIO 17, 18, 21, 38–42 and 45–48 are the camera pins, free with no camera fitted.

| Pin | Net | Pin | Net |
|---|---|---|---|
| 1 | VBUS (5 V in) | 2 | VBAT |
| 3 | GND | 4 | GND |
| 5 | USB D− | 6 | GPIO 21 |
| 7 | USB D+ | 8 | GPIO 38 |
| 9 | ESP_SCLK (GPIO 11) | 10 | GPIO 39 |
| 11 | ESP_MOSI (GPIO 10) | 12 | GPIO 40 |
| 13 | ESP_MISO (GPIO 9) | 14 | GPIO 41 |
| 15 | GPIO 17 | 16 | GPIO 42 |
| 17 | GPIO 18 | 18 | GPIO 45 |
| 19 | IO0 (BOOT) | 20 | GPIO 46 |
| 21 | ESP_EN | 22 | GPIO 47 |
| 23 | PWRON | 24 | GPIO 48 |
| 25 | I2C SCL (GPIO 7) | 26 | UART0 TX (GPIO 43) |
| 27 | I2C SDA (GPIO 8) | 28 | UART0 RX (GPIO 44) |
| 29 | GND | 30 | GND |
| 31 | VCC3V3 | 32 | VCC3V3 |

The schematic doesn't show where pin 1 sits on the board. Take that from the vendor 2D drawing (`reference/waveshare/`) before laying out the PCB.

**Backpack allocation:** heater and pump outputs and the level switch use direct GPIOs with gate pull-downs. Only the front panel goes through an expander.

| Function | GPIO |
|---|---|
| Signal outputs OUT1–OUT4 | 38 / 39 / 40 / 41 |
| 12 V power outputs PWR1 / PWR2 (PWM) | 42 / 21 |
| 1‑Wire sensors / level sensor | 17 / 18 |
| MAX31865 SPI SCK / MOSI / MISO | 11 / 10 / 9 |
| MAX31865 CS1 / CS2 | 47 / 48 |
| Buttons Start/Pause, Pump and their LED rings | backpack TCA9535 at 0x22, port 0: P00, P01 buttons (to GND, 10 k pull-ups); P02, P03 LEDs (high = on) |
| Spare | 45, 46 (strapping pins: outputs with pull-downs only); 43/44 debug UART |

**Checks on the first panel:**
- J8 pin 1 position. The numbers are settled (see above), but neither the drawing nor the STEP marks pin 1. With USB plugged in, find the 3.3 V pair (31/32) at one end of the header. At the other end, pin 1 (VBUS) reads about 5 V, and pin 2 (VBAT) reads 0 V with no battery fitted.
- touch reset follows the LCD reset
- the start chime plays (amplifier enabled on EXIO7)
- ES8311 address
- the board powers up from J8 VBUS without pressing PWR (AXP2101)
- the console `i2c` scan matches the addresses above
- display orientation and colours (`MT_LCD_ROTATION`, `MT_LCD_INVERT`, `MT_LCD_BGR`)

**Panels compared (2026‑11):**
- **Guition JC3248W535** (closed case, JST connectors): its AXS15231B QSPI display needs a new driver (none in LovyanGFX), and it has only about 12 GPIOs on cables.
- **OpenNextion ONX3248G035:** 14 GPIOs on a 0.5 mm FPC, all shared with camera, mic or UART1; no documented mounting.
- **Waveshare** won on its stacking header and on using the same display and touch chips as the MSP3526 the firmware was first written for, so the display code carried over.

## I/O backpack PCB (KiCad)
- **No MCU on the backpack.** The ESP32‑S3, USB‑C, Wi‑Fi antenna, display and speaker amplifier are on the Waveshare panel. The backpack plugs into the panel's J8 and stacks behind it. J8 on the panel is a **surface-mount female** 2×16 2.54 mm header (STEP part `HEADER-SMD-F-2_54-2X16PIN`), so the backpack carries a **male** 2×16 pin header.
- Mechanical data is in the vendor 2D/3D files (`ESP32-S3-Touch-LCD-3.5-2Dand3D.zip`, extracted in `reference/waveshare/`; not in git, see `.gitignore`). `ESP32-S3-Touch-LCD-3.5.*` is the bare board with its front frame and `-C.*` is the fully cased version. Both come as `.dwg` and `.pdf`; there is one `.stp` (bare board) to import into KiCad. From the bare-board drawing, seen from the back:
  - **Mounting holes:** 4× **M2**, on a 72.00 × 48.50 mm rectangle. The cased version uses the same pattern.
  - **J8:** along the edge opposite the three side buttons. Its outer row lines up with the two nearby holes, and the header is centred between them (36.00 mm from each). The speaker and battery connectors sit just left of it.
  - **Frame:** 92.44 × 61.00 mm, R6 corners. The cased version is 95.11 × 63.67 × 14.1 mm.
  - **No pin 1 mark:** the drawing draws all 32 pads square, and the STEP part is a generic model. Check pin 1 on the board (see the checks under the pin map).
- M2 standoffs: the same screws hold the panel and the backpack to the box front. Take the stack height from the STEP: the SMD socket height plus the tallest part on the panel's back.
- **Antenna:** the schematic shows a ceramic chip antenna (CA‑C03), plus a U.FL socket selected by a 0 Ω resistor. Find the antenna on the board and keep the area clear. No copper pour, tall parts or metal on the backpack near the panel's antenna end. Check Wi‑Fi range in the real box early, especially if the box is metal.
- **USB‑C on the panel stays reachable** for flashing and the console, or rely on firmware updates over Wi‑Fi.
- **Power input: 12 V** (the same supply as the pump and fan). A buck converter 12 V → 5 V (at least 1.5 A) feeds J8 VBUS, which powers the panel (through its AXP2101) and the SSR drive. Logic on the backpack (MAX31865, TCA9535, 1‑Wire pull-up) runs from the panel's VCC3V3 on J8, so the logic levels match the ESP32. This allows one 12 V supply in the box instead of separate 5 V and 12 V supplies.
- **Outputs stay off in reset:** every output MOSFET gate has a 10 kΩ pull-down, so heater, pump and the other outputs are off while the MCU is in reset, being flashed or unpowered, and while the panel is unplugged. Each output has an indicator LED.
- **Front panel expander:** a TCA9535 (address 0x22) reads the two buttons (10 k pull-ups, ESD protection) and drives the two LED rings (through transistors if the rings need more than a few mA). The firmware polls it every 10 ms, and a missing expander only disables the buttons.
- **4 signal outputs (OUT1–OUT4)** for SSRs and other low-current loads: low-side MOSFET (2N7002/AO3400) switching to GND, with the + supply on the connector, the same scheme as the current 2N2222 stages. A jumper selects +5 V or +12 V for the SSR supply (SSR inputs typically accept 3–32 V). Up to about 200 mA each. A signal output can also drive a **relay coil** (for example to switch a 230 V pump), so each signal output gets a flyback diode. For a pump on an SSR, add an RC snubber or varistor across the SSR output in the box wiring (motors are inductive loads).
- **2 power outputs (PWR1/PWR2)** for 12 V loads such as the DC pump and fan: logic-level MOSFET in a DPAK/PowerPAK package, rated about 3 A continuous, with flyback diode and fuse/PTC. PWM capable, used for quiet fan control; a 12 V pump on a power output is switched on/off.
- **Sensor inputs:** 1‑Wire bus connector (several DS18B20 in parallel, 4.7 k pull-up to 3.3 V, with ESD protection and a spare second connector for convenience) and the level sensor input. 2× MAX31865 on board (or headers for the common breakout boards) with 2/3/4‑wire PT100/PT1000 terminals.
- Connector types don't matter (the user will adapt the wiring); use screw terminals or pluggable terminal blocks.
- Buttons: a JST connector to the two panel-mount buttons and their LEDs.
- **Speaker instead of a buzzer:** the panel's ES8311 codec and NS4150B amplifier drive a small 8 Ω speaker of 1–2 W (about 28 mm round or 20×30 mm) on the panel's MX1.25 speaker connector. Mount the speaker on the box wall behind a few sound holes. The firmware synthesises its chimes (`src/hal/audio`), so every event has its own sound; the volume is a setting.
- **Input protection:** a fuse or PTC, a reverse-polarity MOSFET and a TVS diode on the 12 V input. ESD protection on the 1‑Wire and level inputs and the button connector. A 100 Ω series resistor on the 1‑Wire data line.
- **Safety relay (contactor) output:** any output can have the role *Safety relay*. It drives a relay or contactor in the mains feed of the heater SSRs and is closed only while a mode runs without a fault. An SSR that fails closed (their usual failure) can then no longer heat. Fit this relay in the box; an independent thermal cut-out on the kettle is still recommended.
- **Clock:** the time comes from the network (NTP), or from the browser when there is no internet; it is only needed for the dates in the brew history. The panel's PCF85063 RTC is not used yet.

## Outputs and roles
Any output can take any role (Settings > Outputs).

| Role | Behaviour |
|---|---|
| Heater 1…4 | Kettle heaters. The demanded power is shared in stages: with two heaters, 0–50 % switches heater 1 and 50–100 % adds heater 2 on top; their switching windows are offset. A *mash power limit* caps the power while the grain is in the kettle. |
| Pump | On/off. A 12 V pump on PWR1/PWR2, or a 230 V pump through an SSR or relay on OUT1–4. |
| Fan | PWM on a power output; thermostatic with a heatsink sensor, otherwise on while heating plus a run-on time. |
| Sparge water heater | Thermostat on the sensor with the *sparge* role. Switched by hand, or heated with the brew when the recipe has a sparge temperature. |
| Alarm | Light or horn: on at a fault, blinking while a prompt waits. |
| Safety relay | See above. |

**OUT vs PWR:** OUT1–4 only switch a control signal (SSR input, relay coil; up to about 200 mA) and the SSR or relay switches the load at any voltage. PWR1–2 power a 12 V load directly, up to about 3 A.

**Safety in the firmware:**
- Heater, pump and sparge heater go off on a sensor fault.
- The heater is cut on over-temperature, a hot heatsink or the level switch.
- **Runaway detection** cuts everything and opens the safety relay if the kettle keeps getting hotter while the heater is off.
- A watchdog restarts a stalled controller with all outputs off.

## Out of scope (documented only)
- Box mechanics, SSRs, heatsink, fan mounting, PSU, mains wiring: the user's own existing build. The repo includes a wiring diagram showing how the controller PCB connects to them.

## Phases
1. [in progress] Buy a Waveshare ESP32‑S3‑Touch‑LCD‑3.5, one MAX31865 breakout, a TCA9535 breakout and test parts (LEDs, buttons, a DS18B20, a speaker). The GPL repo exists. (An ESP32‑S3 DevKit + MSP3526 bench rig was planned first and dropped once the Waveshare panel was chosen.)
2. [code done, untested on hardware] Firmware bring-up on a breadboard: display, touch, LVGL test screen, outputs, DS18B20. MAX31865 still to do.
3. [code done, untested on hardware] The brewing core as non-blocking code (`src/core/`, `src/app/controller`, serial console); settings, recipes and power-loss resume on LittleFS (`src/storage/`, see `docs/storage.md`); the touch UI (`src/ui/`, see `docs/ui.md`); recipe import from BeerXML and JSON (see `docs/storage.md`).
4. [code done, untested on hardware] Brew history and logs, sparge heater, staged heaters, alarm and safety-relay roles, runaway detection, I2S speaker (see `PROGRESS.md`). Network: Wi-Fi with setup access point, mDNS, JSON API, own web UI (written fresh), recipe import, firmware update over the air (see `docs/web-api.md`). Next: PT100 via MAX31865.
5. [code done, untested on hardware] Waveshare panel support (`ws35` build: pin map, shared I2C bus, expanders, ES8311). Bring up the panel and settle the checks listed under its pin map.
6. KiCad I/O backpack PCB; fit it into the existing box; first real brew.

## Verification
- Each phase builds with `pio run`. Bench tests use LEDs on the outputs, a DS18B20 and a PT100 in the same cup of water, comparing their readings.
- Fake-heating mode for UI/logic tests; then a real water run (mash steps, pump rest, boil alarms, fan), checking the outputs against the screen and the web UI.
- Wi‑Fi RSSI check with the panel and backpack mounted in the closed box.
