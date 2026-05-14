# HAL And Hardware

Use this file when changing startup, tasks, peripherals, or board behavior.

## HAL Entry

- `main/main.cpp` calls `HAL::HAL_Init()`.
- `main/HAL/HAL.h` declares the public HAL surface.
- `main/HAL/HAL.cpp` implements boot sequencing and starts the periodic HAL task.

## Startup Order

Current `HAL_Init()` sequence:

1. Start `Serial` at `115200`.
2. Initialize and check power.
3. Initialize and scan I2C only when `COMPILE_I2C` is enabled.
4. Initialize filesystem.
5. Initialize and configure GNSS.
6. Initialize Bluetooth.
7. Optionally update WiFi and start web server, currently commented.
8. Print boot timing.
9. Start `HAL_Update` task pinned to core 1.

Preserve this order unless changing a dependency. For example, I2C devices depend on switched peripherals being powered first.

## Periodic Task

`HAL_Update()` currently:

- Reports heap.
- Runs `bluetoothUpdate()`.
- Leaves `webServerUpdate()` commented.
- Delays with `vTaskDelayUntil`.

When adding periodic work:

- Keep stack and timing in mind.
- Avoid blocking calls inside `HAL_Update()`.
- Prefer a dedicated task for long-running I/O or network operations.

## Module Guidance

- Power: keep shutdown and power checks in `HAL_Power.cpp`.
- I2C: keep bus initialization and scanning in `HAL_I2C*`; use `I2C_GetBus()` for device drivers.
- Filesystem: keep mount/init logic in `HAL_FileSystem.cpp`.
- GNSS: keep receiver power/configuration and serial setup in `HAL_GNSS.cpp` and `main/GNSS.*`.
- Bluetooth: keep radio startup and update behavior in `Bluetooth.*` and `HAL_Bluetooth.cpp`.

## Hardware Safety

- Do not assume a feature is physically present just because the code exists.
- Check GPIO, UART, I2C, and power sequencing before enabling hardware paths.
- When changing pin or peripheral configuration, search for every reference to the same setting or GPIO.
