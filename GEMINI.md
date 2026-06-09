# NEMA2000_ESP (RTK S20) Project Instructions

This project is the **RTK S20** firmware — an ESP32-based GNSS RTK rover/base station that bridges NMEA0183 GNSS data to NMEA2000 CAN (marine networks). It is built using the ESP-IDF framework (v5.5.4) with the Arduino-ESP32 component.

## Project Overview

- **Purpose:** NMEA0183 GNSS to NMEA2000 gateway and RTK rover/base station.
- **Hardware:** ESP32, Unicore UM980 GNSS receiver, BQ40Z50 battery fuel gauge, MP2762A charger, Bluetooth, WiFi, I2C.
- **Core Technologies:** ESP-IDF v5.5.4, Arduino-ESP32, NMEA2000 Library.
- **Architecture:**
    - **HAL (Hardware Abstraction Layer):** Located in `main/HAL/`. Abstracts hardware interactions.
    - **DataCenter:** Located in `components/DataCenter/`. A pub-sub system for inter-module communication.
    - **App/DataProc:** Located in `main/App/DataProc/`. Implements data processing "nodes" (GNSS_NMEA, NMEA2000, Bluetooth).
    - **Web Server:** Source assets in `main/AP-Config/`; generated embedded assets in `main/form.h`.

## Building and Running

### Standard ESP-IDF Workflow
Use the standard ESP-IDF commands (requires `IDF_PATH` set to ESP-IDF v5.5.4):
- **Build:** `idf.py build`
- **Flash:** `idf.py flash`
- **Monitor:** `idf.py monitor` (115200 baud)
- **All-in-one:** `idf.py build flash monitor`

### Interactive Script
An interactive wrapper is available:
```bash
./scripts/idf_build_flash_monitor.sh
```

### Web Asset Generation
If you modify files in `main/AP-Config/`, regenerate `main/form.h`:
```bash
python3 scripts/generate_form_header.py
```

## Development Conventions

- **Code Style:** Use `.clang-format`. 4-space indentation, no tabs, 120-column limit.
- **Naming:**
    - `HAL_*` for hardware layer functions.
    - `DP_*` for data processing nodes.
- **Logging:** Prefer `systemPrintf`/`systemPrintln` or `ESP_LOGX` macros.
- **HAL/App Separation:** Keep hardware-specific code in `HAL` and business logic in `App`/`DataProc`.
- **DataCenter:** New modules should be implemented as `DataProc` nodes using the `Account` system.

## Key Directories

- `main/`: Application source code.
- `main/HAL/`: Hardware abstraction layer.
- `main/App/DataProc/`: Data processing nodes.
- `main/AP-Config/`: Web UI source assets.
- `components/`: Internal and external libraries.
- `scripts/`: Build and utility scripts.
