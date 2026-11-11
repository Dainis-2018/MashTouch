# Open ArdBir analysis

What MashTouch takes from Open ArdBir 2.8.3-RC ([reference/open-ardbir/](../reference/open-ardbir/)), and what changes.

Open ArdBir is an Arduino Uno sketch: about 2,700 lines in `Open-ArdBir.ino`, plus helpers (`Funzioni.h`), recipes (`Recipe.h`) and one LCD text file per language. Every phase of the brew is a **blocking `while` loop** that reads the sensor, drives the outputs, polls 4 buttons and redraws a 16×2 or 20×4 LCD. All settings and 10 recipes are stored as bytes at fixed EEPROM addresses.

MashTouch keeps the **brewing behaviour** and rewrites the code as non-blocking state machines, so the touch UI, web server and sensors keep running at all times.

## Brewing process (automatic mode)

| # | Phase | Open ArdBir behaviour | Function |
|---|---|---|---|
| 1 | Water check | prompt "water added?" | `auto_mode` → `prompt_for_water` |
| 2 | Pump prime | 5 pump bursts of increasing length | `pump_prime` |
| 3 | Delayed start | optional countdown (15 min – 24 h) before heating | `WaitStart` |
| 4 | Mash in | heat to mash-in temperature; on reaching it, beep and wait for confirmation | `stage_loop`, `Temperatura_Raggiunta` |
| 5 | Add malt | pump off, prompt to add grain (can be skipped in settings) | `add_malt` |
| 6 | Mash rests | up to 6 fixed rests: phytase, glucanase, protease, β‑amylase, α‑amylase 1 and 2. The step timer starts only once the temperature is reached. Long press Enter skips a step | `stage_loop` |
| 7 | Iodine test | optional pause before mash-out, with timeout | `Iodine_Test` |
| 8 | Mash out | last rest | `stage_loop` |
| 9 | Remove malt | prompt to lift the malt pipe; heater can keep PID or stay off | `remove_malt` |
| 10 | Boil | heat to boil temperature, then fixed boil power %; up to 10 hop alarms by minutes remaining | `stage_loop`, `hop_add` |
| 11 | Cooling | prompt, then pump control until a target temperature | `Cooling` |
| 12 | Whirlpool | optional, hot (before cooling) or cold (after), 1–10 min with pump | `Whirlpool` |
| 13 | End | clear resume data | `End` |

**Resume after power loss:** the current step, remaining minutes and hop index are saved every minute; on the next start the user is offered to continue.

## Heater control (`PID_HEAT`)

- **Electric mode:** full power while more than 3.5 °C below the setpoint (1 °C near boil). Inside that band, a PID loop (kP, kI, kD, sample time from settings) drives a **time-proportional output**: a window of `WindowSize × 250 ms` in which the heater is on for `output / 255` of the time.
- **Gas mode:** stepped output (0, ~16 %, then proportional) with hysteresis, longer windows.
- **Boil:** once the temperature reaches the boil setpoint, the PID is bypassed and the output runs at the fixed *boil power %*, adjustable during the boil.
- **Sensor calibration:** offset −5.0…+5.0 °C.

MashTouch keeps this model (full power far from the setpoint, PID near it, fixed % in boil) and adds **PID autotune** and per-sensor calibration.

## Pump control (`pump_rest`, `pump_control`)

- Settings: pump enabled before mash, during mash, during mash-out, during boil.
- **Pump rest cycle** during mash: run for *cycle* minutes, stop for *rest* minutes. The heater is also off during the rest. The cycle restarts early if the temperature drops more than a threshold.
- **Pump stop temperature:** settable 80–105 °C; above it the pump stops to avoid cavitation near the boil. The behaviour differs for an *internal* sensor (in the kettle) and an *external* one (in the pump outlet).
- Manual pump toggle with the Enter button in manual mode and cooling.

MashTouch keeps these rules per step, adds the front **Pump** button with "manual override until next step".

## Manual mode (`manual_mode`)

Adjustable setpoint, heater and pump toggles, count-up timer once the temperature is reached, optional countdown timer, and boil power % when the setpoint is at or above the boil temperature. Kept as is, with touch controls.

## Settings (EEPROM map in the `.ino` header)

PID (gas/electric, kP/kI/kD, sample time, window, boil %, calibration, hysteresis), units °C/°F, sensor position (internal/external), boil temperature, pump settings, skip add/remove malt, iodine test and timeout, whirlpool mode.

MashTouch stores settings and recipes as **JSON on LittleFS** instead of fixed EEPROM addresses.

## What changes in MashTouch

| Open ArdBir | MashTouch |
|---|---|
| 8 fixed mash steps with enzyme names | any number of steps, mash in, any number of rests, mash out; each with temperature, time and pump mode (on/off only) |
| 10 recipes × 10 hops, minutes only | unlimited recipes (flash space), hops and other additions with name and amount |
| 1 DS18B20, 1 heater, 1 pump | any number of sensors and outputs, assigned by role |
| blocking loops, `delay()` | non-blocking state machines driven from `loop()` |
| 4 buttons + LCD, 6 languages | touchscreen + 2 buttons + web UI (English first, translations later) |
| combos like Up+Down = quit | explicit on-screen actions, hold to confirm |
| no network | Wi‑Fi, web UI, logs, OTA |
| — | fan control, level sensor, PT100/PT1000, safety limits (sensor fault → heater off) |

## Function mapping

| Open ArdBir | MashTouch module |
|---|---|
| `Temperature`, `dsInizializza` | `hal/temp_onewire`, role mapping and faults in `hal/sensors` |
| `heat_on/off`, `pump_on/off`, `Buzzer` | `hal/io`, role mapping in `hal/outputs`, beeps in `app/controller` |
| `PID_HEAT`, `load_pid_settings` | `core/heater`, `core/pid` (PID + relay autotune) |
| `pump_rest`, `pump_control`, `pump_prime` | `core/pump` |
| `auto_mode`, `stage_loop`, `hop_add`, `add_malt`, `remove_malt`, `Iodine_Test`, `Cooling`, `Whirlpool`, `WaitStart` | `core/brew` (automatic process state machine) |
| `manual_mode` | `core/manual` |
| `Timing`, `start_time` | `core/timer` (pausable step timer) |
| `r_set` / `s_set`, `Recipe.h` | `storage/settings`, `storage/recipes` |
| LCD language files, `Presentazione.h`, `ArdBir1.h` | not used: replaced by the LVGL UI |

## Implementation notes (brewing core, done)

- `core/` holds no hardware calls: every module takes the time as an argument, so the same code runs on a scaled clock in fake-heating mode (and can later run in host unit tests).
- `app/controller` is the one action layer for the front buttons, the console, and later the touch UI and web API. It also holds the safety gate, fan control, LED rings and buzzer.
- Differences from Open ArdBir:
  - The mash has a fixed frame with a free middle (Open ArdBir has fixed mash-in, 6 enzyme rests and mash out): **mash in** → *add malt* → any number of **rests** → **mash out** → *remove malt*. Each step has a temperature, a rest time (0 = no rest) and a pump mode. Mash in and mash out always exist, so add malt and remove malt cannot be forgotten or misplaced. The iodine test is a setting (off by default) and runs before the mash out; a "+5 min" button covers the usual need to extend a rest.
  - Pause stops the timers, heater and pump.
  - Pump rest restarts after a fixed temperature drop (`pump.restRestartDropC`), not a formula.
  - Below the boil setpoint the heater runs at full power. From the boil temperature it runs at boil %, with hysteresis.
- Boil: Open ArdBir only starts the boil timer at the boil temperature setting. MashTouch also warns shortly before the boil (boil-over), notices when the kettle is hot but no longer getting hotter, and lets the user start the timer by hand; the heater then holds the boil power at that temperature.
- Sensor faults only stop the brew for sensors with the kettle, RIMS or heatsink role. An aux (display-only) sensor fault does not.
- Power-loss resume: the controller saves `brew::Process::snapshot()` with a copy of the recipe on every phase or step change and once a minute, and offers it at boot (see `docs/storage.md`).
