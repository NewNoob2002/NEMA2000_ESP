#!/usr/bin/env python3
"""Decode NMEA2000 GNSS frames from simple CAN receive logs.

Input lines are expected in the form:
  Recv[0DF80516]: C0 2B 01 FF FF FF FF FF

The decoder handles single-frame PGNs 129025/129026 and fast-packet PGN 129029.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


LINE_RE = re.compile(r".*\[([0-9A-Fa-f]{8})\]\s*:\s*((?:[0-9A-Fa-f]{2}\s*)+)")

PGN_POSITION_RAPID = 129025
PGN_COG_SOG_RAPID = 129026
PGN_GNSS_POSITION = 129029

HEADING_REF = {
    0: "true",
    1: "magnetic",
    2: "error",
    3: "unavailable",
}

GNSS_TYPE = {
    0: "GPS",
    1: "GLONASS",
    2: "GPS+GLONASS",
    3: "GPS+SBAS/WAAS",
    4: "GPS+SBAS/WAAS+GLONASS",
    5: "Chayka",
    6: "integrated",
    7: "surveyed",
    8: "Galileo",
}

GNSS_METHOD = {
    0: "no GNSS",
    1: "GNSS fix",
    2: "DGNSS",
    3: "precise GNSS",
    4: "RTK fixed",
    5: "RTK float",
    6: "estimated/dead reckoning",
    7: "manual",
    8: "simulated",
    15: "unavailable",
}


@dataclass(frozen=True)
class N2kFrame:
    can_id: int
    priority: int
    pgn: int
    source: int
    destination: int
    data: bytes


@dataclass
class FastPacketState:
    expected_len: int
    payload: bytearray
    next_frame: int


def parse_can_id(can_id: int, data: bytes) -> N2kFrame:
    priority = (can_id >> 26) & 0x07
    edp = (can_id >> 25) & 0x01
    dp = (can_id >> 24) & 0x01
    pf = (can_id >> 16) & 0xff
    ps = (can_id >> 8) & 0xff
    source = can_id & 0xff

    if pf < 240:
        pgn = (edp << 17) | (dp << 16) | (pf << 8)
        destination = ps
    else:
        pgn = (edp << 17) | (dp << 16) | (pf << 8) | ps
        destination = 0xff

    return N2kFrame(can_id, priority, pgn, source, destination, data)


def parse_line(line: str) -> N2kFrame | None:
    match = LINE_RE.match(line)
    if not match:
        return None

    can_id = int(match.group(1), 16)
    data = bytes(int(part, 16) for part in match.group(2).split())
    return parse_can_id(can_id, data)


def s32(value: bytes) -> int:
    return struct.unpack_from("<i", value)[0]


def s64(value: bytes) -> int:
    return struct.unpack_from("<q", value)[0]


def u16(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<H", payload, offset)[0]


def s16(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<h", payload, offset)[0]


def u32(payload: bytes, offset: int) -> int:
    return struct.unpack_from("<I", payload, offset)[0]


def fmt(value: float | int | str | None, unit: str = "") -> str:
    if value is None:
        return "NA"
    if isinstance(value, float):
        text = f"{value:.8f}".rstrip("0").rstrip(".")
    else:
        text = str(value)
    return f"{text} {unit}".rstrip()


def decode_i32_scaled(payload: bytes, offset: int, factor: float) -> float | None:
    raw = s32(payload[offset : offset + 4])
    if raw == 0x7FFFFFFF:
        return None
    return raw * factor


def decode_i64_scaled(payload: bytes, offset: int, factor: float) -> float | None:
    raw = s64(payload[offset : offset + 8])
    if raw == 0x7FFFFFFFFFFFFFFF:
        return None
    return raw * factor


def decode_u16_scaled(payload: bytes, offset: int, factor: float) -> float | None:
    raw = u16(payload, offset)
    if raw == 0xFFFF:
        return None
    return raw * factor


def decode_s16_scaled(payload: bytes, offset: int, factor: float) -> float | None:
    raw = s16(payload, offset)
    if raw == 0x7FFF:
        return None
    return raw * factor


def decode_u32_scaled(payload: bytes, offset: int, factor: float) -> float | None:
    raw = u32(payload, offset)
    if raw == 0xFFFFFFFF:
        return None
    return raw * factor


def decode_u16_na(payload: bytes, offset: int) -> int | None:
    raw = u16(payload, offset)
    return None if raw == 0xFFFF else raw


def decode_u8_na(value: int) -> int | None:
    return None if value == 0xFF else value


def days_to_ymd(days: int | None) -> str | None:
    if days is None:
        return None
    try:
        from datetime import date, timedelta

        return str(date(1970, 1, 1) + timedelta(days=days))
    except OverflowError:
        return None


def seconds_to_hms(seconds: float | None) -> str | None:
    if seconds is None:
        return None
    total_ms = int(round(seconds * 1000))
    ms = total_ms % 1000
    total_s = total_ms // 1000
    s = total_s % 60
    total_m = total_s // 60
    m = total_m % 60
    h = total_m // 60
    return f"{h:02d}:{m:02d}:{s:02d}.{ms:03d}"


def decode_129025(payload: bytes) -> str:
    lat = decode_i32_scaled(payload, 0, 1e-7)
    lon = decode_i32_scaled(payload, 4, 1e-7)
    return f"129025 Position Rapid: lat={fmt(lat, 'deg')}, lon={fmt(lon, 'deg')}"


def decode_129026(payload: bytes) -> str:
    sid = decode_u8_na(payload[0])
    ref = HEADING_REF.get(payload[1] & 0x03, f"unknown({payload[1] & 0x03})")
    cog = decode_u16_scaled(payload, 2, 0.0001)
    sog = decode_u16_scaled(payload, 4, 0.01)
    return (
        "129026 COG/SOG Rapid: "
        f"sid={fmt(sid)}, ref={ref}, cog={fmt(cog, 'rad')}, sog={fmt(sog, 'm/s')}"
    )


def decode_129029(payload: bytes) -> str:
    sid = decode_u8_na(payload[0])
    days = decode_u16_na(payload, 1)
    seconds = decode_u32_scaled(payload, 3, 0.0001)
    lat = decode_i64_scaled(payload, 7, 1e-16)
    lon = decode_i64_scaled(payload, 15, 1e-16)
    altitude = decode_i64_scaled(payload, 23, 1e-6)
    type_method = payload[31]
    gnss_type = type_method & 0x0F
    gnss_method = (type_method >> 4) & 0x0F
    integrity = payload[32] & 0x03
    satellites = decode_u8_na(payload[33])
    hdop = decode_s16_scaled(payload, 34, 0.01)
    pdop = decode_s16_scaled(payload, 36, 0.01)
    geoidal_sep = decode_i32_scaled(payload, 38, 0.01)
    ref_stations = decode_u8_na(payload[42])

    return (
        "129029 GNSS Position Data: "
        f"sid={fmt(sid)}, date={fmt(days_to_ymd(days))}, time={fmt(seconds_to_hms(seconds))}, "
        f"lat={fmt(lat, 'deg')}, lon={fmt(lon, 'deg')}, altitude={fmt(altitude, 'm')}, "
        f"type={GNSS_TYPE.get(gnss_type, f'unknown({gnss_type})')}, "
        f"method={GNSS_METHOD.get(gnss_method, f'unknown({gnss_method})')}, "
        f"integrity={integrity}, sats={fmt(satellites)}, hdop={fmt(hdop)}, "
        f"pdop={fmt(pdop)}, geoidal_sep={fmt(geoidal_sep, 'm')}, ref_stations={fmt(ref_stations)}"
    )


def decode_payload(frame: N2kFrame, payload: bytes) -> str | None:
    if frame.pgn == PGN_POSITION_RAPID and len(payload) >= 8:
        return decode_129025(payload)
    if frame.pgn == PGN_COG_SOG_RAPID and len(payload) >= 8:
        return decode_129026(payload)
    if frame.pgn == PGN_GNSS_POSITION and len(payload) >= 43:
        return decode_129029(payload)
    return None


def decode_lines(lines: Iterable[str], show_raw: bool) -> Iterable[str]:
    fast_packets: dict[tuple[int, int, int], FastPacketState] = {}

    for line_no, line in enumerate(lines, 1):
        frame = parse_line(line)
        if frame is None:
            continue

        prefix = f"line {line_no}: id=0x{frame.can_id:08X} pgn={frame.pgn} src=0x{frame.source:02X}"
        if frame.pgn == PGN_GNSS_POSITION:
            fp_header = frame.data[0]
            sequence = fp_header >> 5
            frame_index = fp_header & 0x1F
            key = (frame.source, frame.pgn, sequence)

            if frame_index == 0:
                expected_len = frame.data[1]
                fast_packets[key] = FastPacketState(expected_len, bytearray(frame.data[2:]), 1)
                continue

            state = fast_packets.get(key)
            if state is None or state.next_frame != frame_index:
                yield f"{prefix}: fast-packet out of sequence seq={sequence} frame={frame_index}"
                fast_packets.pop(key, None)
                continue

            state.payload.extend(frame.data[1:])
            state.next_frame += 1
            if len(state.payload) >= state.expected_len:
                payload = bytes(state.payload[: state.expected_len])
                fast_packets.pop(key, None)
                decoded = decode_payload(frame, payload)
                if decoded:
                    raw = f" raw={payload.hex(' ').upper()}" if show_raw else ""
                    yield f"{prefix}: {decoded}{raw}"
            continue

        decoded = decode_payload(frame, frame.data)
        if decoded:
            raw = f" raw={frame.data.hex(' ').upper()}" if show_raw else ""
            yield f"{prefix}: {decoded}{raw}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logfile", nargs="?", type=Path, help="Log file to decode. Reads stdin when omitted.")
    parser.add_argument("--raw", action="store_true", help="Append decoded payload bytes to each output line.")
    args = parser.parse_args()

    if args.logfile:
        with args.logfile.open("r", encoding="utf-8", errors="replace") as handle:
            for output in decode_lines(handle, args.raw):
                print(output)
    else:
        for output in decode_lines(sys.stdin, args.raw):
            print(output)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
