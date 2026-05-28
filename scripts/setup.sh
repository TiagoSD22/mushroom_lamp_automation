#!/usr/bin/env bash
# Environment setup helper for mushroom_light_automation.
# Run from the project root:  ./scripts/setup.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CONFIG_EXAMPLE="$ROOT/include/config_example.h"
CONFIG_FILE="$ROOT/include/config.h"

echo "mushroom_light_automation - environment setup"
echo

# 1. Bootstrap config.h from template
if [[ ! -f "$CONFIG_EXAMPLE" ]]; then
    echo "ERROR: include/config_example.h not found at $CONFIG_EXAMPLE" >&2
    exit 1
fi

if [[ -f "$CONFIG_FILE" ]]; then
    echo "[config] include/config.h already exists - leaving it untouched."
else
    cp "$CONFIG_EXAMPLE" "$CONFIG_FILE"
    echo "[config] Created include/config.h from template."
    echo "         Edit it and fill in your Wi-Fi + Sinric Pro credentials."
fi

# 2. Check for PlatformIO CLI
echo
echo "[tools] Checking for PlatformIO Core CLI..."
if command -v pio >/dev/null 2>&1; then
    echo "        Found: $(command -v pio)"
else
    echo "        Not found on PATH."
    echo "        This is fine if you build through VSCode (PlatformIO IDE extension)."
    echo "        For CLI builds, install via:  pip install --user platformio"
fi

# 3. Check for VSCode
echo
echo "[tools] Checking for VSCode CLI..."
if command -v code >/dev/null 2>&1; then
    echo "        Found: $(command -v code)"
else
    echo "        'code' not found on PATH."
    echo "        Install from https://code.visualstudio.com/ and enable 'Shell Command: Install code in PATH'."
fi

# 4. Linux-only: dialout group check for serial access
if [[ "$(uname -s)" == "Linux" ]]; then
    echo
    echo "[serial] Checking '$USER' is in the dialout group (needed for /dev/ttyUSB*)..."
    if id -nG "$USER" | grep -qw dialout; then
        echo "         OK"
    else
        echo "         WARNING: user '$USER' is NOT in the dialout group."
        echo "         Fix with:  sudo usermod -aG dialout \$USER"
        echo "         Then log out and back in for the change to take effect."
    fi
fi

# 5. macOS-only: hint about driver location
if [[ "$(uname -s)" == "Darwin" ]]; then
    echo
    echo "[serial] On macOS the ESP32 typically appears as /dev/cu.SLAB_USBtoUART"
    echo "         (CP210x) or /dev/cu.usbserial-XXXX (CH340). If it doesn't show up,"
    echo "         install the matching driver — see README section 3.2."
fi

echo
echo "Next steps:"
echo "  1. Open this folder in VSCode (File -> Open Folder...)."
echo "  2. Install the 'PlatformIO IDE' extension if not already installed."
echo "  3. Edit include/config.h with your Wi-Fi + Sinric Pro credentials."
echo "  4. Plug in the ESP32 via USB."
echo "  5. Click the PlatformIO Upload button (or run: pio run -t upload)."
echo "  6. Open Serial Monitor at 115200 baud and try the 'state' command."
echo
