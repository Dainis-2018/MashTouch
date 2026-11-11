# Storage

Settings, recipes and the resume point are JSON files on the LittleFS partition (3.4 MB, `default_16MB.csv`). The code is in `src/storage/`: `files` handles mounting and atomic writes, `schema` the JSON form and paths, and `storage` the API.

| File | Contents | Written |
|---|---|---|
| `/settings.json` | everything in `settings::Settings`: PID, pump, process, fan, safety, simulator, output roles, sensor roles, active recipe | on first boot, then on every change |
| `/recipes/<id>.json` | one recipe; the id is the name in lower case with `-` | when a recipe is created, imported or edited |
| `/resume.json` | brew position (`brew::Snapshot`) plus a copy of the running recipe | during an automatic brew: at every phase or step change and once a minute; deleted when the brew ends or is stopped |

`/logs/NNNN.jsonl` holds one **brew log** per automatic brew, one JSON object per line, appended as the brew goes on:

```
{"v":1,"id":7,"recipe":"Pale Ale","start":1794300000,"sim":false}   header (start: Unix time, 0 = clock not set)
{"t":30,"T":45.2,"sp":66.0,"h":100,"p":1}      sample every 30 s: temperature, setpoint, heater %, pump
{"t":912,"e":"Add malt, then confirm"}         event
{"t":11520,"end":"finished"}                   last line: finished or stopped
```

`t` is seconds since the brew started. A log without an end line was cut off by a power loss; resuming the brew continues the same log. The newest 30 logs are kept, and older ones are deleted when a new brew starts or when less than 256 kB is free.

## Rules

- **Atomic writes:** each file is written to `<name>.tmp` first, then renamed over the old file. A power cut leaves either the old file or the new one, never half of one.
- **Reading is forgiving:** a missing key, or a value with the wrong type or out of range, keeps the default. Older files therefore load after new settings are added, and new settings get their defaults. Every file carries a `version` for future migrations.
- **A broken `settings.json`** is renamed to `/settings.bad.json` and the controller starts on defaults.
- **First boot:** the default recipe is saved as `single-infusion` and becomes active.
- **No filesystem:** if LittleFS cannot be mounted even after formatting, everything runs from defaults in RAM and saving reports an error.
- **Changing settings:** the console's `set <path> <value>` changes one value and saves it. A value that would not survive the round trip is refused, so a typo does not silently do nothing. This covers values out of range, unknown names and values that would be clamped.
- **When changes apply:**
  - Changed settings apply from the next start of a mode.
  - Output roles, the sensor role of an already-running sensor and the simulator settings apply at once.
  - A running brew keeps its own copy of the recipe.
- **Sensors** are stored by ROM address (`"rom": "28FF..."`), so they keep their role and calibration when the bus order changes. A configured sensor that is missing is reported as a fault if its role is kettle, RIMS or heatsink.

## Power-loss resume

The controller offers the saved brew at boot: on the console, and with the Start LED blinking. A short press on Start or `resume` continues it, and `discard` forgets it. Starting any other mode also discards it.

The brew continues in the saved phase and step with the time already spent. The step temperature must be reached again before the timer runs on. Additions that already alarmed do not alarm again. No resume point is written in fake-heating mode.

## Examples

```
get heater
set heater.kp 25
set outputs.1.role heater          OUT2 becomes a second heater
set process.whirlpool hot
set safety.level closed            heater only with the level switch closed
sensor 0 role kettle
sensor 1 role heatsink
sensor 0 offset -0.3

recipe new Pale Ale                -> id "pale-ale", active
rset mashIn.tempC 67
rset rests.0.minutes 45
rset rests [{"name":"Beta","tempC":63,"minutes":40},{"name":"Alpha","tempC":72,"minutes":20}]
rset mashOut.tempC 78
rset additions.1.amount 30
recipes
recipe use single-infusion
```

A recipe file (version 3):

```json
{
  "version": 3,
  "name": "Pale Ale",
  "mash": true,
  "mashIn":  {"name": "Mash in", "tempC": 67, "minutes": 0, "pump": "on"},
  "rests": [
    {"name": "Saccharification", "tempC": 66, "minutes": 60, "pump": "cycle"}
  ],
  "mashOut": {"name": "Mash out", "tempC": 76, "minutes": 10, "pump": "on"},
  "boilMinutes": 60,
  "additions": [
    {"name": "Bittering hops", "atMinutes": 60, "amount": 25, "unit": "g"}
  ]
}
```

- **Fixed frame, free middle:** the mash always runs mash in → *add malt* → rests → mash out → *remove malt*.
- **Mash in** and **mash out** always exist when `mash` is true. **Rests** can be any number, including none.
- **Times:** each step heats to `tempC`, then rests for `minutes` (0 = no rest: go on as soon as the temperature is reached).
- **No mash-out heating:** set the mash out to the last rest's temperature with 0 minutes, and the brew goes straight to *remove malt*.
- **Pump:** `pump` is `off`, `on` or `cycle` (rest cycles). The pump is switched on/off only; older files with a `speedPct` field still load (it is ignored).
- **Sparge:** `spargeTempC` is the sparge water temperature, 0 for no sparge. It is shown when the bag comes out; with a sparge heater it is heated from the start of the brew until the boil.
- **Iodine test:** a setting, not part of the recipe (off by default). When it is on, the test runs before the mash out.
- **No mash:** `mash: false` is a boil-only (extract) brew.
- **Older files** convert when they are loaded:
  - Version 1 (a separate `mashInTempC`) and version 2 (one `steps` list with `addMalt`/`mashOut` flags) become version 3.
  - The first step becomes the mash in, and the step flagged as mash out becomes the mash out; the others become rests.
  - Without a flagged mash out, a last step at 75 °C or more becomes the mash out. Otherwise a mash out at the last temperature with 0 minutes is added.

## Import

The console command `import` accepts a pasted **BeerXML 1.0** file (the export of Brewfather, BeerSmith, Brewer's Friend and others) or MashTouch recipe JSON (one object or an array). The paste may span many lines, up to 256 kB. It ends by itself at `</RECIPES>`, `</RECIPE>` or the closing bracket; `cancel` stops it.

- **Each import adds new recipes.** A name already in use gets `-2`, `-3`… The active recipe does not change; use `recipe use <id>`.
- **BeerXML is converted once into the JSON form.** The controller only ever stores JSON. The converter is in `src/storage/beerxml.*`.

| BeerXML | MashTouch |
|---|---|
| `RECIPE/NAME`, `BOIL_TIME` | name, boil time |
| `MASH_STEP`, in order | the first is the mash in; the last one named "…out" (or a last step at 75 °C or more) is the mash out; the others are rests. Times from `STEP_TIME` (0 = no rest). Without a mash out, one is added at the last temperature with 0 minutes; without steps, no mash. The pump is on. |
| `HOP` used for *Boil* | addition at `TIME`; `AMOUNT` is in kg and becomes g |
| `HOP` used for *First Wort* / *Aroma* | addition at the start of the boil / at flameout |
| `MISC` used for *Boil* | addition at `TIME`, with the amount and unit from `DISPLAY_AMOUNT` |
| dry hops, mash hops, other miscs, fermentables, yeast, water, style, fermentation | not used on brew day; skipped items are listed after the import |

- **Encoding:** the file's encoding is read from its prolog (ISO-8859-1/Windows-1252 or UTF-8). Accented letters are folded to their base letter (ā → a, š → s, ß → ss), because the screen fonts only contain ASCII and °.
- **Self-test:** `import selftest` runs the parser on built-in samples and prints PASS/FAIL for each check.
- **Later:** the web UI (step 6) will upload files through the same `storage::importRecipes()`.
