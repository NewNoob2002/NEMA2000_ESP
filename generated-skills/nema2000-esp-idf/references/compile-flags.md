# Compile Flags

Local feature flags live in `main/CompileConfig.h`.

## Current Observed Defaults

- `COMPILE_BT`: enabled.
- `COMPILE_WIFI`: present but commented out.
- `COMPILE_NTP`: present but commented out.
- `COMPILE_I2C`: present but commented out.

Other modules also use guards such as `COMPILE_WEBSERVER`, `COMPILE_MENU_WIFI`, and GNSS-specific feature flags.

## Rules

- Check whether a symbol is compiled before editing call sites.
- When enabling a feature, inspect both initialization and update paths. Example: enabling WiFi may require startup calls, web server behavior, settings defaults, and sdkconfig support.
- When disabling a feature, ensure unguarded callers do not still reference guarded functions or globals.
- Keep feature flags centralized. Do not create scattered one-off defines unless there is a clear build-variant reason.

## Common Checks

- Bluetooth: inspect `main/Bluetooth.h`, `main/Bluetooth.cpp`, and `main/BluetoothSelect.h`.
- WiFi: inspect `main/myWIFI.h`, `main/myWIFI.cpp`, `main/myWebServer.*`, and settings fields in `main/mcu_typedef.h`.
- I2C: inspect `main/HAL/HAL_I2C*`, `main/BQ40Z50.*`, and `main/MP2762A.*`.
- GNSS: inspect `main/GNSS.*`, `main/HAL/HAL_GNSS.cpp`, and GNSS-specific component headers.
