# Notice

MashTouch is free software, licensed under the **GNU General Public License, version 3 or (at your option) any later version**. The full text is in [LICENSE](LICENSE).

## Origin

MashTouch's brewing logic is derived from **Open ArdBir** (https://github.com/ArdBir/Open-ArdBir, version 2.8.3-RC, commit `ad2f4c2`):

- brauduino semi-automated single vessel RIMS: Copyright (C) 2012 Stephen Mathison
- modified by Mike Wilson
- modified by Massimo Nevi (2014/2015) and the Open ArdBir contributors

Open ArdBir's source header grants the GNU GPL version 3 or later. Its repository also ships a GPL‑2.0 license file. MashTouch follows the grant in the source header. The original sources are kept unchanged in [reference/open-ardbir/](reference/open-ardbir/) for reference and attribution; they are not compiled into the firmware.

MashTouch has been rewritten for the ESP32‑S3 as non-blocking code with a new touch UI, web interface and hardware layer. Any file that adapts Open ArdBir logic says so in its header.

## Third-party libraries

Downloaded by PlatformIO at build time; each keeps its own license:

| Library | License |
|---|---|
| LVGL | MIT |
| LovyanGFX | FreeBSD (BSD-2-Clause) |
| OneWire (Paul Stoffregen) | MIT-style |
| DallasTemperature | LGPL-2.1 or later |
| ArduinoJson | MIT |
| Arduino core for ESP32 / ESP-IDF | LGPL-2.1 / Apache-2.0 |
