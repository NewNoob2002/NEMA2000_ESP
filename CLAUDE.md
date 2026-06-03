# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

This is an ESP-IDF v5.5.4 project (CMake-based) targeting ESP32 with Arduino-ESP32 components. The project name is `s20_app`.

```bash
# Standard ESP-IDF build workflow
idf.py build             # Build the project
idf.py flash             # Build and flash to ESP32
idf.py monitor           # Serial monitor (115200 baud)
idf.py build flash monitor  # All-in-one

# Build with specific serial port
idf.py -p /dev/ttyUSB0 flash monitor
```

There are no linting or unit test commands. The `build/` directory is gitignored. No PlatformIO — this uses raw ESP-IDF with CMake.

## Architecture

This is the **RTK S20** firmware — an ESP32-based GNSS RTK rover/base station that bridges NMEA0183 GNSS data to NMEA2000 CAN (marine networks), originally derived from SparkFun RTK firmware. The core hardware is a Unicore UM980 GNSS receiver, BQ40Z50 battery fuel gauge, and MP2762A charger IC.

### Layered structure

```
main.cpp (app_main)
  └─► HAL::HAL_Init()       — one-time hardware init (power, I2C, filesystem, GNSS, WiFi, web server)
  └─► App_Init()            — initializes the DataProc publish-subscribe pipeline
  └─► loop: HAL::HAL_Update() — 10ms tick driving the state machine, GNSS config, WiFi, and web server
```

### HAL (Hardware Abstraction Layer) — `main/HAL/`

The central orchestrator. `HAL_Init()` sequences boot in order: Power → I2C → FileSystem → GNSS → StateInit → WiFi (AP or Station) → WebServer. `HAL_Update()` calls `stateUpdate()`, `gnssUpdate()`, `wifiUpdate()`, and `webServerUpdate()` every 10ms.

Pin definitions live in `main/HAL/HAL_Config.h`:
- Power enable: GPIO 26
- I2C: SDA 21, SCL 22
- GNSS UART: TX 10, RX 9
- Status LEDs: GPIOs 27, 14, 13, 15, 2

### State Machine — `main/States.cpp`

The core system mode controller. Modes include Rover (not started → config wait → no fix → fix → RTK float → RTK fix) and Base (temp settle → survey started → transmitting, or fixed base). WebConfig mode runs a captive-portal WiFi AP + web server. States are driven by GNSS fix quality from the UM980. External modules request state changes via `requestChangeState()`. Mode is tracked with a bitfield `RTK_MODE_t`.

### Data Processing Pipeline — `main/App/DataProc/`

Uses the **DataCenter/Account** publish-subscribe pattern (from `components/DataCenter/`). Accounts are named data nodes connected via a central DataCenter. `DP_LIST.inc` defines the node inventory (currently: `GNSS_NMEA`, `Bluetooth`). Each node has an `_Init()` function that receives an Account pointer. Nodes can Publish, Subscribe, Notify, and Pull data. This is the primary mechanism for routing GNSS data between subsystems.

### Web Server — `main/myWebServer.cpp`

Serves an SPA configuration interface over HTTP on port 80. Web resources (HTML, JS, CSS, images) are gzip-compressed and embedded in `main/form.h` as PROGMEM byte arrays. The source files live in `main/AP-Config/` — when you modify the web UI, the gzip'd form.h must be regenerated. The server uses WebSocket (`/ws`) for real-time status/settings updates and supports OTA firmware upload at `/uploadFirmware`. State machine: OFF → WAIT_FOR_NETWORK → NETWORK_CONNECTED → RUNNING.

### WiFi — `main/myWIFI.cpp`

The `RTK_WIFI` class manages both SoftAP and Station modes. On boot, if `wifiConfigOverAP` is true, it starts as an access point for web configuration. Otherwise, it connects to stored WiFi networks (up to 4, stored in `settings.wifiNetworks[]`). Captive portal support is included.

### GNSS Driver — `main/Unicore_UM980.cpp` + `components/Unicore_GNSS_Library/`

Wraps the Unicore UM980 GNSS receiver over UART. Exposes fix status, coordinates, survey-in state, RTK mode detection, satellite info, and configuration commands. `GNSS.cpp` provides higher-level configure/update orchestration with a pending-configuration bitfield (`gnssConfigureRequest`).

### NMEA0183 → NMEA2000 Bridge — `main/nmea0183_to_n2k.cpp`

Parses NMEA0183 sentences (RMC, GGA) via the SparkFun Extensible Message Parser (`components/Parser/`), extracts position/course/speed, and builds NMEA2000 messages (PGN 129025 LatLonRapid, 129026 CogSogRapid, 129029 GNSS Position). Currently commented out in `main.cpp` in favor of the DataProc pipeline.

### Components (`components/`)

| Component | Purpose |
|-----------|---------|
| `NMEA2000/` | Timo Lappalainen's NMEA2000 library (CAN message definitions, group functions) |
| `NMEA2000_ESP32/` | ESP32 TWAI CAN driver implementing `tNMEA2000` interface |
| `DataCenter/` | Publish-subscribe data router (Account nodes + central DataCenter) |
| `Unicore_GNSS_Library/` | Low-level Unicore binary protocol parser |
| `ESP32_BleSerial/` | BLE SPP serial bridge with battery service |
| `Parser/` | SparkFun Extensible Message Parser (NMEA0183, RTCM, SBF, SPARTN CRC) |

### Settings — `main/mcu_typedef.h`

The `settings_t` struct (~80 fields) holds the entire device configuration: GNSS parameters, WiFi networks, NTRIP client/server, antenna offsets, logging, PointPerfect PPP, battery, Bluetooth, and hardware tuning. Settings are persisted to NVM via LittleFS. Fields initialize with non-zero defaults for display in the web config; `254`/`0xFF` are used as sentinel values to trigger platform-specific defaults.

### Compile-Time Feature Flags — `main/CompileConfig.h`

Controls which subsystems are compiled in: `COMPILE_WEBSERVER`, `COMPILE_WIFI`, `COMPILE_BT`, `COMPILE_I2C`, `COMPILE_NTP`. WiFi depends on WEBSERVER being enabled.

## Key Patterns

- **Global externs** in `mcu_settings.h`: `settings`, `online_devices`, `task`, `productType` are extern globals used across modules. `online_devices` is a struct of bools tracking which hardware is alive.
- **`systemPrintf`/`systemPrintln`** (from `Support.h`): Use these instead of `Serial.print` — they route to USB Serial and optionally web server WebSocket.
- **`reportHeap()`** is called in the update loop to monitor memory pressure.
- **Boot time profiling**: The `DMW_b("label")` macro records millisecond timestamps for init steps, displayed as a sorted delta-time table at boot complete.
- **Settings sentinel pattern**: Array fields with first element `254` (or `0.0` for floats) trigger platform-specific default population on first boot.
