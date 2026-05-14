# Common Failures

Use this file for first-pass debugging.

## Build Fails After Adding A File

- If the file is under `main/` or `main/HAL/`, `main/CMakeLists.txt` globbing should pick it up.
- If the file is under `components/`, update that component's `CMakeLists.txt`.
- Check include paths and component dependencies.

## Undefined Reference Or Missing Symbol

- Check whether the implementation is compiled under a feature guard.
- Check whether the caller is outside the same guard.
- Check whether the source file belongs to a registered component.

## Header Not Found

- If the header is in `main/`, confirm `INCLUDE_DIRS . HAL`.
- If the header is in a component, confirm that component exports the include directory.
- Avoid adding broad include paths that expose vendored internals accidentally.

## Bluetooth Or WiFi Compile Errors

- Inspect `main/CompileConfig.h` first.
- Bluetooth is currently enabled by default; WiFi is present but not enabled there.
- Arduino-ESP32 APIs may depend on sdkconfig options and component availability.

## Runtime Hangs Or Slow Boot

- Review `HAL_Init()` ordering and boot timing output.
- Check for blocking calls added before task startup.
- Check power and I2C scan behavior if hardware is missing.

## Parser Accepts No Messages

- Confirm bytes are actually fed to `tGatewayNmea0183Parser::FeedByte()` or `FeedBytes()`.
- Confirm `Begin()` succeeded.
- Check checksum failures and `LastError`.
- Verify sentence type and field layout.

## NMEA2000 Messages Not Sent

- Confirm parser generated pending messages.
- Confirm `ConfigureGatewayNmea2000()` and CAN open path are active.
- Check `SendGatewayMessage()` return value and failure count.
- Inspect NMEA2000 source and mode setup.
