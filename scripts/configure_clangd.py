#!/usr/bin/env python3
"""Configure VS Code clangd settings for ESP-IDF toolchains."""

from __future__ import annotations

import json
import os
import platform
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SETTINGS_PATH = REPO_ROOT / ".vscode" / "settings.json"

QUERY_DRIVER_PATTERNS = [
    "**/xtensa-esp32-elf-gcc",
    "**/xtensa-esp32-elf-g++",
    "**/xtensa-esp-elf-gcc",
    "**/xtensa-esp-elf-g++",
    "**/riscv32-esp-elf-gcc",
    "**/riscv32-esp-elf-g++",
    "${env:HOME}/.espressif/tools/**/xtensa-esp32-elf-*",
    "${env:HOME}/.espressif/tools/**/xtensa-esp-elf-*",
    "${env:HOME}/.espressif/tools/**/riscv32-esp-elf-*",
    "${env:USERPROFILE}\\.espressif\\tools\\**\\xtensa-esp32-elf-*",
    "${env:USERPROFILE}\\.espressif\\tools\\**\\xtensa-esp-elf-*",
    "${env:USERPROFILE}\\.espressif\\tools\\**\\riscv32-esp-elf-*",
    "C:/Espressif/tools/**/xtensa-esp32-elf-gcc.exe",
    "C:/Espressif/tools/**/xtensa-esp32-elf-g++.exe",
    "C:/Espressif/tools/**/xtensa-esp-elf-gcc.exe",
    "C:/Espressif/tools/**/xtensa-esp-elf-g++.exe",
    "C:/Espressif/tools/**/riscv32-esp-elf-gcc.exe",
    "C:/Espressif/tools/**/riscv32-esp-elf-g++.exe",
]

CLANGD_ARGUMENTS = [
    "--background-index",
    "--compile-commands-dir=${workspaceFolder}/build",
    "--query-driver=" + ",".join(QUERY_DRIVER_PATTERNS),
]


def candidate_tool_roots() -> list[Path]:
    roots: list[Path] = []
    for env_name in ("IDF_TOOLS_PATH", "ESPRESSIF_TOOLCHAIN_PATH"):
        value = os.environ.get(env_name)
        if value:
            roots.append(Path(value).expanduser())

    roots.append(Path.home() / ".espressif")

    userprofile = os.environ.get("USERPROFILE")
    if userprofile:
        roots.append(Path(userprofile) / ".espressif")

    if platform.system() == "Windows":
        roots.extend([Path("C:/Espressif"), Path("C:/Espressif/.espressif")])
    else:
        roots.append(Path("/opt/esp"))

    unique_roots: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        key = str(root)
        if key not in seen:
            seen.add(key)
            unique_roots.append(root)
    return unique_roots


def find_esp_clangd() -> Path | None:
    exe_name = "clangd.exe" if platform.system() == "Windows" else "clangd"
    matches: list[Path] = []

    for root in candidate_tool_roots():
        tools_root = root / "tools"
        matches.extend(tools_root.glob(f"esp-clang/*/esp-clang/bin/{exe_name}"))

    existing = [path for path in matches if path.is_file()]
    if not existing:
        return None

    return sorted(existing, key=lambda path: path.stat().st_mtime, reverse=True)[0]


def load_settings() -> dict[str, object]:
    if not SETTINGS_PATH.exists():
        return {}
    with SETTINGS_PATH.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def save_settings(settings: dict[str, object]) -> None:
    SETTINGS_PATH.parent.mkdir(parents=True, exist_ok=True)
    with SETTINGS_PATH.open("w", encoding="utf-8") as handle:
        json.dump(settings, handle, indent=2)
        handle.write("\n")


def main() -> int:
    settings = load_settings()
    settings["clangd.arguments"] = CLANGD_ARGUMENTS
    settings["C_Cpp.intelliSenseEngine"] = "disabled"

    clangd_path = find_esp_clangd()
    if clangd_path is not None:
        settings["clangd.path"] = str(clangd_path)
        print(f"Configured Espressif clangd: {clangd_path}")
    else:
        existing_path = settings.get("clangd.path")
        if isinstance(existing_path, str) and existing_path:
            expanded_path = Path(os.path.expandvars(existing_path)).expanduser()
            if not expanded_path.is_file():
                settings.pop("clangd.path", None)
                print("Espressif clangd was not found; removed stale clangd.path.")
            else:
                print("Espressif clangd was not found; existing clangd.path was left unchanged.")
        else:
            print("Espressif clangd was not found; clangd.path was not set.")

    save_settings(settings)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
