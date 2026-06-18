# NMEA2000 CAN Debugging and Functional Test Guide

This note summarizes how to debug the NMEA2000/CAN path in this project and how to use captured CAN evidence for later functional module testing.

Chinese version: `Document/NMEA2000_CAN_Debugging_Guide_CN.md`

## Summary for Test Team

This project exposes one NMEA2000 gateway node over ESP32 TWAI. The testing team should validate three layers:

1. Bus bring-up: 250 kbit/s classic CAN, 29-bit extended frames, working transceiver, correct termination, and successful NMEA2000 address claim.
2. Gateway output: after a valid fresh GNSS fix, the DUT periodically transmits PGN `129025`, `129026`, and `129029`.
3. Gateway control: a CAN debug tool can send ISO Request PGN `59904` and Group Function PGN `126208` to verify device identity, PGN list, and configurable transmit intervals.

The most important send messages are:

| Purpose | CAN ID | Data |
| --- | --- | --- |
| Request address claim | `0x18EA1650` | `00 EE 00 FF FF FF FF FF` |
| Request PGN list | `0x18EA1650` | `00 EE 01 FF FF FF FF FF` |
| Request product info | `0x18EA1650` | `14 F0 01 FF FF FF FF FF` |
| Set PGN `129025` to 500 ms, frame 1 | `0x0DED1650` | `00 0B 00 01 F8 01 F4 01` |
| Set PGN `129025` to 500 ms, frame 2 | `0x0DED1650` | `01 00 00 FF FF 00 FF FF` |
| Disable PGN `129025`, frame 1 | `0x0DED1650` | `00 0B 00 01 F8 01 00 00` |
| Disable PGN `129025`, frame 2 | `0x0DED1650` | `01 00 00 FF FF 00 FF FF` |
| Restore PGN `129025`, frame 1 | `0x0DED1650` | `00 0B 00 01 F8 01 FE FF` |
| Restore PGN `129025`, frame 2 | `0x0DED1650` | `01 FF FF FF FF 00 FF FF` |

These IDs assume DUT source address `0x16` and tester source address `0x50`. Always confirm the DUT source address from PGN `60928` before running directed tests.

## Project CAN Path

The firmware is an ESP-IDF NMEA0183 GNSS to NMEA2000 gateway. The active NMEA2000 processor is implemented in `main/App/DataProc/DP_NMEA2000.cpp`, using the ESP32 TWAI based adapter in `components/NMEA2000/src/NMEA2000_esp32.cpp`.

Runtime flow:

1. `DATA_PROC_INIT_DEF(NMEA2000)` configures the gateway with `ConfigureGatewayNmea2000()`.
2. `tNMEA2000_esp32::Open()` starts the ESP32 TWAI node.
3. A 10 ms timer calls `nmea2000.ParseMessages()`.
4. Valid UM980 GNSS data is converted by `BuildGatewayN2kMessages()`.
5. Due NMEA2000 messages are sent with `nmea2000.SendMsg()`.

Default bus configuration:

| Item | Value |
| --- | --- |
| Physical controller | ESP32 TWAI on-chip controller |
| CAN mode | Classic CAN 2.0, extended 29-bit identifiers |
| CAN-FD | Not used |
| Bitrate | 250 kbit/s |
| Sample point | 87.5 percent |
| TX pin | GPIO 22, unless `ESP32_CAN_TX_PIN` is overridden |
| RX pin | GPIO 21, unless `ESP32_CAN_RX_PIN` is overridden |
| NMEA2000 node mode | `N2km_ListenAndNode` |
| Preferred source address | 22, but address claim may change it |

The preferred source address is configured in `main/nmea0183_to_n2k.cpp`:

```cpp
nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
```

`22` decimal is `0x16` hexadecimal. This is only the preferred address; the actual runtime source address must be confirmed from the NMEA2000 address claim process.

## Expected Gateway PGNs

The gateway advertises and transmits these GNSS PGNs:

| PGN | Name | Source code | Priority | Default period | Default offset |
| --- | --- | --- | --- | --- | --- |
| 129025 | Position, Rapid Update | `SetN2kLatLonRapid()` | 2 | 1000 ms | 0 ms |
| 129026 | COG/SOG, Rapid Update | `SetN2kCOGSOGRapid()` | 2 | 1000 ms | 20 ms |
| 129029 | GNSS Position Data | `SetN2kGNSS()` | 3 | 1000 ms | 40 ms |

The firmware only sends these messages when GNSS data is complete enough for `ReadGatewayGnssData()` and `BuildGatewayN2kMessages()` to succeed. A stale fix older than 3000 ms is rejected.

The gateway also handles NMEA2000 group function requests for the three transmit PGNs. Supported interval request behavior:

| Request value | Expected behavior |
| --- | --- |
| `N2k_KEEP_TRANSMISSION_INTERVAL` | Keep the current interval |
| `N2k_RESTORE_TRANSMISSION_INTERVAL` | Restore default interval and offset |
| `0` | Disable that PGN |
| `100` to `60000` ms | Set a new transmit interval |
| Offset field other than `0xffff` | Apply offset in 10 ms units |

The firmware acknowledges directed group function requests. Broadcast requests are handled without sending an acknowledge frame.

## PGN, CAN ID, and Payload Conversion

NMEA2000 uses a 29-bit extended CAN identifier. The library conversion is implemented by `N2ktoCanID()` in `components/NMEA2000/src/NMEA2000.cpp`.

CAN ID fields:

| Bits | Field | Meaning |
| --- | --- | --- |
| 28..26 | Priority | Lower numeric value has higher CAN arbitration priority |
| 24 | Data Page | Part of the PGN |
| 23..16 | PF | PDU format |
| 15..8 | PS or destination | PDU2 uses PGN low byte; PDU1 uses destination |
| 7..0 | Source | Device source address after address claim |

For PDU2 PGNs, where `PF >= 240`, the PGN includes the low byte and destination is global:

```text
CAN ID = (priority << 26) | (PGN << 8) | source
```

Examples when DUT source address is `0x16`:

| PGN | Priority | CAN ID | Notes |
| --- | --- | --- | --- |
| `129025` / `0x1F801` | 2 | `0x09F80116` | Position, Rapid Update |
| `129026` / `0x1F802` | 2 | `0x09F80216` | COG/SOG, Rapid Update |
| `129029` / `0x1F805` | 3 | `0x0DF80516` | GNSS Position Data, fast-packet |

For PDU1 PGNs, where `PF < 240`, the PGN low byte must be `00` and the destination address is inserted into the ID:

```text
CAN ID = (priority << 26) | (PGN << 8) | (destination << 8) | source
```

Examples when tester source is `0x50` and DUT destination is `0x16`:

| PGN | Priority | CAN ID | Purpose |
| --- | --- | --- | --- |
| `59904` / `0xEA00` | 6 | `0x18EA1650` | ISO Request |
| `126208` / `0x1ED00` | 3 | `0x0DED1650` | NMEA Group Function |

Payload conversion is PGN-specific:

| PGN | Encoder | Payload layout |
| --- | --- | --- |
| `129025` | `SetN2kLatLonRapid()` | Latitude 4 bytes at `1e-7`, longitude 4 bytes at `1e-7` |
| `129026` | `SetN2kCOGSOGRapid()` | SID, COG reference, COG 2 bytes at `0.0001 rad`, SOG 2 bytes at `0.01 m/s`, reserved bytes |
| `129029` | `SetN2kGNSS()` | Date/time, latitude, longitude, altitude, GNSS type/method, satellites, DOP values; sent as fast-packet |
| `59904` | `SetN2kPGNISORequest()` | Requested PGN as 3 little-endian bytes |
| `126208` | Group Function handler | Function code, target PGN, interval, offset, parameter-pair count |

Schedule configuration changes only the timing/enabled state of transmitted PGNs. The GNSS payload content still comes from the UM980 data path.

## CAN Debug Tool Setup

Use a CAN adapter that supports classic extended frames at 250 kbit/s. Examples include PCAN, Kvaser, CANable/candleLight, ValueCAN, or a Linux SocketCAN adapter.

Physical requirements:

1. Use an NMEA2000/CAN transceiver between the ESP32 TWAI pins and the bus.
2. Connect CAN-H, CAN-L, and ground correctly.
3. Ensure exactly two 120 ohm terminators are present on the bus.
4. Power the NMEA2000 backbone if the test setup uses a real N2K cable harness.
5. Keep the analyzer in silent/listen-only mode for passive captures unless a stimulus frame is being injected.

Linux SocketCAN example:

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000 sample-point 0.875
sudo ip link set can0 up
candump -tz -x can0
```

For Windows vendor tools, configure the channel as:

```text
Bitrate: 250000
Frame type: Extended 29-bit
CAN-FD: Disabled
Listen-only: Enabled for baseline capture
Timestamping: Enabled
```

## Baseline Capture Procedure

1. Build and flash the firmware.
2. Start the serial monitor and confirm:

```text
NMEA2000 gateway configured
TWAI node started on TX=22 RX=21 bitrate=250000
NMEA2000 gateway opened
```

3. Start the CAN analyzer before GNSS data becomes valid.
4. Capture at least 60 seconds of traffic.
5. Confirm address claim and product/configuration traffic from the device.
6. After GNSS is fixed, confirm periodic PGNs `129025`, `129026`, and `129029`.

Pass criteria:

| Check | Expected result |
| --- | --- |
| Bus opens | Serial log reports `NMEA2000 gateway opened` |
| CAN state | No repeating TWAI error logs |
| Frame format | Extended classic CAN frames only |
| PGN 129025 | About 1 Hz after valid GNSS fix |
| PGN 129026 | About 1 Hz, about 20 ms after 129025 |
| PGN 129029 | About 1 Hz, about 40 ms after 129025 |
| Payload validity | Position, COG/SOG, UTC/date, fix method, and satellite count match GNSS input |
| Error counters | No steady TX fail growth, bus-off, or receive overflow |

## Stimulus Tests

Assumptions for the raw CAN examples below:

| Symbol | Value | Meaning |
| --- | --- | --- |
| DUT address | `0x16` | Gateway source address, preferred by the firmware as decimal 22 |
| Tester address | `0x50` | Source address used by the CAN debug tool |
| CAN type | Extended classic CAN | 29-bit identifier, no CAN-FD |
| Padding | `FF` | Use `FF` for unused bytes if the tool requires 8-byte frames |

If the gateway loses address claim and uses another source address, replace `0x16` in the CAN ID with the actual gateway source address observed in PGN `60928`.

### ISO Request Smoke Tests

Use PGN `59904` to request standard device information. These are single-frame messages.

| Test | CAN ID | Data bytes to send | Expected response |
| --- | --- | --- | --- |
| Request address claim, PGN 60928 | `0x18EA1650` | `00 EE 00 FF FF FF FF FF` | PGN `60928` from gateway |
| Request transmit/receive PGN list, PGN 126464 | `0x18EA1650` | `00 EE 01 FF FF FF FF FF` | PGN `126464`; transmit list includes `129025`, `129026`, `129029` |
| Request product information, PGN 126996 | `0x18EA1650` | `14 F0 01 FF FF FF FF FF` | PGN `126996`; model text is `GNSS NMEA2000 gateway` |
| Request configuration information, PGN 126998 | `0x18EA1650` | `16 F0 01 FF FF FF FF FF` | PGN `126998` or ISO acknowledgement if not available |

ID construction for these ISO requests:

```text
priority 6, PGN 59904, destination 0x16, source 0x50
CAN ID = 0x18EA1650
```

### Group Function Frame Format

The gateway handles PGN `126208` Request Group Function for PGNs `129025`, `129026`, and `129029`. The logical request payload is 11 bytes:

```text
Byte0      Group Function Code = 0x00
Byte1-3    Target PGN, little endian
Byte4-7    TransmissionInterval, little endian uint32, ms
Byte8-9    TransmissionIntervalOffset, little endian uint16, 10 ms units
Byte10     NumberOfParameterPairs = 0x00
```

Because the payload is 11 bytes, a raw CAN tool must send it as an NMEA2000 Fast Packet. With the assumptions above, send both frames with CAN ID `0x0DED1650`:

```text
priority 3, PGN 126208, destination 0x16, source 0x50
CAN ID = 0x0DED1650
```

The first data byte in each CAN frame is the fast-packet frame counter. The second byte in frame 0 is the logical payload length, `0B`.

Useful encodings:

| Value | Bytes |
| --- | --- |
| PGN `129025` | `01 F8 01` |
| PGN `129026` | `02 F8 01` |
| PGN `129029` | `05 F8 01` |
| interval `0 ms`, disable | `00 00 00 00` |
| interval `500 ms` | `F4 01 00 00` |
| interval `1000 ms` | `E8 03 00 00` |
| interval `2000 ms` | `D0 07 00 00` |
| interval restore default, `0xfffffffe` | `FE FF FF FF` |
| interval keep current, `0xffffffff` | `FF FF FF FF` |
| offset keep current | `FF FF` |
| offset `0 ms` | `00 00` |
| offset `20 ms` | `02 00` |
| offset `40 ms` | `04 00` |

### Interval Change

Send a directed group function request for PGN `129025`, `129026`, or `129029` with a supported interval. Then measure the cycle time in the analyzer.

Expected result:

| Stimulus | Frames to send | Expected result |
| --- | --- | --- |
| Set PGN `129025` to 500 ms, keep offset | `0x0DED1650  00 0B 00 01 F8 01 F4 01` then `0x0DED1650  01 00 00 FF FF 00 FF FF` | PGN `129025` repeats at about 500 ms |
| Set PGN `129025` to 1000 ms, offset 0 ms | `0x0DED1650  00 0B 00 01 F8 01 E8 03` then `0x0DED1650  01 00 00 00 00 00 FF FF` | PGN `129025` repeats at about 1000 ms |
| Set PGN `129026` to 1000 ms, offset 20 ms | `0x0DED1650  00 0B 00 02 F8 01 E8 03` then `0x0DED1650  01 00 00 02 00 00 FF FF` | PGN `129026` repeats at about 1000 ms and is offset after `129025` |
| Set PGN `129029` to 2000 ms, offset 40 ms | `0x0DED1650  00 0B 00 05 F8 01 D0 07` then `0x0DED1650  01 00 00 04 00 00 FF FF` | PGN `129029` repeats at about 2000 ms |
| Disable PGN `129025` | `0x0DED1650  00 0B 00 01 F8 01 00 00` then `0x0DED1650  01 00 00 FF FF 00 FF FF` | PGN `129025` stops |
| Restore PGN `129025` defaults | `0x0DED1650  00 0B 00 01 F8 01 FE FF` then `0x0DED1650  01 FF FF FF FF 00 FF FF` | PGN `129025` returns to about 1000 ms |

### Offset Change

Send a directed group function request with a valid transmission interval offset. The firmware interprets the offset field in 10 ms units.

Expected result: the first next send is scheduled at `now + offset * 10 ms`; subsequent sends follow the configured interval.

### Invalid Interval

Send a directed group function request below 100 ms or above 60000 ms.

Expected result: the device acknowledges with a transmission/priority error and does not apply the invalid interval.

Example invalid interval test for PGN `129025`, interval `50 ms`:

```text
0x0DED1650  00 0B 00 01 F8 01 32 00
0x0DED1650  01 00 00 FF FF 00 FF FF
```

Expected response: PGN `126208` Acknowledge from the gateway. Decode the acknowledge payload and confirm the transmission/priority error code is `1`, meaning transmission interval or priority not supported. The PGN period must remain unchanged.

### Parameter Pair Rejection

The current firmware accepts only `NumberOfParameterPairs = 0`. To test rejection, send a valid interval but set the last logical payload byte to `01`.

Example for PGN `129025`, interval `1000 ms`, one unsupported parameter pair:

```text
0x0DED1650  00 0B 00 01 F8 01 E8 03
0x0DED1650  01 00 00 FF FF 01 FF FF
```

Expected response: PGN `126208` Acknowledge with a parameter error. The configured schedule must not change.

### Acknowledge Decoding

For directed PGN `126208` requests, decode the gateway response as an NMEA2000 Group Function Acknowledge:

```text
Byte0      Group Function Code = 0x02, Acknowledge
Byte1-3    Target PGN, little endian
Byte4      Low nibble: PGN error code; high nibble: transmission/priority error code
Byte5      NumberOfParameterPairs
```

Important codes:

| Field | Value | Meaning |
| --- | --- | --- |
| PGN error code | `0` | Acknowledge, no PGN error |
| PGN error code | `1` | PGN not supported |
| PGN error code | `4` | Request or command not supported |
| Transmission/priority error code | `0` | Acknowledge, no interval/priority error |
| Transmission/priority error code | `1` | Transmission interval or priority not supported |
| Parameter error code | `0` | Acknowledge, no parameter error |
| Parameter error code | `5` | Request or command parameter not supported |

Some CAN tools show the NMEA2000 fast-packet wrapper bytes; some show only the decoded logical PGN payload. Use the NMEA2000 decoder view when available.

### Broadcast Request

To test broadcast handling, change the destination byte in the CAN ID from `0x16` to `0xFF`:

```text
0x0DEDFF50  00 0B 00 01 F8 01 E8 03
0x0DEDFF50  01 00 00 00 00 00 FF FF
```

Expected result: the schedule is applied, but the gateway does not send a PGN `126208` acknowledge because the request was broadcast.

### Missing GNSS Fix

Block or disconnect GNSS input, or provide stale data.

Expected result: the gateway keeps parsing NMEA2000 traffic but stops emitting the GNSS data PGNs because the conversion path rejects invalid input.

## Decoding Captured Frames

NMEA2000 uses the 29-bit CAN identifier to encode priority, PGN, destination, and source. Use the analyzer's NMEA2000 decoder if available. If manually decoding, verify at least:

| Field | Validation |
| --- | --- |
| IDE | Must be extended |
| RTR | Must be false |
| FDF/BRS | Must be false |
| Source | Usually preferred address 22, unless address claim changed it |
| Priority | 2 for PGN 129025/129026, 3 for PGN 129029 |
| PGN | One of the expected gateway PGNs or required NMEA2000 management PGNs |
| DLC | 8 for single CAN frames; fast-packet segments also use classic 8-byte CAN payloads |

Common management PGNs to expect during startup and network interaction:

| PGN | Purpose |
| --- | --- |
| 59904 | ISO Request |
| 60928 | ISO Address Claim |
| 126208 | NMEA group function |
| 126464 | Transmit/receive PGN list |
| 126996 | Product information |
| 126998 | Configuration information |

## TWAI Health Indicators

The ESP32 adapter records health counters in `tNMEA2000_esp32::tCANStatus`:

| Counter | Meaning |
| --- | --- |
| `TxDoneCount` | Successful TWAI transmissions |
| `TxFailCount` | Failed transmissions |
| `RxFrameCount` | Received extended classic frames accepted by the driver |
| `ErrorCount` | TWAI error callback count |
| `StateChangeCount` | TWAI state changes |
| `LastErrorFlags` | Last TWAI error flags |
| `LastState` | Last TWAI error state |
| `TxErrorCount` | Current TWAI TX error counter |
| `RxErrorCount` | Current TWAI RX error counter |
| `BusErrorCount` | Controller bus error record |

Useful failure clues:

| Symptom | Likely cause | Check |
| --- | --- | --- |
| ACK errors or TX failures | No other active node, missing termination, wrong bitrate, disconnected transceiver | Add a second node/analyzer, verify 250 kbit/s and termination |
| Bus-off recovery logs | Wiring fault, CAN-H/CAN-L swapped, bad transceiver, shorted bus | Inspect wiring and scope differential signal |
| No RX frames | Analyzer silent-only is fine, but another NMEA2000 node should be visible on a real bus | Verify backbone power and analyzer channel |
| No GNSS PGNs | GNSS fix invalid or stale | Check UM980 serial logs and fix age |
| Startup open failure | TWAI node creation/configuration failed | Verify ESP-IDF target supports TWAI and pins are valid |

## Functional Test Matrix

Use this matrix when adding or validating modules that depend on the NMEA2000 path.

| Test ID | Area | Stimulus | Expected CAN evidence | Expected firmware evidence |
| --- | --- | --- | --- | --- |
| N2K-BOOT-001 | Startup | Boot with analyzer connected | Address claim and management traffic appear | `NMEA2000 gateway opened` |
| N2K-GNSS-001 | Position output | Provide valid GNSS fix | PGN 129025 at about 1 Hz | No send failure growth |
| N2K-GNSS-002 | COG/SOG output | Move or replay GNSS speed/course data | PGN 129026 at about 1 Hz, decoded COG/SOG matches input | No conversion errors |
| N2K-GNSS-003 | GNSS detail output | Provide valid UTC/date/fix/satellites | PGN 129029 at about 1 Hz, decoded fields match input | GNSS data accepted |
| N2K-GNSS-004 | Stale fix rejection | Stop GNSS updates for more than 3000 ms | PGNs 129025/129026/129029 stop | Converter rejects stale fix |
| N2K-GF-001 | Interval command | Request 500 ms interval for PGN 129025 | Directed acknowledge, PGN 129025 at about 500 ms | Schedule updated |
| N2K-GF-002 | Disable command | Request interval 0 for PGN 129026 | Directed acknowledge, PGN 129026 stops | Schedule disabled |
| N2K-GF-003 | Restore command | Request restore interval | Directed acknowledge, PGN returns to 1000 ms | Schedule restored |
| N2K-GF-004 | Invalid interval | Request 50 ms interval for PGN 129025 | Directed acknowledge reports interval unsupported; PGN period unchanged | Schedule not changed |
| N2K-GF-005 | Unsupported parameter pair | Request interval with `NumberOfParameterPairs = 1` | Directed acknowledge reports parameter error; PGN period unchanged | Schedule not changed |
| N2K-GF-006 | Broadcast group request | Send PGN 126208 to destination `0xFF` | Schedule changes; no acknowledge frame | Schedule updated |
| N2K-ISO-001 | ISO request | Request PGN 126996 with PGN 59904 | Product information response | Device info available |
| N2K-ISO-002 | PGN list | Request PGN 126464 with PGN 59904 | Transmit list contains 129025, 129026, 129029 | Device advertises expected PGNs |
| N2K-ERR-001 | Physical fault | Disconnect CAN-H or CAN-L briefly | Analyzer sees errors; device may enter recovery | TWAI error/state logs, then recovery |

## Unit Test Targets

There is no dedicated unit-test framework in this repository yet, but these are the high-value unit tests to add if a host-side test target is introduced:

| Unit | Test input | Expected result |
| --- | --- | --- |
| `ReadGatewayGnssData()` | Valid UM980 fix with fresh fix age | Returns true; fills latitude, longitude, altitude, COG, SOG, date, time, satellites, and GNSS method |
| `ReadGatewayGnssData()` | Fix age greater than 3000 ms | Returns false |
| `ReadGatewayGnssData()` | RTK fixed, RTK float, DGNSS, normal fix, no fix | Maps to the matching `tN2kGNSSmethod` |
| `BuildGatewayN2kMessages()` | Complete `tGatewayGnssData` | Builds PGNs `129025`, `129026`, and `129029` |
| `BuildGatewayN2kMessages()` | Missing position, time, date, speed/course, or fix | Returns false |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Interval `0` | Disables the matching schedule |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Interval `500`, offset keep-current | Applies 500 ms interval |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Restore default interval | Restores default interval and offset |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Interval below 100 ms or above 60000 ms | Rejects request and leaves schedule unchanged |
| `tGatewayPgnGroupFunctionHandler::HandleRequest()` | Nonzero parameter-pair count | Rejects request and leaves schedule unchanged |

## Omissions and Open Items

The document now covers bus settings, expected PGNs, CAN IDs, raw stimulus frames, timing checks, negative tests, unit-test targets, and evidence format. The remaining items are not firmware-document omissions, but must be decided or captured by the test team for each validation run:

| Item | Status | Action for testing team |
| --- | --- | --- |
| Actual DUT source address | Runtime-dependent | Capture PGN `60928` and replace `0x16` in directed CAN IDs if needed |
| CAN adapter model/channel | Lab-dependent | Record adapter name, serial number, channel, driver/tool version |
| Physical bus topology | Lab-dependent | Record termination, cable harness, backbone power, transceiver board |
| GNSS stimulus source | Lab-dependent | Record whether using live sky, simulator, serial replay, or UM980 canned data |
| Exact decoded PGN `129029` payload values | Input-dependent | Compare against known GNSS input for each run |
| Automated unit test framework | Not present in repo | Add host-side tests before claiming unit-test automation coverage |
| Direct runtime exposure of `tCANStatus` | Not exposed as CLI/API | Use serial logs and CAN evidence unless a debug endpoint is added |
| Vendor-specific CAN tool commands | Tool-dependent | Translate the raw ID/data rows into PCAN/Kvaser/Vector/etc. send syntax |

Recommended release gate for test documentation:

1. One raw CAN capture file for baseline boot and address claim.
2. One raw CAN capture file showing valid GNSS output for at least 60 seconds.
3. One capture showing each Group Function case: interval change, disable, restore, invalid interval, parameter-pair rejection, and broadcast request.
4. One serial log matching the same firmware commit as the CAN captures.
5. One filled evidence template per hardware/test-tool combination.

## Evidence Template

Store raw captures and a short decoded report for every validation run.

```text
Test run:
Firmware commit:
ESP-IDF version:
Target hardware:
CAN adapter:
Channel:
Bitrate:
Sample point:
Listen-only:
Bus termination:
Capture file:
Serial log:

Observed frames:
- PGN:
  Source:
  Priority:
  DLC:
  Period min/avg/max:
  Payload summary:
  Pass/fail:

TWAI status:
- TxDoneCount:
- TxFailCount:
- RxFrameCount:
- ErrorCount:
- StateChangeCount:
- LastState:
- TxErrorCount:
- RxErrorCount:
- BusErrorCount:

Result:
Notes:
```

## Maintenance Notes

Update this guide when any of these change:

1. CAN pins, bitrate, sample point, or TWAI mode.
2. Transmitted PGN list in `main/nmea0183_to_n2k.cpp`.
3. Scheduling logic in `main/App/DataProc/DP_NMEA2000.cpp`.
4. Driver behavior or counters in `components/NMEA2000/src/NMEA2000_esp32.*`.
5. Test tools, adapters, or required evidence format.
