#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <SinricPro.h>
#include <SinricProLight.h>
#include <esp_system.h>
#include "config.h"

// ---------- Pins ----------
constexpr int PIN_POWER_BTN = 25;
constexpr int PIN_COLOR_BTN = 26;

// ---------- Timing ----------
constexpr uint16_t PRESS_HOLD_MS = 80;
constexpr uint16_t PRESS_GAP_MS  = 200;
constexpr uint16_t LAMP_BOOT_MS  = 600;
constexpr uint32_t WIFI_TIMEOUT_MS = 30000;

// ---------- Color cycle ----------
constexpr int COLOR_WHITE      = 0;
constexpr int COLOR_NEUTRAL    = 1;
constexpr int COLOR_YELLOW     = 2;
constexpr int COLOR_COUNT      = 3;
constexpr int DEFAULT_ON_COLOR = COLOR_YELLOW;

static const char *COLOR_NAMES[] = {"white", "neutral", "yellow"};

// ---------- Persisted state ----------
namespace state {
    Preferences prefs;
    bool lamp_on = false;
    int  color_index = COLOR_WHITE;

    void load() {
        prefs.begin("lamp", false);
        lamp_on = prefs.getBool("on", false);
        color_index = prefs.getInt("color", COLOR_WHITE);
        prefs.end();
    }

    void save() {
        prefs.begin("lamp", false);
        prefs.putBool("on", lamp_on);
        prefs.putInt("color", color_index);
        prefs.end();
    }

    // Called after a hard power loss: lamp came up off and in white.
    void resetAfterPowerLoss() {
        lamp_on = false;
        color_index = COLOR_WHITE;
        save();
    }
}

// ---------- Button pulses ----------
void pressButton(int pin) {
    digitalWrite(pin, HIGH);
    delay(PRESS_HOLD_MS);
    digitalWrite(pin, LOW);
    delay(PRESS_GAP_MS);
}

// ---------- Color control ----------
void stepColorOnce() {
    pressButton(PIN_COLOR_BTN);
    state::color_index = (state::color_index + 1) % COLOR_COUNT;
    state::save();
    Serial.printf("[color] index=%d (%s)\n",
                  state::color_index, COLOR_NAMES[state::color_index]);
}

void walkToColor(int target) {
    target = ((target % COLOR_COUNT) + COLOR_COUNT) % COLOR_COUNT;
    int steps = (target - state::color_index + COLOR_COUNT) % COLOR_COUNT;
    Serial.printf("[color] walk to %s (%d steps)\n", COLOR_NAMES[target], steps);
    for (int i = 0; i < steps; i++) stepColorOnce();
}

// ---------- Power control ----------
// Press power button, mark as on, wait for the lamp's MCU to boot.
// Does NOT change color — used as a building block.
void rawPowerOn() {
    if (state::lamp_on) return;
    pressButton(PIN_POWER_BTN);
    delay(LAMP_BOOT_MS);
    state::lamp_on = true;
    state::save();
    Serial.println("[power] ON");
}

// Public "on" command: turn lamp on AND ensure color is yellow.
void powerOn() {
    rawPowerOn();
    walkToColor(DEFAULT_ON_COLOR);
}

void powerOff() {
    if (!state::lamp_on) return;
    pressButton(PIN_POWER_BTN);
    state::lamp_on = false;
    state::save();
    Serial.println("[power] OFF");
}

// ---------- Sinric Pro callbacks ----------
bool onPowerState(const String & /*deviceId*/, bool &on) {
    on ? powerOn() : powerOff();
    return true;
}

bool onColorTemperature(const String & /*deviceId*/, int &colorTemp) {
    int target;
    if      (colorTemp >= 5000) target = COLOR_WHITE;
    else if (colorTemp >= 3300) target = COLOR_NEUTRAL;
    else                        target = COLOR_YELLOW;
    rawPowerOn();
    walkToColor(target);
    return true;
}

// ---------- Serial command interface ----------
void printState() {
    Serial.printf("[state] on=%d color=%s(%d) wifi=%s\n",
                  state::lamp_on,
                  COLOR_NAMES[state::color_index], state::color_index,
                  WiFi.isConnected() ? "up" : "down");
}

void handleSerial() {
    static String buf;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            buf.trim();
            if (buf.length() == 0) { buf = ""; continue; }
            if      (buf == "on")      powerOn();
            else if (buf == "off")     powerOff();
            else if (buf == "next")    stepColorOnce();
            else if (buf == "white")   { rawPowerOn(); walkToColor(COLOR_WHITE); }
            else if (buf == "neutral") { rawPowerOn(); walkToColor(COLOR_NEUTRAL); }
            else if (buf == "yellow")  { rawPowerOn(); walkToColor(COLOR_YELLOW); }
            else if (buf == "state")   printState();
            else if (buf == "reset") {
                state::resetAfterPowerLoss();
                Serial.println("[nvs] cleared (off / white)");
            }
            else Serial.printf("[serial] unknown: '%s'\n", buf.c_str());
            buf = "";
        } else if (c >= 32 && c < 127) {
            buf += c;
        }
    }
}

// ---------- Boot helpers ----------
void initGpios() {
    // Pre-set the output latch LOW *before* enabling the driver, then assert
    // again after, so the pin can never glitch HIGH during boot.
    digitalWrite(PIN_POWER_BTN, LOW);
    digitalWrite(PIN_COLOR_BTN, LOW);
    pinMode(PIN_POWER_BTN, OUTPUT);
    pinMode(PIN_COLOR_BTN, OUTPUT);
    digitalWrite(PIN_POWER_BTN, LOW);
    digitalWrite(PIN_COLOR_BTN, LOW);
}

void handleResetReason() {
    esp_reset_reason_t r = esp_reset_reason();
    bool power_loss = (r == ESP_RST_POWERON) || (r == ESP_RST_BROWNOUT);
    Serial.printf("[boot] reset_reason=%d (%s)\n",
                  r, power_loss ? "power-loss" : "soft");
    if (power_loss) state::resetAfterPowerLoss();
}

void setupWiFi() {
    Serial.printf("[wifi] connecting to '%s'", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print(".");
    }
    if (WiFi.isConnected()) {
        Serial.printf("\n[wifi] connected ip=%s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[wifi] timeout — will keep retrying in background");
    }
}

void setupSinric() {
    SinricProLight &light = SinricPro[SINRIC_DEVICE_ID];
    light.onPowerState(onPowerState);
    light.onColorTemperature(onColorTemperature);
    SinricPro.begin(SINRIC_APP_KEY, SINRIC_APP_SECRET);
}

// ---------- Arduino entry points ----------
void setup() {
    initGpios();
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== mushroom_light_automation ===");

    state::load();
    handleResetReason();
    printState();

    setupWiFi();
    setupSinric();

    Serial.println("[serial] commands: on, off, white, neutral, yellow, next, state, reset");
}

void loop() {
    SinricPro.handle();
    handleSerial();
}
