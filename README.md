# BILLY_AMBIENT

Ambient lighting + sensors project for FireBeetle ESP32 (ESP-WROOM-32).

![Circuit diagram](data/circuit_image.svg)

## What it is

This project integrates multiple PIR motion sensors, an LDR light sensor, and a WS2812B LED strip. The ESP32 reads sensors and drives the LEDs. A small web UI provides status and OTA firmware update.

## Web UI

- `/` – device overview
- `/pir312` – sensors status
- `/ota` – firmware update (OTA)

## Hardware

### Components (from `data/project_doc.md`)

- FireBeetle ESP32
- HC-SR505 Mini PIR Motion Sensing Module (multiple)
- SN74AHCT125N (signal buffer / level shifting for WS2812B data)
- WS2812B LED strip
- KY-018 LDR photo resistor
- 5V PSU
- Resistor (325 Ω)

### Wiring (high level)

- ESP32
  - `D7/IO13` -> `SN74AHCT125N 1A`
  - `A2/IO34` -> LDR signal
  - Multiple GPIOs -> PIR `out` (see the full list in `data/project_doc.md`)
- SN74AHCT125N
  - `Vcc` -> 5V
  - `GND` -> GND
  - `1Y` -> series resistor -> WS2812B `Din`
- WS2812B
  - `+5V` -> 5V
  - `GND` -> GND
  - `Din` -> from buffer through resistor

Full wiring details: see `data/project_doc.md`.

## Build / Flash

This repository is set up for PlatformIO.

- Build: `platformio run`
- Upload (USB): `platformio run -t upload`

Notes:
- Web assets are embedded into the firmware binary (CMake `EMBED_TXTFILES`), so there is no separate filesystem image to upload.

## License

MIT – see [LICENSE](LICENSE).
