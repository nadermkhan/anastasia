#!/usr/bin/env bash
set -e

# =============================================================================
# Anastasia ESP32-S3 One-Click AOT Compiler, Flasher & Serial Monitor (flesp.sh)
# Author: Nader Mahbub Khan
# Usage: ./flesp.sh [input_file.ana] [port] [baud]
# Example: ./flesp.sh examples/11_esp32s3_hello_world.ana /dev/ttyACM0 115200
# =============================================================================

INPUT_ANA="${1:-examples/11_esp32s3_hello_world.ana}"
PORT="${2:-}"
BAUD="${3:-115200}"

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
BIN_FILE="${BASENAME}_esp32s3.bin"

# 2. Compile .ana directly to native ESP32-S3 flash binary image (with valid 8-bit XOR checksum)
echo "[2/4] Compiling '$INPUT_ANA' -> '$BIN_FILE' (Native ESP32-S3 Binary with checksum)..."
./build/anastasia_engine --aot "$INPUT_ANA" "$BIN_FILE" --esp32s3-bin

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

# 3. Auto-detect serial port if not specified
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

echo "[3/4] Flashing '$BIN_FILE' to ESP32-S3 on port $PORT..."
$ESPTOOL_CMD --chip esp32s3 -p "$PORT" -b 921600 write-flash 0x0 "$BIN_FILE"

echo "======================================================="
echo "🎉 Flashed cleanly to ESP32-S3!"
echo "======================================================="
echo "[4/4] Listening for ESP32-S3 UART serial output on $PORT at $BAUD baud..."
echo "      (Press Ctrl+C to exit serial monitor)"
echo "-------------------------------------------------------"

sleep 0.5

# 4. Launch Serial Monitor to capture ESP32-S3 UART Output
if python3 -m serial.tools.miniterm --version >/dev/null 2>&1; then
    python3 -m serial.tools.miniterm "$PORT" "$BAUD"
elif command -v picocom >/dev/null 2>&1; then
    picocom -b "$BAUD" "$PORT"
elif command -v tio >/dev/null 2>&1; then
    tio -b "$BAUD" "$PORT"
elif command -v minicom >/dev/null 2>&1; then
    minicom -D "$PORT" -b "$BAUD"
else
    # Fallback to bash stty + cat
    stty -F "$PORT" "$BAUD" raw -echo -echoe -echok
    cat "$PORT"
fi
