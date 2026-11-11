# Touch UI

LVGL 9.2 on the 480×320 panel of the Waveshare ESP32‑S3‑Touch‑LCD‑3.5. The code is in `src/ui/`:

- `ui`: top bar, screen switching, °C/°F, temperature history
- `kit`: widgets and popups
- `screen_*.cpp`: one file per screen
- `bringup`: the output test

Every action goes through `app/controller`, the same action layer used by the front buttons and the serial console.

## Layout

- **Top bar** (40 px), from left to right:
  - Home button and screen title
  - Icons for fault (red), fake heating (SIM), heater on and pump on
  - Current temperature
- **Screen area** (480×280) below it. Changing screen deletes the old one, so a screen keeps no state of its own; everything comes from `controller::status()` and the settings.
- **Automatic switching:** the brew screen opens when a mode starts (from the touch screen, the console or a resume). Home returns when the mode ends.

## Screens

| Screen | Contents |
|---|---|
| Home | Temperature and active recipe (tap it to open Recipes). An *unfinished brew* banner with Resume and Discard. Tiles: Brew (start now or delayed), Manual, Recipes, Sensors, Settings, Tools. While a mode runs, Brew becomes *Back to brew*. |
| Brew | See below. |
| Recipes | Stored recipes; the active one is ticked. Tap one to use it, use and edit it, or delete it. New (from the default template), Copy active, Edit active. |
| Edit recipe | Edits the active recipe; each change is validated and saved at once. Name, a *Mash* switch (off = extract, boil only), then the mash as a timeline: **Mash in** (fixed) → *add malt* → **rests** (any number: name, temperature, rest time, pump mode, move up/down, delete; "+ Add rest") → **Mash out** (fixed; the last rest's temperature with 0 min = no heating) → *remove malt*. Mash in and mash out cannot be deleted or moved. Then the boil time and additions (name, time before the end of the boil, amount, unit, delete). |
| Sensors | Every sensor found or configured, with a live reading. Role (kettle, RIMS, heatsink, aux), calibration offset, name, and forget. Rescan the bus. |
| Settings | Sections: Heater, Pump, Process, Fan, Safety, Autotune, Outputs (role, number and inverted for OUT1–4 and PWR1–2), Display (°F, brightness, reset to defaults) and Simulator. Fields are a table of setting paths, so they get the same validation as the console's `set`. |
| Tools | Fake heating on/off and its temperature. Output and button test (idle only; leaving it switches every test output off). Prime the pump. PID autotune. Version, storage, memory and uptime. |

## Brew screen

```
┌ main card (tap: setpoint) ──────────┐ ┌ status ─────────┐
│ Rest 1/1: Saccharification         │ │ ⚡ pid 34%      │ tap: boil power
│ 65.8°C                    → 66.0°C  │ │ ⟳ on 100%       │
│ 42:10 (tap: time)              left │ │ Fan 100%        │
│ ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬ progress          │ │ Next: Mash out  │
└─────────────────────────────────────┘ └─────────────────┘
 Mash in › +malt › [Saccharification] › Mash out › -malt › Boil › Cool
[ chart: temperature and setpoint, last 20 minutes ]
[ ⏸ ] [ Pump ] [ +5 min ] [ Skip (hold) ] [ Stop (hold) ]
```

- The temperature turns green once the step temperature is reached.
- The buttons depend on the mode:
  - Automatic: Pause/Resume, Pump (override until the next step), **+5 min** (lengthens the current rest or boil, e.g. for a slow conversion), Skip, Stop. A **step strip** above the chart shows the whole brew (mash in, add malt, rests, mash out, remove malt, boil, cool) with the current step highlighted and finished ones dimmed.
  - Manual: Heat on/off, Pump, Timer, Stop.
  - Autotune: Pump, Stop.
- **Skip and Stop need a long press (0.8 s).** A short tap only shows a hint.
- **Prompts** open as a large panel over the chart and buttons:
  - water added
  - add malt
  - iodine test
  - remove malt
  - whirlpool
  - cooling
  - finished

  The Start button on the front panel answers *yes* to the same prompts. Its skip/stop confirmation appears as a red panel.
- **Notices:**
  - Hop and other additions, a finished countdown and autotune results open a message with OK.
  - Temperature reached and faults show a short toast.
  - Shortly before the boil a toast warns of a boil-over (`process.boilWarnC`).
- **A kettle that boils below the boil setting** (altitude, sensor offset): when the temperature has stopped rising for 4 minutes in the boil phase, a question offers to start the boil timer. Tapping the timer while a mash step or the boil is still heating offers *Start the timer now* as well.

## Sound

A small speaker on an I2S amplifier replaces the buzzer (`src/hal/audio`). Chimes are synthesised, one per kind of event:
- start-up
- temperature reached (one ding)
- step finished (ding-dong)
- prompt (three rising notes, with a short reminder every 30 s)
- addition (repeated pattern)
- boil-over watch (urgent beeps)
- brew finished (melody)
- fault (low tones)

The volume is in Settings > Display.

## Sparge, heaters and safety on the touch screen

- Settings has the mash power limit, the Sparge section, the runaway cut and the sound volume. Output roles include *contactor* (the safety relay); the heater number 1–4 selects the stage.
- Sensors has the *sparge* role.
- Tools shows a sparge heater switch and its temperature when a sparge output and sensor exist.
- The recipe editor has the sparge water temperature.
- While the sparge heater is enabled, the info line of the brew screen shows its temperature.
- The brew history is on the web UI only.

## Conventions

- Touch targets are at least 48 px high. Values are edited in a popup with −/+ (hold to repeat) and −−/++ (×10). Text uses the on-screen keyboard.
- Temperatures are stored in °C. With *Fahrenheit* on, every temperature and temperature difference is shown and edited in °F.
- `kit::on` and `kit::onClick` take C++ lambdas. Anything that deletes widgets from inside an event goes through `kit::later` or `ui::go`.
