# MeshtasticCoordinator (ESP32-S3)

Coordinates two Meshtastic devices over UART. One UART carries commands (Diagnostics),
the other carries sensor telemetry and probe-initiated messages (Sensors).

## Hardware

| UART | Role | RX | TX |
|------|------|----|----|
| Diagnostics | Command Center | 5 | 6 |
| Sensors     | Meshtastic     | 2 | 3 |

All devices must share **common ground**.

## Features
- Commands for areas, thresholds, overrides, and baseline mode
- Live rolling (last 10) readings per probe
- Per-area live min/max per metric; baseline fallback with smooth takeover
- Persistent config on LittleFS (`/config.json`), created on first boot
- Case-insensitive inputs; stored uppercase in memory and JSON
- `SET PROBE` (sensor UART), `SET PROBES` (either UART), and `REMOVE PROBE`

## Commands

```text
GET AREAS
  → AREA: FLOOR11 LOBBY 6620

GET STATS:
  → STAT: FLOOR11 CO2  min:300 max:1500 min_o:-1 max_o:-1 baseline:true
  → STAT: FLOOR11 TEMP min:20.1 max:26.8 min_o:-1 max_o:-1 baseline:true
  → STAT: FLOOR11 HUM  min:41.2 max:68.4 min_o:-1 max_o:-1 baseline:true
  → STAT: FLOOR11 DB   min:36.0 max:83.5 min_o:-1 max_o:-1 baseline:true

GET THRESHOLD:
  → THRESHOLD FLOOR11 CO2 400 600 800 1000 1200 1400

GET USE_BASELINE FLOOR11
  → USE_BASELINE FLOOR11 True

SET USE_BASELINE FLOOR11 True
  → USE_BASELINE FLOOR11 True ACCEPTED

SET OVERRIDE FLOOR11 MIN 500
  → OVERRIDE FLOOR11 MIN 500 ACCEPTED

SET THRESHOLD FLOOR11 CO2 3 800
  → THRESHOLD FLOOR11 CO2 3 800 ACCEPTED

SET PROBES 6620 FLOOR11 LOBBY       (either UART)
  → PROBES 6620 FLOOR11 LOBBY ACCEPTED

6620 SET PROBE FLOOR11 LOBBY         (sensor UART)
  → PROBE 6620 FLOOR11 LOBBY ACCEPTED

REMOVE PROBE 6620                    (either UART)
  → PROBE 6620 REMOVED
```

## Baselines

See `Baselines.h`. Areas start with safe-low base min/max (0–1) so live readings
quickly establish real min/max. When `useBaseline=true`, stats report baseline values
until the live max for a metric reaches `BASELINE_NEAR_FRACTION * baseline_max`, at
which point the live range is reported (still tagging `baseline:true`).

## Default Areas

Created on first boot (uppercase): `FLOOR11, FLOOR12, FLOOR15, FLOOR16, FLOOR17, POOL, TEAROOM`.
Probes and locations are empty until assigned.

## Building

### Arduino IDE
- Board: **ESP32-S3** (Arduino core)
- Install **ArduinoJson** v6 via Library Manager
- LittleFS comes with the ESP32 core

### PlatformIO (`platformio.ini` included)
```ini
[env:esp32-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
lib_deps = bblanchon/ArduinoJson @ ^6.21.3
monitor_speed = 115200
build_flags =
    -DCORE_DEBUG_LEVEL=2
```

## Notes
- All matching is case-insensitive; values stored uppercase in config and memory.
- Config is saved after each successful mutation.
- Sensor ring buffers are in RAM only (last 10 values).
