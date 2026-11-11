# MashTouch

ESP32‑S3 touchscreen controller for single-vessel (RIMS/BIAB) brewing systems, based on [Open ArdBir](https://github.com/ArdBir/Open-ArdBir).

**Status:** the firmware is feature-complete for a first brew and builds cleanly, but has not run on hardware yet. It includes the automatic mash/boil process with PID, manual mode, recipes with BeerXML import, a touch UI, a web UI with firmware update over Wi‑Fi, and brew logs. The web UI is tested on a PC mock. See [PROGRESS.md](PROGRESS.md).

## Hardware

| Part | |
|---|---|
| Display panel | [Waveshare ESP32‑S3‑Touch‑LCD‑3.5](https://docs.waveshare.com/ESP32-S3-Touch-LCD-3.5): ESP32‑S3R8 (8 MB PSRAM, 16 MB flash), 3.5" IPS 480×320, ST7796 (SPI) + FT6336 capacitive touch, ES8311 codec + speaker, front cover frame |
| I/O backpack | MashTouch PCB without an MCU, plugged into the panel's 2×16 header J8 (in design) |
| Front panel | 2 small buttons with LED rings: Start/Pause and Pump |
| Outputs | 4 signal outputs for SSRs and relays, 2 × 12 V PWM power outputs (pump, fan) |
| Sensors | DS18B20 (any number on one 1‑Wire bus), PT100/PT1000 via MAX31865 (planned), level switch |
| Power | 12 V in |

Output and sensor roles (heater, pump, fan, kettle sensor...) are assigned in settings, so no part of the system is limited to one heater, pump or sensor.

The panel and backpack replace the display part of an existing controller box. The box keeps its SSRs, power supply and mains wiring.

## Building

PlatformIO Core is enough; the PlatformIO IDE extension isn't needed.

```
pio run                        # build (environment ws35)
pio run -t upload              # flash over the panel's USB-C
pio device monitor             # serial console (type "help")
pio run -e ws35_ota -t upload  # flash over Wi-Fi (mashtouch.local)
```

In VS Code, use **Terminal → Run Task → PIO: Build / Upload**.

The web UI can be tried without hardware: `node tools/mock/server.js`, then open http://localhost:8080.

## Documentation

- [Design](docs/design.md): architecture, pin map, backpack PCB and roadmap
- [Bring-up wiring and tests](docs/bringup-wiring.md)
- [Touch UI](docs/ui.md), [storage and recipe format](docs/storage.md), [web API](docs/web-api.md)
- [Open ArdBir analysis](docs/open-ardbir-analysis.md): what is kept from the original and how it maps to MashTouch

## License

GPL‑3.0‑or-later. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for origin and third-party licenses.
