#!/usr/bin/env python3
"""Regenerate main/form.h from main/AP-Config source files.

Each source file is gzip-compressed and embedded as a PROGMEM uint8_t array.
The resulting header is used by the ESP32 web server to serve the SPA.
"""

import gzip
import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
AP_CONFIG_DIR = os.path.join(PROJECT_ROOT, "main", "AP-Config")
OUTPUT_PATH = os.path.join(PROJECT_ROOT, "main", "form.h")

# Map source file paths → C variable name suffix
FILES = [
    ("src/main.js", "main_js"),
    ("index.html", "index_html"),
    ("src/style.css", "style_css"),
    ("favicon.ico", "favicon_ico"),
    ("singularxyz.png", "singularxyz_png"),
]


def file_to_c_array(data: bytes, name: str) -> str:
    """Convert binary data to a C uint8_t PROGMEM array string."""
    lines = []
    lines.append(f"static const uint8_t {name}[] PROGMEM = {{")

    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        hex_bytes = ", ".join(f"0x{b:02X}" for b in chunk)
        comma = "," if i + 12 < len(data) else ""
        lines.append(f"  {hex_bytes}{comma}")

    lines.append("};")
    lines.append(f"const uint32_t {name}_len = sizeof({name});")
    return "\n".join(lines)


def main():
    if not os.path.isdir(AP_CONFIG_DIR):
        print(f"Error: AP-Config directory not found: {AP_CONFIG_DIR}", file=sys.stderr)
        sys.exit(1)

    sections = [
        "// Auto-generated from main/AP-Config resources.",
        "// Files are gzip-compressed and served with Content-Encoding: gzip.",
        "#pragma once",
        "",
        '#include <Arduino.h>',
        "",
    ]

    for rel_path, var_name in FILES:
        src_path = os.path.join(AP_CONFIG_DIR, rel_path)
        if not os.path.isfile(src_path):
            print(f"Warning: skipping missing file: {src_path}", file=sys.stderr)
            continue

        with open(src_path, "rb") as f:
            raw = f.read()

        compressed = gzip.compress(raw, compresslevel=9)
        ratio = (1 - len(compressed) / len(raw)) * 100 if raw else 0
        print(f"  {rel_path}: {len(raw)} → {len(compressed)} bytes ({ratio:+.1f}%)")

        sections.append(file_to_c_array(compressed, var_name))
        sections.append("")

    with open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(sections))

    print(f"\nWrote {OUTPUT_PATH}")


if __name__ == "__main__":
    main()
