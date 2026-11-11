# MashTouch

ESP32‑S3 touchscreen controller for single-vessel (RIMS/BIAB) brewing systems, based on [Open ArdBir](https://github.com/ArdBir/Open-ArdBir).

**Status:** hardware bring-up. The firmware currently shows a test screen for checking the display, touch, buttons, outputs and sensors. The brewing logic is being ported next.

## Hardware

| Part | |
|---|---|
| MCU | ESP32‑S3‑WROOM‑1 N16R8 (16 MB flash, 8 MB PSRAM) |
| Display | MSP3526: 3.5" IPS 480×320, ST7796S (SPI) + FT6336U capacitive touch |
| Front panel | 2 small buttons with LED rings: Start/Pause and Pump |
| Outputs | 4 signal outputs for SSRs, 2 × 12 V PWM power outputs (pump, fan), onboard buzzer |
| Sensors | DS18B20 (any number on one 1‑Wire bus), PT100/PT1000 via MAX31865 (planned), level switch |
| Power | 12 V in |

Output and sensor roles (heater, pump, fan, kettle sensor...) are assigned in settings, so no part of the system is limited to one heater, pump or sensor.

The controller PCB is a "backpack" with the same outline and mounting holes as the MSP3526, and fits into an existing controller box next to the SSRs and power supply.

## Building

PlatformIO Core is enough; the PlatformIO IDE extension isn't needed.

```
pio run -e devkit              # build
pio run -e devkit -t upload    # flash over USB
pio device monitor             # serial log
```

In VS Code, use **Terminal → Run Task → PIO: Build / Upload**.

| Environment | Hardware |
|---|---|
| `devkit` | ESP32‑S3‑DevKitC‑1 N16R8 + MSP3526 on a breadboard ([wiring](docs/bringup-wiring.md)) |

## Documentation

- [Design](docs/design.md): architecture, pin map, PCB and roadmap
- [Breadboard wiring for bring-up](docs/bringup-wiring.md)
- [Open ArdBir analysis](docs/open-ardbir-analysis.md): what is kept from the original and how it maps to MashTouch

## License

GPL‑3.0‑or-later. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for origin and third-party licenses.
