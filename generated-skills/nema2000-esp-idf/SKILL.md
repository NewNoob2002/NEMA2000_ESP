---
name: nema2000-esp-idf
description: Maintain the NEMA2000_ESP ESP-IDF firmware project. Use when Codex needs to modify, debug, build, explain, or review this ESP32 NMEA0183-to-NMEA2000 gateway, including app_main startup, HAL modules, Bluetooth, WiFi, I2C, GNSS, filesystem code, NMEA parser logic, NMEA2000 PGN generation, ESP-IDF CMake components, sdkconfig, and idf.py build workflows.
---

# NEMA2000 ESP-IDF Firmware

## Workflow

1. Inspect the relevant files before editing. Start with `references/project-map.md` when orientation is needed.
2. Keep changes inside the existing architecture. Startup belongs in `main/main.cpp`, hardware setup in `main/HAL`, protocol conversion in `main/nmea0183_to_n2k.*`, and reusable ESP-IDF modules under `components/`.
3. Check compile guards before assuming code is active. Read `references/compile-flags.md` when touching optional Bluetooth, WiFi, I2C, NTP, web server, or GNSS code.
4. Treat vendored components as third-party unless the task clearly targets them. Avoid broad refactors in `components/NMEA2000`, `components/NMEA2000_ESP32`, `components/Parser`, and `components/arduino-esp32`.
5. Validate firmware changes with an ESP-IDF build. Read `references/build-and-verify.md`; use `scripts/build.ps1` when a local Windows ESP-IDF environment is available.

## Task Routing

- For architecture or file ownership questions, read `references/project-map.md`.
- For build, CMake, dependency, sdkconfig, or target changes, read `references/build-and-verify.md`.
- For NMEA0183 parsing, GNSS state, NMEA2000 PGNs, or gateway behavior, read `references/nmea0183-nmea2000.md`.
- For HAL, task, peripheral, or boot-order changes, read `references/hal-and-hardware.md`.
- For recurring build/runtime failures, read `references/common-failures.md`.

## Project Rules

- Preserve the thin `app_main()` pattern. Prefer calling into HAL or a focused module instead of adding long logic directly to startup.
- Prefer existing logging and support helpers over adding a new diagnostics style.
- Keep C++ compatible with the project's current ESP-IDF/Arduino-ESP32 hybrid environment.
- Avoid changing `sdkconfig` casually. If a config change is needed, explain the behavioral reason and verify the build.
- Do not rewrite generated, vendored, or upstream library code to fix an application-layer problem.
- When modifying protocol output, verify both parser acceptance and NMEA2000 message construction paths.

## Resources

- `references/project-map.md`: repository layout and ownership boundaries.
- `references/build-and-verify.md`: ESP-IDF setup, build commands, and validation rules.
- `references/compile-flags.md`: local feature flags and compile-guard expectations.
- `references/nmea0183-nmea2000.md`: gateway parser and PGN behavior.
- `references/hal-and-hardware.md`: HAL startup order and hardware responsibilities.
- `references/common-failures.md`: likely failure modes and first checks.
- `scripts/build.ps1`: runs the expected Windows ESP-IDF build command for this project.
