# Mushroom Light Automation — Firmware

Voice-controlled automation for a 5 V USB mushroom lamp, built on an ESP32 and integrated with Alexa through **Sinric Pro**. See [DESIGN.md](DESIGN.md) for the full architecture, schematic, and rationale. This README focuses on the firmware: what it does, how to set up the toolchain, how to build/flash it, and how to validate it on the bench.

---

## 1. Behavior

| Event | What firmware does |
|---|---|
| **Turn on** (Alexa or serial `on`) | Press power button → always walk color to **yellow** (preferred default) |
| **Turn off** (Alexa or serial `off`) | Press power button (lamp turns off via its built-in toggle) |
| **Set color** (Alexa color-temp command, or serial `white` / `neutral` / `yellow`) | If lamp is off, turn it on (without forcing yellow); then walk color to target |
| **Step color** (serial `next`) | Press color button once — useful when ESP32's tracked state drifted out of sync with the lamp |
| **Power loss** (powerbank disconnected/empty) | On next ESP32 boot, detected via `esp_reset_reason()`; firmware resets its tracked state to `off / white` to match the lamp's behavior after a cold start |
| **Soft reset** (Wi-Fi blip, watchdog, OTA) | Tracked state from NVS is trusted, since the lamp didn't lose power |

### Color cycle (one-direction, wraps around)

`white (0) → neutral (1) → yellow (2) → white (0) → …`

### Why "always start in yellow" works after power loss

After a power loss, the lamp itself boots in white. The ESP32 resets its NVS-tracked color to `white`. When the "on" command arrives, it presses power (lamp lights up in white), then walks 2 steps to yellow. The lamp ends in yellow regardless of whether it just lost power or not.

---

## 2. Project structure

```
mushroom_light_automation/
├── DESIGN.md                    # Hardware design + schematic
├── README.md                    # This file
├── platformio.ini               # PlatformIO build config
├── .gitignore
├── src/
│   └── main.cpp                 # All firmware logic
├── include/
│   ├── config_example.h         # Template for Wi-Fi + Sinric credentials
│   └── config.h                 # YOUR credentials (gitignored, created by setup)
└── scripts/
    └── setup.sh                 # One-shot environment helper (bash)
```

---

## 3. Prerequisites

### 3.1 Tools

| Tool | Why | Where |
|---|---|---|
| **VS Code** | IDE | https://code.visualstudio.com/ |
| **PlatformIO IDE** (VS Code extension) | ESP32 toolchain, build, flash, serial monitor | Install from VS Code Extensions: search "PlatformIO IDE" |
| **C/C++** (Microsoft, VS Code extension) | IntelliSense, code navigation | Usually auto-installed with PlatformIO |
| **USB-UART driver** | Lets the OS see the ESP32's USB-to-serial chip | See §3.2 |

### 3.2 USB-UART driver and serial access

Most ESP32 dev boards expose their UART through a USB-to-serial chip. Identify yours from the markings on the board:

- **CP2102 / CP2104** (Silicon Labs) — common on NodeMCU-32S, ESP32 DevKit-V1.
- **CH340 / CH341 / CH9102** (WCH) — common on cheap clones.

**Linux:** drivers ship in the mainline kernel; no install needed. After plugging in, the ESP32 appears as `/dev/ttyUSB0` (CH340), `/dev/ttyACM0` (CP210x or CH9102 on recent kernels). Confirm with `ls /dev/tty{USB,ACM}*` or `sudo dmesg | tail`.

Two more one-time steps are needed on Linux before PlatformIO can open the port:

**1) Install PlatformIO's udev rules** (without these, the port shows up but PlatformIO can't open it):

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

**2) Add yourself to the serial group** (`dialout` on Debian/Ubuntu, `uucp` on Arch):

```bash
sudo usermod -aG dialout $USER     # Debian/Ubuntu
# or
sudo usermod -aG uucp $USER        # Arch
```

You need a fresh shell session for the group change to take effect. **On WSL, closing the terminal isn't enough — see §3.3.**

**macOS:** drivers are *not* bundled. Install the right one:

- CP210x: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers (universal binary)
- CH340/CH9102: https://github.com/WCHSoftGroup/ch34xser_macos

After install, the ESP32 appears as `/dev/cu.SLAB_USBtoUART` (CP210x) or `/dev/cu.usbserial-XXXX` (CH340/CH9102). PlatformIO auto-detects it.

### 3.3 WSL2 — bridging USB from Windows (skip if not on WSL)

WSL2 doesn't expose USB devices to Linux by default; Windows owns the USB stack. Use [**usbipd-win**](https://github.com/dorssel/usbipd-win) to bridge the ESP32 into WSL.

#### One-time setup

1. **Install usbipd-win** on the Windows side. From Windows PowerShell:

   ```powershell
   winget install --source winget --interactive --exact dorssel.usbipd-win
   ```

   If winget fails with a Microsoft Store source / certificate error, download the MSI directly from https://github.com/dorssel/usbipd-win/releases/latest and double-click it. Close and reopen PowerShell after install so `usbipd` is on PATH.

2. **Identify the ESP32.** In any Windows PowerShell:

   ```powershell
   usbipd list
   ```

   Look for the serial device whose VID:PID matches your chip — CP2102 (`10c4:ea60`), CH340 (`1a86:7523`), or CH9102 (`1a86:55d4`). Note its **BUSID** (e.g., `3-3`).

3. **Bind it** (persistent across reboots). In **Windows PowerShell as Administrator**:

   ```powershell
   usbipd bind --busid 3-3
   ```

   `STATE` should change from `Not shared` to `Shared`.

#### Per-session steps

Each time you boot Windows or run `wsl --shutdown`, re-attach the device. From Windows PowerShell (no admin needed):

```powershell
usbipd attach --wsl --busid 3-3
```

Then in WSL:

```bash
ls /dev/tty{USB,ACM}* 2>/dev/null
```

#### Applying `dialout` membership on WSL

`sudo usermod -aG dialout $USER` doesn't take effect by reopening the terminal alone — WSL keeps the old credentials in its init process. Restart the whole WSL VM from Windows PowerShell:

```powershell
wsl --shutdown
```

Then re-run `usbipd attach --wsl --busid 3-3` and open a fresh WSL terminal. `groups | grep dialout` should now list `dialout`.

### 3.4 Sinric Pro account

1. Sign up at https://sinric.pro/.
2. From the dashboard, create a new **Light** device.
3. Note the **App Key**, **App Secret**, and **Device ID** — they go in `include/config.h`.
4. In the Alexa app, enable the **Sinric Pro** skill and discover devices.

---

## 4. Setup

From a terminal in this folder:

```bash
chmod +x scripts/setup.sh    # only needed the first time
./scripts/setup.sh
```

The script copies `include/config_example.h` to `include/config.h` (if missing), checks for the PlatformIO and VSCode CLIs, and on Linux verifies you're in the `dialout` group. Open the new `include/config.h` and fill in:

```c
#define WIFI_SSID         "your-wifi-ssid"
#define WIFI_PASS         "your-wifi-password"
#define SINRIC_APP_KEY    "from-sinric-pro-dashboard"
#define SINRIC_APP_SECRET "from-sinric-pro-dashboard"
#define SINRIC_DEVICE_ID  "from-sinric-pro-dashboard"
```

`config.h` is git-ignored so credentials don't leak.

---

## 5. Build & flash

### Via VS Code (PlatformIO IDE)

1. Open this folder in VS Code (`File → Open Folder…`).
2. Wait for PlatformIO to initialize (status bar at bottom).
3. Click the **✓ (Build)** icon in the PlatformIO bar — or `Ctrl+Alt+B`.
4. Plug in the ESP32 via USB.
5. Click the **→ (Upload)** icon — or `Ctrl+Alt+U`.
6. Click the **🔌 (Serial Monitor)** icon — or `Ctrl+Alt+S`. Baud rate is auto-configured to 115200.

### Via CLI

```bash
pio run                  # build
pio run -t upload        # build + flash
pio device monitor       # serial monitor
```

If `pio` isn't on your PATH, install it with `pip install --user platformio` and add `~/.local/bin` to PATH (Linux) or `~/Library/Python/3.x/bin` (macOS).

---

## 6. Wiring (recap)

Refer to [DESIGN.md §6 (Schematic)](DESIGN.md) for the full diagram. Quick reference:

| ESP32 pin | → | Through | → | To |
|---|---|---|---|---|
| GPIO 25 | → | 1 kΩ | → | Base of Q1 (BC547), C/E across **power button** pads |
| GPIO 26 | → | 1 kΩ | → | Base of Q2 (BC547), C/E across **color button** pads |
| GND | → | — | → | Lamp GND (shared via USB Y-splitter) |
| 5V / VIN | → | — | → | Powerbank USB 5V (via Y-splitter — see below) |

**Power delivery — important:** plug both the ESP32 and the lamp into the **same** powerbank port using a USB Y-splitter, not two separate ports. Powerbanks auto-shut off each port when its load drops below ~50–100 mA, and the lamp's standby draw alone is too low to keep its port awake. Sharing the port keeps total draw above the threshold whether the lamp is on or off. See [DESIGN.md §5.6](DESIGN.md) for the full rationale.

**Before soldering:** identify which side of each lamp button is GND (multimeter continuity to lamp USB shield) and which is signal — connect emitter to GND side, collector to signal side.

---

## 7. Validation

### 7.1 Serial commands (bench bring-up)

With the serial monitor open at 115200 baud, the firmware accepts these commands (newline-terminated):

| Command | Effect |
|---|---|
| `on` | Turn lamp on, walk to yellow |
| `off` | Turn lamp off |
| `white` | Walk color to white |
| `neutral` | Walk color to neutral |
| `yellow` | Walk color to yellow |
| `next` | Press color button once (debug / resync) |
| `state` | Print current tracked state + Wi-Fi status |
| `reset` | Clear NVS state (use after a manual power-cycle of the lamp) |

### 7.2 Validation checklist

Work through these in order on the bench:

1. **Boot sanity** — flash firmware, open serial monitor. Verify the boot banner prints reset reason and state. No spurious button presses on the lamp during ESP32 boot.
2. **Power toggle** — type `on`. Lamp should turn on and end at yellow (after 0–2 color presses depending on starting state). Type `off`. Lamp should turn off.
3. **Color step** — type `next` while the lamp is on. Lamp should advance one tone. Repeat and observe the cycle: white → neutral → yellow → white.
4. **Direct color** — type `white`, then `yellow`. The firmware should compute the right number of presses and arrive at the target.
5. **Cold-start handling** — disconnect the powerbank fully, wait 5 s, reconnect. Verify:
   - Serial banner prints `reset_reason=…(power-loss)`.
   - Tracked state is reset to `off / white`.
   - Lamp is observably off and (once turned on) starts in white.
6. **Soft reset preservation** — turn lamp on (yellow), then press the ESP32's `EN`/`RST` button only (lamp stays powered). Verify:
   - Serial banner prints `(soft)`.
   - Tracked state is preserved (`on / yellow`).
7. **Wi-Fi + Sinric** — verify serial shows `[wifi] connected`. In the Sinric Pro dashboard, your device should show **online**.
8. **Alexa** — say *"Alexa, turn on \<your light name\>"*. Lamp lights up and walks to yellow. Try *"Alexa, set \<your light name\> to warm white"* — lamp walks to yellow. Try *"Alexa, set \<your light name\> to cool white"* — lamp walks to white. *"Alexa, turn off …"* — lamp turns off.

### 7.3 Color-temperature mapping (Alexa)

| Alexa says | Sinric sends (K) | Firmware picks |
|---|---|---|
| "cool white" / "daylight" | ≥ 5000 | white |
| "soft white" / "neutral" | 3300–4999 | neutral |
| "warm white" / "incandescent" | < 3300 | yellow |

---

## 8. Troubleshooting

### 8.1 Hardware / firmware

| Symptom | Likely cause | Fix |
|---|---|---|
| Lamp twitches on ESP32 boot | GPIO not driven LOW before output enable | Already handled in `initGpios()` — check the GPIO pins aren't swapped with strapping pins (avoid GPIO 0, 2, 5, 12, 15) |
| Lamp doesn't respond to button-sim | Emitter/collector swapped, or button GND side misidentified | Re-check with multimeter; BC547 in reverse mode barely conducts |
| Color out of sync with tracked state | Someone pressed the lamp's physical button | `reset` over serial, then type `on` to re-synchronize to yellow |
| Wi-Fi won't connect | SSID/password typo, or 5 GHz-only network | ESP32 (classic) is 2.4 GHz only — check router band |
| Sinric device shows offline | App key/secret/device ID mismatch | Re-copy from the Sinric Pro dashboard into `include/config.h` |
| Alexa says "device isn't responding" | Sinric device not yet discovered | In Alexa app: *Devices → + → Add Device → Other → Discover devices* |
| Lamp becomes unresponsive after sitting idle (off) for a while, until you replug or press the powerbank's button | Powerbank shut off the lamp's USB port because standby draw was below its threshold | **Plug ESP32 + lamp into the SAME powerbank port via a USB Y-splitter.** The combined draw (~90 mA min) keeps the port active. Each port has its own threshold — running them on separate ports doesn't help, even though the ESP32 alone would keep its own port awake. |
| Whole rig dies (ESP32 too) after long idle | Some powerbanks shut down all outputs when total draw is low | Same fix as above (Y-splitter) — combined draw is what triggers per-port AND overall keep-alive logic |
| Lamp doesn't react to press despite ESP32 firing GPIO | Press too short for lamp MCU's debounce | Increase `PRESS_HOLD_MS` in `src/main.cpp` (the default is 250 ms; some lamps need 300–400 ms) |

### 8.2 Toolchain / OS

| Symptom | Likely cause | Fix |
|---|---|---|
| `winget install … usbipd-win` fails with `msstore` certificate error (`0x8a15005e`) | Microsoft Store source has a TLS issue on this machine | Re-run with `--source winget`, or download the MSI directly from https://github.com/dorssel/usbipd-win/releases/latest |
| `usbipd: command not recognized` right after install | PowerShell session was started before install, PATH not refreshed | Close and reopen PowerShell, or refresh PATH in-session: `$env:Path = [Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [Environment]::GetEnvironmentVariable("Path","User")` |
| `usbipd attach` succeeds but `/dev/ttyUSB*` and `/dev/ttyACM*` don't appear in WSL | WSL kernel didn't enumerate the device, or stale dmesg | Run `sudo dmesg | tail -30`; if nothing serial-related, run `wsl --shutdown` then re-attach |
| `Could not open /dev/ttyACM0 … [Errno 13] Permission denied` during `pio run -t upload` | Missing PlatformIO udev rules and/or user not in `dialout` group | Install udev rules (see §3.2 step 1), `sudo usermod -aG dialout $USER`, then `wsl --shutdown` on WSL or log out/in on bare Linux; re-attach USB and retry |
| `Warning! Please install 99-platformio-udev.rules` printed during build | PlatformIO udev rules absent | Run the curl + udevadm commands from §3.2 step 1 |
| Port disappears from WSL after `wsl --shutdown` | Expected — shutdown detaches all USB devices | Re-run `usbipd attach --wsl --busid <id>` from Windows PowerShell after WSL is back up |

---

## 9. Implementation notes

- **GPIO init order matters.** `digitalWrite(LOW)` runs *before* `pinMode(OUTPUT)` to set the output latch low before the driver is enabled, preventing a brief HIGH glitch during boot.
- **NVS namespace** is `lamp`. Two keys: `on` (bool), `color` (int).
- **Power-loss detection** uses `esp_reset_reason()` — only `ESP_RST_POWERON` and `ESP_RST_BROWNOUT` trigger the reset-to-white logic. All other reset causes trust NVS.
- **Press timing**: 80 ms HIGH, 200 ms gap. These work reliably for most cheap lamps; tune in `main.cpp` if your specific lamp needs longer presses.
- **Lamp boot delay**: after pressing the power button, the firmware waits 600 ms before sending color-step presses, giving the lamp's MCU time to register the power state.
