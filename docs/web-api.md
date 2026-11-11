# Network, web UI and API

Code: `src/net/wifi.*` (Wi‑Fi), `src/net/web.*` (server and API), `web/index.html` (web UI).

## Wi‑Fi

- **Home network:** the controller joins the network in `net.ssid` / `net.password` and is reachable at **http://mashtouch.local** (`net.hostname`) or its IP.
- **Setup access point:** when no home network is set, or it cannot be reached within 20 s, the controller opens **MashTouch-XXXX**. The password is `net.apPassword` (default `mashtouch`; shorter than 8 characters means an open network). The web UI is at http://192.168.4.1.
  - A captive DNS answers every name with the controller, so phones offer to open the page by themselves.
  - The Wi‑Fi screen on the touch display shows a QR code for joining.
- **Retries:** while the access point is open, the controller retries the home network every 2 minutes, but only when no phone is connected to the access point (retrying makes the access point change channel).
- **Closing the access point:** it closes 30 s after the home network connects, once nobody is using it.
- **Where to set it up:**
  - the touch screen (Tools > Wi‑Fi: scan, pick, password)
  - the web UI (System tab)
  - the console (`set net.ssid "My net"`, `set net.password "..."`, `wifi connect`)
- **Credentials** are stored in `settings.json`, not in the ESP32's Wi‑Fi memory. The API never returns passwords.

## Web UI

One page embedded in the firmware: `web/index.html`, gzipped into `src/net/webui_data.h` by `tools/embed_web.py` before every build. It is plain HTML, CSS and JavaScript with no framework. A firmware update therefore also updates the UI, and nobody has to flash the filesystem (which would erase settings and recipes).

**Layout:**
- **Header:** sections as tabs on wide screens, a menu on phones, the current temperature, Wi‑Fi bars, a sound switch, and a theme switch (automatic, light or dark; remembered per browser).
- **Offline banner:** shown when the controller stops answering.
- **Notices:** pop up bottom-right, or at the top on phones. Alarms (hop additions, countdown, faults) stay until dismissed and play a sound.

| Page | Contents |
|---|---|
| Brew | **Prompt card** with large buttons and a line on what happens next. **Main card:** mode, stage ("Heating strike water", "Add the grain", "Heating to the boil"...), large temperature, time left, an estimate of the time to the setpoint, heater and pump chips (tap the setpoint or boil power to change it), and **Start timer now** while a mash step or the boil is still heating. **Controls:** when idle, start, delayed start (pick a clock time) and manual; during a brew, pause (everything off), pump, +5 min, skip and stop. Skip and Stop must be held for 1 s. On phones the controls stay pinned to the bottom. **Now / Next** with a progress bar and the estimated time to the end of the boil; when idle, a recipe picker and the estimated brew time. **Brew plan:** the whole brew with done and current steps, including additions. **Temperature chart:** the last 30 minutes, 2 hours or 6 hours, read from the controller, so it is complete after a reload. With a sparge heater: a sparge chip (temperature, target, on/off). With several heaters: how many are on. |
| History | One card per logged brew (date, duration, result). A log opens as a chart of the whole brew with event markers and the event list; download as CSV; delete. |
| Recipes | Cards with a summary and the active recipe marked; use, edit, download, delete. Import BeerXML or JSON with the button, or drop files on the page. **Editor:** a form with name, mash on/off, mash in → *add malt* → rests (add, move, delete) → mash out → *remove malt*, boil time, and additions (name, minutes before the end, amount, unit). A **strike temperature calculator** works out the mash-in temperature from the target mash temperature, the water volume, the grain weight and the grain temperature. A save bar holds unsaved changes. The JSON view is kept under "Advanced". |
| Sensors | One card per sensor: live reading, address, read errors, name, role, calibration; save, forget, rescan. |
| Settings | Cards per section, each field with a short explanation, toggle switches and −/+ number fields. Everyday settings show by default; **Show advanced** adds the expert ones (PID details, hysteresis values, rest-cycle timing, autotune, simulator). Temperatures follow the °C/°F setting. Changes collect in a save bar (Save / Discard), and fields that are refused are marked. Output roles for OUT1–4 and PWR1–2. Reset to defaults keeps Wi‑Fi. |
| System | Wi‑Fi status with signal, scan and connect, forget. Firmware update (choose or drop `firmware.bin`, with a progress bar). Tools: prime the pump, PID autotune, fake heating. Controller info and restart. |

**Not missing a prompt:** while a prompt or a fault is waiting, every other page shows a banner that links to Brew, the browser tab title changes, and the prompt sound repeats every 30 s (phones also vibrate). While a brew runs, the tab title shows the temperature, the stage and the time left.

Dialogs replace the browser's `prompt()`/`confirm()`: number entry with −/+, text entry, and confirmations.

The page polls `/api/status` once a second. A WebSocket push was planned. With a single 1 s poll, the server stays a simple loop-driven one with no extra library, and that is fast enough for brewing.

## Security

The controller is meant for the home network.

- **Web password:** `net.webPassword` sets a password (HTTP basic auth, user `admin`) for every request that changes something, and for reading the settings. Status, recipes and Wi‑Fi status stay readable.
- **No HTTPS.**

## Firmware update

- **From the browser:** System tab, choose `.pio/build/devkit/firmware.bin`, Upload.
- **From PlatformIO:** `pio run -e devkit_ota -t upload` (task "PIO: Upload", env `devkit_ota`). It sends the build with curl to `http://mashtouch.local/api/ota`. If `.local` names do not resolve on the PC, put the IP in `upload_port`. With a web password, add `-u admin:<password>` to `upload_command`.

Updates are refused while a mode is running. The controller switches all outputs off and restarts after a successful update. The 16 MB flash has two app slots, so a failed update leaves the old firmware running.

## API

All bodies and responses are JSON unless noted. Errors: HTTP 4xx/5xx with `{"error": "..."}`. 🔒 = needs the web password when one is set.

### Status

- `GET /api/status`: everything the screens show:
  - `mode` (`idle`, `auto`, `manual` or `autotune`), `paused`, `faults` / `faultText`, `confirm` (`""`, `skip` or `stop`)
  - `temp` (°C or null), `setpoint`, `reached`
  - `heater {zone, duty, on, boilPower, count, stages[]}` (stages: which heater outputs are on), `sparge {available, on, heating, temp, setpoint}`, `clock` (controller clock, s), `time` (Unix time, 0 = not set)
  - `pump {on, resting, override, priming}`, `fan`
  - `phase` / `phaseName`, `step` and `stepCount` (mash sequence: 0 = mash in, last = mash out), `stepName`
  - `timer {remainingMs, elapsedMs, counting}`, `prompt` / `promptText`, `lastAddition`
  - `manual {heater, pump, countdown}`, `tune {cycles, of}`, `resume {offered, phase, step}`, `sim {on, speed}`
  - `fahrenheit`, `recipe`, `activeRecipe`, `wifi {connected, rssi, apOn}`
  - `sensors[] {name, role, present, ok, temp, offsetC, errors, rom}`
  - `eventSeq` and `events[] {seq, type, text}`: the last 12 events. Types are those of `core/events.h`, such as `addition`, `temp-reached`, `prompt`, `sensor-fault`, `brew-finished`, `boil-soon` (boil-over watch) and `boil-stalled` (hot but no longer rising: offer `startTimer`).
- `GET /api/history`: the rolling temperature history for the charts, 6 hours at one sample per 10 s of controller time, as parallel arrays: `{clock, step, t[], temp[], sp[], duty[]}`. `t` is the controller clock in seconds (the same clock as `clock` in the status); `temp` and `sp` are tenths of a degree C, or null.
- `GET /api/info`: version, build date, free heap/PSRAM, storage use, uptime.
- `GET /api/brew/recipe`: the recipe of the running brew (its own copy).

### Actions 🔒

`POST /api/action` with `{"action": "...", ...}`:

| Action | Fields | |
|---|---|---|
| `startAuto` | `delayMin` | automatic brew with the active recipe, optionally delayed |
| `startManual` | | manual mode |
| `startAutotune` | `tempC` | PID autotune |
| `resume`, `discardResume` | | brew interrupted by power loss |
| `stop`, `pauseResume`, `skip` | | pause switches the heater and pump off and stops the timers |
| `startTimer` | | start the rest or boil timer now although the setpoint is not reached; in the boil (from 85 °C) the setpoint drops to the current temperature and the heater goes to the boil power |
| `answer` | `yes` | open prompt, or the skip/stop confirmation |
| `pumpToggle`, `pumpPrime`, `heaterToggle` | | |
| `setpoint` | `tempC` | manual setpoint, or the current step's temperature |
| `boilPower` | `pct` | |
| `timer` | `min` | manual countdown, or the remaining minutes of the step |
| `addMinutes` | `min` (default 5) | lengthen the current step |
| `sparge` | `on`, `tempC` (either or both) | sparge water heater thermostat; needs an output and a sensor with the sparge role |
| `sim` | `on`, `speed` | fake heating |
| `simTemp` | `tempC` | |

A refused action (for example no sensor reading) returns HTTP 409 with the reason.

### Settings 🔒

- `GET /api/settings`: the settings JSON (as in `settings.json`, see docs/storage.md). Passwords come back empty, with `passwordSet`, `apPasswordSet` and `webPasswordSet` flags.
- `POST /api/settings`: one change `{"path": "heater.kp", "value": 25}`, or several: `{"changes": [{"path": ..., "value": ...}, ...]}`. The same validation as the console's `set`. The response lists `errors[] {path, error}`.
- `POST /api/settings/reset`: defaults, keeping the network settings.

### Recipes

- `GET /api/recipes`: `[{id, name, active}]`.
- `GET /api/recipes/{id}`: the recipe JSON.
- `POST /api/recipes/{id}` 🔒: save a recipe JSON object under this id (validated).
- `DELETE /api/recipes/{id}` 🔒: not the active one.
- `POST /api/recipes/{id}/use` 🔒: make it active.
- `POST /api/import` 🔒: the body is a BeerXML file or MashTouch JSON (one recipe or an array), sent as plain text. Each recipe is added as a new one. Response: `{ok, ids[], notes[]}`.

### Brew logs

- `GET /api/logs`: `[{id, recipe, start, sim, durationS, result, size, active}]`, newest first. `result` is `finished`, `stopped` or empty (cut off, or still running).
- `GET /api/logs/{id}`: the log file, one JSON object per line (format in docs/storage.md).
- `DELETE /api/logs/{id}` 🔒: not the one being written.
- `POST /api/time` 🔒: `{"epoch": 1794300000}` sets the clock when it is not set (no NTP in access-point mode). The web UI does this by itself.

### Sensors 🔒

- `POST /api/sensors/{index}`: `{"role": "kettle", "offsetC": -0.3, "name": "Kettle"}` (any of them). Roles: `none`, `kettle`, `rims`, `heatsink`, `aux`, `sparge`.
- `POST /api/sensors/{index}/forget`: remove the stored settings of that sensor.
- `POST /api/sensors/rescan`.

### Wi‑Fi

- `GET /api/wifi`: `{connected, ssid, ip, rssi, hostname, apOn, apSsid, apIp, apClients, scanning, networks[] {ssid, rssi, open}}`.
- `POST /api/wifi/scan` 🔒: start a scan; poll `GET /api/wifi` until `scanning` is false.
- `POST /api/wifi` 🔒: `{"ssid": "...", "password": "..."}`: save and connect.
- `POST /api/wifi/forget` 🔒.

### System 🔒

- `POST /api/ota`: multipart form with the firmware file in field `firmware`.
- `POST /api/reboot`.

Any other path that does not start with `/api/` returns the web UI (this also answers phone captive-portal checks).

## Trying the web UI on a PC (mock)

`tools/mock/server.js` imitates the controller so the web UI can be tried without hardware. It needs only Node.js.

```
node tools/mock/server.js            # http://localhost:8080, kettle at 20x speed
node tools/mock/server.js --port 8090 --speed 60
```

In VS Code: Terminal > Run Task > **Mock: web UI on this PC**.

It serves the real `web/index.html`, read fresh on every load, so edits show after a browser reload. It implements the whole API:

- **A simulated 20 L / 2.5 kW kettle with a lagging sensor.** The heater runs at full power far from the setpoint, on PI control near it, and at boil power in the boil. While cooling, a chiller pulls the temperature down.
- **The automatic brew:** water check, prime, delayed start, mash in with add malt, rests, mash out, iodine test (if enabled), remove malt, boil with addition alarms, whirlpool and cooling. Manual mode works too, and autotune is faked (the gains are random).
- **BeerXML and JSON import** with the same mapping as the firmware, plus recipe editing and settings with validation.
- **Persistence:** settings and recipes are kept in `tools/mock/data.json`. Delete the file to start over.

**Also simulated:** the rolling history, brew logs (kept in `data.json`), and a sparge water heater on OUT2 with its own water.

**Not real in the mock:** Wi‑Fi (always "connected"), firmware upload (accepted and ignored), faults, rest cycles, power-loss resume and the touch screen. It is a simplified model for the web UI and the workflow; the firmware's own logic only runs on the ESP32.

**Screenshots of dialogs:** `http://localhost:8080/?run=strikeCalc#/recipes/<id>` makes the mock call that page function 1.5 s after loading (for example `strikeCalc` or `startDelayed`), so a headless browser can capture a dialog: `chrome --headless=new --screenshot=out.png --virtual-time-budget=6000 <url>`.
