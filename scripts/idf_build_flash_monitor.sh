#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  ./scripts/idf_build_flash_monitor.sh

Interactive ESP-IDF workflow:
  - Build, flash and monitor
  - Size reports
  - Menuconfig, reconfigure, clean and fullclean
  - Merge bin and UF2 generation

Environment:
  IDF_PATH can override the default /home/gtc/esp/esp-idf
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ $# -ne 0 ]]; then
    echo "error: this script is interactive and does not accept positional arguments" >&2
    usage >&2
    exit 2
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
IDF_PATH="${IDF_PATH:-/home/gtc/esp/esp-idf}"
IDF_EXPORT="$IDF_PATH/export.sh"

if [[ ! -f "$IDF_EXPORT" ]]; then
    echo "error: ESP-IDF export.sh not found: $IDF_EXPORT" >&2
    echo "       Set IDF_PATH to the ESP-IDF directory, then retry." >&2
    exit 1
fi

prompt_choice() {
    local title="$1"
    local result_var="$2"
    shift 2
    local options=("$@")
    local choice

    echo
    echo "$title"
    local i
    for i in "${!options[@]}"; do
        printf "  %d) %s\n" "$((i + 1))" "${options[$i]}"
    done

    while true; do
        read -r -p "Select [1-${#options[@]}]: " choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#options[@]} )); then
            printf -v "$result_var" "%s" "${options[$((choice - 1))]}"
            return 0
        fi
        echo "Invalid selection."
    done
}

prompt_custom_number() {
    local title="$1"
    local result_var="$2"
    local value

    while true; do
        read -r -p "$title: " value
        if [[ "$value" =~ ^[0-9]+$ ]]; then
            printf -v "$result_var" "%s" "$value"
            return 0
        fi
        echo "Please enter a numeric baud rate."
    done
}

confirm() {
    local prompt="$1"
    local answer

    read -r -p "$prompt [y/N]: " answer
    [[ "$answer" == "y" || "$answer" == "Y" || "$answer" == "yes" || "$answer" == "YES" ]]
}

detect_ports() {
    local ports=()
    local dev

    shopt -s nullglob
    for dev in /dev/serial/by-id/* /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        if [[ "$dev" == /dev/serial/by-id/* ]]; then
            ports+=("$dev -> $(readlink -f "$dev")")
        else
            ports+=("$dev")
        fi
    done
    shopt -u nullglob

    printf "%s\n" "${ports[@]}"
}

select_port() {
    local result_var="$1"
    local ports=()
    local selected raw_port custom_port

    while IFS= read -r raw_port; do
        [[ -n "$raw_port" ]] && ports+=("$raw_port")
    done < <(detect_ports)

    ports+=("Manual input")
    prompt_choice "Serial port" selected "${ports[@]}"

    if [[ "$selected" == "Manual input" ]]; then
        read -r -p "Port path, for example /dev/ttyUSB0: " custom_port
        if [[ -z "$custom_port" ]]; then
            echo "error: serial port cannot be empty" >&2
            exit 2
        fi
        printf -v "$result_var" "%s" "$custom_port"
        return 0
    fi

    printf -v "$result_var" "%s" "${selected%% -> *}"
}

select_baud() {
    local title="$1"
    local result_var="$2"
    shift 2
    local selected
    local options=("$@" "Custom input")

    prompt_choice "$title" selected "${options[@]}"
    if [[ "$selected" == "Custom input" ]]; then
        prompt_custom_number "$title" "$result_var"
    else
        printf -v "$result_var" "%s" "$selected"
    fi
}

print_build_artifacts() {
    python3 - "$PROJECT_DIR/build" <<'PY'
import json
import os
import sys
import glob
from datetime import datetime

build_dir = sys.argv[1]
desc_path = os.path.join(build_dir, "project_description.json")
flasher_path = os.path.join(build_dir, "flasher_args.json")

def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {}

def fmt_size(size):
    for unit in ("B", "KiB", "MiB"):
        if size < 1024 or unit == "MiB":
            return f"{size:.1f} {unit}" if unit != "B" else f"{size} {unit}"
        size /= 1024
    return f"{size:.1f} MiB"

desc = load_json(desc_path)
flasher = load_json(flasher_path)

project_name = desc.get("project_name", "unknown")
project_version = desc.get("project_version", "unknown")
target = desc.get("target", "unknown")
idf_version = desc.get("git_revision", "unknown")
app_bin = desc.get("app_bin")
app_elf = desc.get("app_elf")
bootloader_elf = desc.get("bootloader_elf")

print()
print("==================================================")
print("Build output")
print("==================================================")
print(f"Project : {project_name}")
print(f"Version : {project_version}")
print(f"Target  : {target}")
print(f"ESP-IDF : {idf_version}")

entries = []
if app_elf:
    entries.append(("app elf", os.path.join(build_dir, app_elf), ""))
if app_bin:
    entries.append(("app bin", os.path.join(build_dir, app_bin), ""))
if bootloader_elf:
    entries.append(("bootloader elf", bootloader_elf, ""))

flash_files = flasher.get("flash_files", {})
if isinstance(flash_files, dict):
    for offset, rel_path in sorted(flash_files.items(), key=lambda item: int(item[0], 0)):
        path = rel_path if os.path.isabs(rel_path) else os.path.join(build_dir, rel_path)
        entries.append((f"flash @ {offset}", path, offset))

for pattern in ("merged-binary.*", "uf2.bin", "*.uf2"):
    for path in sorted(glob.glob(os.path.join(build_dir, pattern))):
        entries.append(("generated", path, ""))

seen = set()
for label, path, _offset in entries:
    path = os.path.normpath(path)
    key = (label, path)
    if key in seen:
        continue
    seen.add(key)

    if not os.path.exists(path):
        print(f"{label:16} missing  {path}")
        continue

    stat = os.stat(path)
    mtime = datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
    rel = os.path.relpath(path, os.path.dirname(build_dir))
    print(f"{label:16} {fmt_size(stat.st_size):>10}  {mtime}  {rel}")

print("==================================================")
PY
}

run_build() {
    echo
    echo "=================================================="
    echo "[1/3] Build"
    echo "=================================================="
    idf.py build
    print_build_artifacts
}

run_idf_action() {
    local title="$1"
    shift

    echo
    echo "=================================================="
    echo "$title"
    echo "=================================================="
    idf.py "$@"
}

run_flash() {
    local port="$1"
    local baud="$2"

    echo
    echo "=================================================="
    echo "[2/3] Flash"
    echo "  port: $port"
    echo "  baud: $baud"
    echo "=================================================="
    idf.py -p "$port" -b "$baud" flash
}

run_flash_with_target() {
    local port="$1"
    local baud="$2"
    local target="$3"

    echo
    echo "=================================================="
    echo "[2/3] $target"
    echo "  port: $port"
    echo "  baud: $baud"
    echo "=================================================="
    idf.py -p "$port" -b "$baud" "$target"
}

run_monitor() {
    local port="$1"
    local baud="$2"

    echo
    echo "=================================================="
    echo "[3/3] Monitor"
    echo "  port: $port"
    echo "  baud: $baud"
    echo "=================================================="
    idf.py -p "$port" -b "$baud" monitor
}

run_size_report() {
    local report="$1"

    case "$report" in
        "Summary")
            run_idf_action "Size summary" size
            ;;
        "By component")
            run_idf_action "Size by component" size-components
            ;;
        "By source file")
            run_idf_action "Size by source file" size-files
            ;;
    esac
}

# shellcheck source=/dev/null
source "$IDF_EXPORT" >/dev/null
cd "$PROJECT_DIR"

prompt_choice "Action" ACTION \
    "Build only" \
    "Build + flash" \
    "Build + flash + monitor" \
    "Monitor only" \
    "Size report" \
    "Menuconfig" \
    "Reconfigure" \
    "Clean" \
    "Fullclean" \
    "Erase flash" \
    "Encrypted flash" \
    "Merge bin" \
    "UF2"

PORT=""
FLASH_BAUD=""
MONITOR_BAUD=""

case "$ACTION" in
    "Build only")
        run_build
        ;;
    "Build + flash")
        select_port PORT
        select_baud "Flash baud rate" FLASH_BAUD 460800 921600 1500000 2000000
        run_build
        run_flash "$PORT" "$FLASH_BAUD"
        ;;
    "Build + flash + monitor")
        select_port PORT
        select_baud "Flash baud rate" FLASH_BAUD 460800 921600 1500000 2000000
        select_baud "Monitor baud rate" MONITOR_BAUD 115200 230400 460800 921600
        run_build
        run_flash "$PORT" "$FLASH_BAUD"
        run_monitor "$PORT" "$MONITOR_BAUD"
        ;;
    "Monitor only")
        select_port PORT
        select_baud "Monitor baud rate" MONITOR_BAUD 115200 230400 460800 921600
        run_monitor "$PORT" "$MONITOR_BAUD"
        ;;
    "Size report")
        prompt_choice "Size report" SIZE_REPORT "Summary" "By component" "By source file"
        run_size_report "$SIZE_REPORT"
        ;;
    "Menuconfig")
        run_idf_action "Menuconfig" menuconfig
        ;;
    "Reconfigure")
        run_idf_action "Reconfigure" reconfigure
        ;;
    "Clean")
        run_idf_action "Clean build files" clean
        ;;
    "Fullclean")
        if confirm "fullclean deletes the entire build directory. Continue?"; then
            run_idf_action "Fullclean" fullclean
        else
            echo "Cancelled."
        fi
        ;;
    "Erase flash")
        select_port PORT
        select_baud "Flash baud rate" FLASH_BAUD 460800 921600 1500000 2000000
        if confirm "erase-flash will erase the whole chip on $PORT. Continue?"; then
            run_flash_with_target "$PORT" "$FLASH_BAUD" erase-flash
        else
            echo "Cancelled."
        fi
        ;;
    "Encrypted flash")
        select_port PORT
        select_baud "Flash baud rate" FLASH_BAUD 460800 921600 1500000 2000000
        run_build
        run_flash_with_target "$PORT" "$FLASH_BAUD" encrypted-flash
        ;;
    "Merge bin")
        run_build
        run_idf_action "Merge bin" merge-bin
        print_build_artifacts
        ;;
    "UF2")
        run_build
        run_idf_action "UF2" uf2
        print_build_artifacts
        ;;
esac
