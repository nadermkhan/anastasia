#!/usr/bin/env bash
set -e

# =============================================================================
# Anastasia ESP32-S3 One-Click AOT Compile & Flash Utility (flesp.sh)
# Author: Nader Mahbub Khan
# Usage: ./flesp.sh [input_file.ana] [port]
# Example: ./flesp.sh examples/10_esp32s3_rgb_led.ana /dev/ttyACM0
# =============================================================================

INPUT_ANA="${1:-examples/10_esp32s3_rgb_led.ana}"
PORT="${2:-}"

if [ ! -f "$INPUT_ANA" ]; then
    echo "❌ Error: Source file '$INPUT_ANA' not found!"
    exit 1
fi

echo "======================================================="
echo "⚡ Anastasia ESP32-S3 One-Click AOT Compiler & Flasher"
echo "======================================================="

# 1. Build Anastasia engine if needed
if [ ! -f "build/anastasia_engine" ]; then
    echo "[1/4] Building Anastasia Engine..."
    ./build.sh
fi

BASENAME=$(basename "$INPUT_ANA" .ana)
OBJ_FILE="${BASENAME}_esp32s3.o"
BIN_FILE="${BASENAME}_esp32s3.bin"

# 2. Compile .ana to 32-bit Xtensa LX7 ELF object file
echo "[2/4] Compiling '$INPUT_ANA' -> '$OBJ_FILE' (Xtensa LX7 Target)..."
./build/anastasia_engine --aot "$INPUT_ANA" "$OBJ_FILE" --xtensa

# Detect esptool command variant
ESPTOOL_CMD=""
if command -v esptool >/dev/null 2>&1; then
    ESPTOOL_CMD="esptool"
elif command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL_CMD="esptool.py"
elif python3 -m esptool --version >/dev/null 2>&1; then
    ESPTOOL_CMD="python3 -m esptool"
else
    echo "❌ Error: esptool is not installed! Install via: pip install esptool"
    exit 1
fi

# 3. Convert ELF32 object file to ESP32-S3 flash binary image via esptool
echo "[3/4] Converting '$OBJ_FILE' -> '$BIN_FILE' via $ESPTOOL_CMD..."
$ESPTOOL_CMD --chip esp32s3 elf2image "$OBJ_FILE" -o "$BIN_FILE"

# 4. Auto-detect serial port if not specified
if [ -z "$PORT" ]; then
    echo "🔍 Auto-detecting connected ESP32-S3 serial port..."
    for candidate in /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB0 /dev/ttyUSB1 /dev/cu.usbmodem* /dev/cu.usbserial*; do
        if [ -e "$candidate" ]; then
            PORT="$candidate"
            break
        fi
    done
fi

if [ -z "$PORT" ]; then
    echo "❌ Error: No connected ESP32-S3 serial port detected!"
    echo "   Please specify serial port explicitly, e.g.:"
    echo "   ./flesp.sh $INPUT_ANA /dev/ttyACM0"
    exit 1
fi

echo "[4/4] Flashing '$BIN_FILE' to ESP32-S3 on port $PORT..."
$ESPTOOL_CMD --chip esp32s3 -p "$PORT" -b 921600 write_flash 0x10000 "$BIN_FILE"

echo "======================================================="
echo "🎉 SUCCESS: ESP32-S3 flashed cleanly!"
echo "======================================================="
