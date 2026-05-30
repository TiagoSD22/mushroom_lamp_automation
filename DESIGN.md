# Mushroom Light Automation — Design Document

## 1. Overview

Automate a 5 V USB mushroom lamp so it can be turned on/off and have its color tone changed by voice through Amazon Alexa. The lamp's existing physical buttons stay intact; the system works by electrically simulating button presses from an ESP32 microcontroller. The whole rig is powered from a portable powerbank so it can sit anywhere without being tied to a wall outlet.

Typical usage window: **6 PM – 11 PM**, ambient/accent lighting.

## 2. Goals & Non-Goals

### Goals
- Voice control via Alexa (on/off, pick color tone).
- Preserve the lamp's normal manual operation — physical buttons still work.
- Run on battery (powerbank) without needing AC power.
- No invasive modifications to the lamp's internal logic; only solder wires in parallel with existing buttons.

### Non-Goals
- Brightness control (the lamp has no brightness button).
- Hard-cutting the lamp's power supply (analysis showed it isn't worth the complexity — see §5).
- Local-only operation; this design depends on Wi-Fi + Sinric Pro cloud.

## 3. Hardware Inventory

| Component | Spec | Role |
|---|---|---|
| Powerbank | 100 W, 60,000 mAh, USB 5 V output | Energy source |
| ESP32 dev board | Any classic ESP32 with Wi-Fi | Controller |
| Mushroom lamp | 5 V USB, 2 buttons (power, color), 3 color tones | Load |
| BC547 NPN BJT × 2 | Vce ~45 V, Ic max 100 mA | Button bridge transistors |
| 1 kΩ resistors × 2 | — | Base current limiting |
| 300 Ω resistors | — | Spares (not used in final design) |
| **USB Y-splitter / hub** | 1× USB-A male to 2× USB-A female (power only is fine) | Splits a single powerbank port to feed ESP32 + lamp together — see §5.4 |
| Wires | — | Interconnect |

## 4. System Architecture

```
                ┌────────────────────────────────┐
                │   Powerbank 5 V USB output     │
                │   (single port; see §5.4)      │
                └────────────────┬───────────────┘
                                 │ 5 V
                                 ▼
                ┌────────────────────────────────┐
                │     USB Y-splitter / hub       │
                │  (combines load so total       │
                │   draw stays above the         │
                │   powerbank's auto-shutoff     │
                │   threshold)                   │
                └──────┬──────────────────┬──────┘
                       │ 5 V              │ 5 V
                       ▼                  ▼
                ┌────────────┐     ┌──────────────────┐
                │   ESP32    │     │  Mushroom lamp   │
                │  (Wi-Fi)   │     │   (5 V USB)      │
                │            │     │                  │
                │  GPIO 25 ──┼──┐  │  [Power button]  │
                │  GPIO 26 ──┼─┐│  │  [Color button]  │
                │            │ ││  │                  │
                │   GND   ───┼─┼┼──┼─── GND  (shared) │
                └────────────┘ ││  └──────────────────┘
                               ││
                       ┌───────┘└────────┐
                       │                 │
                ┌──────▼──────┐   ┌──────▼──────┐
                │   Q1 BC547  │   │  Q2 BC547   │
                │  power btn  │   │  color btn  │
                │  bridge     │   │  bridge     │
                └─────────────┘   └─────────────┘
                  (wired across the lamp's button pads)


Control flow:
   Alexa voice ──► Sinric Pro cloud ──► MQTT/WebSocket ──► ESP32 (Wi-Fi)
                                                            │
                                                            ▼
                                                     pulse GPIO 25/26
                                                            │
                                                            ▼
                                                BC547 shorts button pads
                                                            │
                                                            ▼
                                                     Lamp reacts
```

## 5. Key Design Decisions

### 5.1 Do NOT cut the lamp's power supply
Originally proposed to save energy when the lamp is off. Rejected because:

- **Energy budget is dominated by the ESP32**, not the lamp. ESP32 with Wi-Fi active draws ~80 mA; the lamp's standby draw is 5–30 mA. Cutting the lamp saves <10% of the system's idle current.
- **High-side switching needs a P-MOSFET**, which is not in the parts inventory.
- **Low-side switching creates a leakage path**: with the button-bridge transistors still wired, the lamp's MCU pull-up can sneak current through the bridge to ESP32 GND, partially powering or stressing the lamp's MCU.
- **Bonus benefit:** the lamp retains its last-selected color, simplifying state tracking.

### 5.2 Button simulation via BC547 low-side bridge
Each BC547 sits across one of the lamp's existing buttons. When the ESP32 GPIO goes HIGH, the transistor saturates and shorts the button pads — electrically equivalent to a finger press.

- Base drive: `Ib = (3.3 V − 0.7 V) / 1 kΩ ≈ 2.6 mA` → deep saturation for the microamp-scale button signal.
- Pulse width: 250 ms HIGH, then 200 ms gap before the next press. *(80 ms was tried first but was below the lamp MCU's debounce threshold; 250 ms registers reliably on this specific lamp. Tune `PRESS_HOLD_MS` in `main.cpp` if a different lamp model needs longer or shorter.)*

### 5.3 GPIO selection
Use **GPIO 25 (power)** and **GPIO 26 (color)**. Both are regular outputs, not strapping pins. Both must be initialized LOW in `setup()` *before* enabling the pin as OUTPUT to prevent a spurious press during ESP32 boot.

### 5.4 Control surface: Sinric Pro
Register **one Light device** in Sinric Pro with on/off + color temperature support. Map the 3 physical tones to color-temperature buckets:

| Lamp color | Index | Sinric color temp |
|---|---|---|
| White | 0 | 6500 K |
| Neutral | 1 | 4000 K |
| Yellow | 2 | 2700 K |

ESP32 receives a color-temp event from Sinric, computes the shortest **forward** path on the cycle, and pulses the color button that many times.

### 5.5 State persistence
Persist `lamp_on` (bool) and `color_index` (0–2) in ESP32 NVS via the `Preferences` library. Since lamp power is never cut, NVS state and physical lamp state stay in sync across reboots. First-time seed: assume index 0 (white) only if NVS is empty.

### 5.6 Power distribution — single port via USB Y-splitter
**Both the ESP32 and the lamp must share the SAME powerbank USB port via a Y-splitter, not two separate ports.**

Reason: powerbanks watch each output port's current draw independently and shut the port off when the load falls below a threshold (typically 50–100 mA, sustained for 30–60 seconds). The original design assumed the ESP32's continuous Wi-Fi draw (~80 mA) would carry the whole system, but in reality:

- ESP32 on its own port → ~80 mA → stays alive ✓
- Lamp on its own port (standby) → ~5–10 mA → **port shuts off** after ~1 minute, lamp becomes unreachable until the powerbank is physically prodded back to life

By combining both devices on one port through a Y-splitter, the **summed** draw never drops below ~90 mA, so the port stays awake even when the lamp is "off" most of the day.

Side benefits:
- Common GND is guaranteed by the splitter's shared shell — no chance of a missing ground jumper between ESP32 and lamp.
- Only one cable to the powerbank → cleaner mechanically.

Caveat: cheap Y-splitters sometimes have thin internal wire (24 AWG or worse). For continuous ~150 mA draw (lamp on + ESP32) this is fine, but avoid anything that feels suspiciously light.

## 6. Schematic

### 6.1 Wiring diagram

```
                                                            +5V
                                                             │
                                                       ┌─────┴─────┐
                                                       │           │
                                                       │  Lamp     │
                                                       │  MCU      │
                                                       │  (with    │
                                                       │  internal │
                                                       │  pull-ups)│
                                                       │           │
                                                       └─┬───────┬─┘
                                                         │       │
                                                         │ SIG_P │ SIG_C
                                                         │       │
   ESP32                                                 │       │
  ┌──────────┐                                           │       │
  │          │                                           │       │
  │  GPIO 25 ├──[1kΩ]──┬─B                           ┌───┴───┐   │
  │          │         │                             │ btn P │   │
  │          │         │ Q1 BC547                    └───┬───┘   │
  │          │         C ───────────────────────────────►│ pad   │
  │          │         │                                 │       │
  │          │         E ─────────────────────────────►──┘ GND   │
  │          │         │                                 │ pad   │
  │  GPIO 26 ├──[1kΩ]──┼─B                               │       │
  │          │         │                                 │   ┌───┴───┐
  │          │         │ Q2 BC547                        │   │ btn C │
  │          │         C ─────────────────────────────────►─┤ pad   │
  │          │         │                                 │   └───┬───┘
  │          │         E ─────────────────────────────────►──┐   │
  │          │         │                                     │ GND
  │   GND    ├────────────────────────────────────────────┬──┴───┘
  │          │                                            │
  │   VIN/5V ├──────────────────────────────────────────┐ │
  │          │                                          │ │
  └──────────┘                                          │ │
                                                        │ │
                  Powerbank USB 5V ─────────────────────┴─┘
                  Powerbank GND  ───────────────────────┘
```

Legend:
- **SIG_P / SIG_C** = the side of each button that the lamp's MCU pulls high through an internal pull-up. To be identified with a multimeter before soldering.
- The opposite side of each button is **GND** (lamp's GND, shared with ESP32 via the powerbank).
- Q1, Q2 collectors connect to the SIG side; emitters connect to the GND side. When ESP32 drives the base HIGH, the transistor saturates and pulls SIG down — same as pressing the button.

### 6.2 Per-button bridge (detail)

```
                  +V (lamp pull-up)
                       │
                       │
                  ┌────┴────┐
                  │ button  │   ← existing physical button, untouched
                  │ pads    │
                  └────┬────┘
                       │
                       ●  SIG side (to Q collector)
                       │
            ───────────┼───────── solder wire tap
                       │
                       │
            ESP32 GPIO ─[1kΩ]─ B
                              │
                              │   Q (BC547, NPN)
                              C ─► to SIG pad above
                              E ─► to GND pad below
                              │
                       ●  GND side (to Q emitter)
                       │
            ───────────┼───────── solder wire tap
                       │
                  ┌────┴────┐
                  │ button  │
                  │ pads    │
                  └────┬────┘
                       │
                       │
                       └─── lamp GND
```

## 7. Firmware Architecture

### 7.1 Modules

| Module | Responsibility |
|---|---|
| `wifi_manager` | Connect to Wi-Fi, reconnect on drop |
| `sinric_handler` | Register device, handle on/off + color-temp callbacks |
| `lamp_control` | `setLampOn(bool)`, `setColor(int)`, `pressButton(pin, ms)` |
| `state_store` | Load/save `lamp_on` and `color_index` from NVS |

### 7.2 Pseudocode

```cpp
const int PIN_POWER_BTN = 25;
const int PIN_COLOR_BTN = 26;
const int COLOR_COUNT   = 3;

bool lamp_on;       // persisted
int  color_index;   // 0=white, 1=neutral, 2=yellow; persisted

void pressButton(int pin, int hold_ms = 80) {
    digitalWrite(pin, HIGH);
    delay(hold_ms);
    digitalWrite(pin, LOW);
    delay(120);
}

void setLampOn(bool on) {
    if (on == lamp_on) return;
    pressButton(PIN_POWER_BTN);
    lamp_on = on;
    saveState();
}

void setColor(int target) {
    if (!lamp_on) setLampOn(true);
    int steps = (target - color_index + COLOR_COUNT) % COLOR_COUNT;
    for (int i = 0; i < steps; i++) {
        pressButton(PIN_COLOR_BTN);
        color_index = (color_index + 1) % COLOR_COUNT;
        saveState();
    }
}
```

### 7.3 Sinric Pro callback mapping

```
onPowerState(bool state) ─► setLampOn(state)

onColorTemperature(int kelvin) ─►
    if (kelvin >= 5500)   setColor(0);   // white
    else if (kelvin >= 3500) setColor(1); // neutral
    else                  setColor(2);    // yellow
```

## 8. Energy Budget

Both devices feed off a single powerbank port via the Y-splitter (see §5.6).

| Source | Current @ 5V | Daily energy (mAh) |
|---|---|---|
| ESP32 (Wi-Fi idle, continuous) | ~80 mA | ~1920 mAh |
| Lamp standby (off) | ~10 mA | ~190 mAh (19 hrs off) |
| Lamp active (on) | ~150 mA | ~750 mAh (5 hrs on) |
| **Total / day** |  | **~2860 mAh** |

Powerbank capacity: 60,000 mAh @ 3.7 V cell → ~44,400 mAh @ 5 V (after boost losses ~85%).
**Estimated autonomy: ~15 days per full charge.**

**Combined draw on the active USB port** stays between ~90 mA (lamp off + ESP32 idle) and ~230 mA (lamp on + ESP32 idle), comfortably above the typical 50–100 mA auto-shutoff threshold.

## 9. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Spurious button press during ESP32 boot | Initialize GPIO LOW *before* setting as OUTPUT; verify with scope on first power-up |
| Powerbank auto-shutoff on low load | **Combine ESP32 + lamp on one USB port via a Y-splitter** (§5.6). Each port has its own threshold — the lamp's standby draw alone (~10 mA) will let its port shut off within a minute. Sharing the port with the ESP32 keeps total draw ≥ 90 mA always. |
| Wi-Fi disconnect → loss of voice control | Sinric Pro library handles reconnect; state survives in NVS |
| Lamp's MCU latch-up via button pad | Mitigated by NOT cutting lamp power (always-on shared rail) |
| Wrong button pad identified as GND | Use multimeter continuity to lamp GND before soldering; transistor in reverse mode would fail to bridge cleanly — easy to detect during bring-up |

## 10. Bring-Up Plan

1. **Identify button pads** on lamp PCB — find SIG and GND side of each button with a multimeter.
2. **Breadboard the two transistor bridges**, jumper to ESP32 GPIO 25/26.
3. **Flash a smoke-test sketch** that pulses each GPIO every few seconds and observe the lamp respond.
4. **Verify color cycle** matches white → neutral → yellow.
5. **Layer in Sinric Pro** and test from Alexa.
6. **Add NVS persistence** and verify state survives ESP32 reboot.
7. **Solder permanent connections** to lamp button pads.
8. **Final packaging** (enclosure / cable management).

## 11. Open Questions

- Confirm which pad on each lamp button is SIG vs GND (resolved at bring-up step 1).
- Sinric Pro account setup and device credentials (user side).
- Optional: should a second Sinric device expose a raw "next color" toggle, for cases where Alexa's color-temp parsing misfires?
