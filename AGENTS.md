# Repository Guidelines

## Project Structure & Module Organization
This is an ESP-IDF v5.5.4 CMake firmware project named `s20_app` for ESP32-based RTK/GNSS functionality. Core application code lives in `main/`, with hardware abstraction in `main/HAL/`, app pipeline logic in `main/App/`, and data processing nodes in `main/App/DataProc/`. Reusable libraries are under `components/`, including `NMEA2000`, `NMEA2000_ESP32`, `DataCenter`, `Parser`, `ESP32_BleSerial`, and `Unicore_GNSS_Library`. Web configuration source assets are in `main/AP-Config/`; generated embedded web assets are stored in `main/form.h`. Build outputs belong in `build/` and should not be committed.

## Build, Test, and Development Commands
Use ESP-IDF commands from a shell with `IDF_PATH` configured:

```bash
idf.py build
idf.py flash
idf.py monitor
idf.py build flash monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

`idf.py build` compiles the firmware. `idf.py flash` programs the ESP32. `idf.py monitor` opens the serial monitor at the configured baud rate. After editing files in `main/AP-Config/`, regenerate `main/form.h` with:

```bash
python3 scripts/generate_form_header.py
```

## Coding Style & Naming Conventions
Format C/C++ code with the repository `.clang-format`: 4-space indentation, no tabs, 120-column limit, LLVM-derived style, attached braces, sorted includes, and mandatory braces for control statements. Prefer existing naming patterns: `HAL_*` for hardware-layer functions, `DP_*` for data processing nodes, and descriptive class/module names such as `RTK_WIFI` or `Unicore_UM980`. Use `systemPrintf`/`systemPrintln` for runtime logging instead of direct `Serial.print` calls.

## Testing Guidelines
There is currently no dedicated unit test framework or lint command in this repository. Validate changes with `idf.py build` at minimum. For firmware behavior, flash a target board and verify boot logs, GNSS state transitions, WiFi/web server behavior, BLE, and NMEA2000/CAN paths relevant to the change. For web UI changes, confirm the regenerated `main/form.h` is included and the captive portal loads correctly.

## Commit & Pull Request Guidelines
Recent history uses concise imperative commits, sometimes with Conventional Commit style, for example `update index style`, `Refactor code structure for improved readability and maintainability`, and `feat(webServer): Enhance settings management and WebSocket communication`. Prefer short imperative subjects; use `feat(scope):`, `fix(scope):`, or `refactor(scope):` when a scope is clear. Pull requests should describe the firmware behavior changed, list validation performed, identify target hardware/ESP-IDF version, and include screenshots or logs for web UI and serial-observable changes.

## Agent-Specific Instructions
Follow `/home/gtc/.codex/RTK.md`: prefix shell commands with `rtk` when working in this repository. Do not revert unrelated user changes, and avoid committing generated build artifacts from `build/` or managed dependency output unless explicitly requested.
