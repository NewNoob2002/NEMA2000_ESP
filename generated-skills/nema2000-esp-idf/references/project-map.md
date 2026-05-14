# Project Map

Use this file to orient before changing the NEMA2000_ESP firmware.

## Top Level

- `CMakeLists.txt`: ESP-IDF project root. Includes `$ENV{IDF_PATH}/tools/cmake/project.cmake` and declares `project(bt_spp_acceptor)`.
- `dependencies.lock`: component registry lockfile. Current locked IDF version is `5.5.4`; target is `esp32`.
- `sdkconfig`, `sdkconfig.defaults`, `sdkconfig.ci.test`: ESP-IDF configuration inputs and generated config.
- `main/`: application code and HAL.
- `components/`: local and vendored ESP-IDF components.

## Application Entry

- `main/main.cpp`: firmware entry point. `app_main()` currently delegates to `HAL::HAL_Init()`. Keep this file thin unless the task is specifically about application startup.
- `main/CMakeLists.txt`: registers all files under `main/` and `main/HAL/` using `file(GLOB_RECURSE sources ./*.* HAL/*.*)`.

## Main Application Modules

- `main/HAL/*`: hardware abstraction layer. Owns power, I2C, filesystem, GNSS, Bluetooth, and periodic HAL update task setup.
- `main/CompileConfig.h`: project-local feature switches such as `COMPILE_BT`, `COMPILE_WIFI`, `COMPILE_NTP`, and `COMPILE_I2C`.
- `main/nmea0183_to_n2k.*`: NMEA0183 GNSS parser state and NMEA2000 message generation.
- `main/Bluetooth*`: Bluetooth Classic/BLE serial selection and lifecycle code.
- `main/myWIFI.*`: WiFi station/AP behavior guarded by compile flags.
- `main/myWebServer.*`: AP configuration web server and static resource serving.
- `main/GNSS.*`: GNSS configuration and serial handling.
- `main/BQ40Z50.*`, `main/MP2762A.*`: I2C device support.
- `main/mcu_settings.*`, `main/mcu_typedef.h`: persistent settings, defaults, and shared types.
- `main/Support.*`, `main/System.cpp`, `main/States.*`: support helpers, system behavior, and state handling.
- `main/AP-Config/`: web configuration UI assets.

## Components

- `components/NMEA2000`: vendored NMEA2000 library, including messages, PGNs, tests, and examples.
- `components/NMEA2000_ESP32`: ESP32 adapter for the NMEA2000 library.
- `components/Parser`: SparkFun extensible message parser used by NMEA parsing.
- `components/Unicore_GNSS_Library`: Unicore GNSS support.
- `components/DataCenter`: account/data-center support and ring buffer.
- `components/ESP32_BleSerial`: BLE serial component.
- `components/arduino-esp32`: large vendored Arduino-ESP32 tree. Treat as third-party by default.

## Ownership Guidance

- Put board/peripheral initialization in `main/HAL`.
- Put parser and PGN behavior in `main/nmea0183_to_n2k.*`.
- Put settings defaults in `main/mcu_typedef.h` or `main/mcu_settings.*` only when the task requires persisted configuration.
- Put application-specific code in `main/`, not in vendored components, unless extending an existing component interface is unavoidable.
