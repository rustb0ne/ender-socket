#include "DisplayUI.h"

void DisplayUI::begin() {
    pinMode(TFT_LED_PIN, OUTPUT);
    digitalWrite(TFT_LED_PIN, HIGH);
    _tft.init();
    _tft.setRotation(1);
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextDatum(TL_DATUM);
    _drawStatic();
}

void DisplayUI::toggleDisplay() {
    _displayOn = !_displayOn;
    Serial.printf("💡 Display toggled: %s\n", _displayOn ? "ON" : "OFF");
    digitalWrite(TFT_LED_PIN, _displayOn ? HIGH : LOW);
}

// Receives a full SharedState snapshot copied under mutex by taskNetComms.
// Runs exclusively on Core 0 — safe to call TFT_eSPI (SPI bus on Core 0).
void DisplayUI::update(const SharedState &snap) {
    if (!_displayOn) return;
    const uint32_t now = millis();
    if (now - _lastRefreshMs < DISPLAY_REFRESH_MS) return;
    _lastRefreshMs = now;

    const PowerReading &r = snap.power;
    const SafetyStatus &s = snap.safety;

    // Status dot (top-right): yellow = config mode, green = normal
    const uint32_t dotColor = snap.configMode ? TFT_YELLOW : TFT_GREEN;
    _tft.fillCircle(308, 14, 5, dotColor);

    // Power readings
    _drawField(40,  "Voltage", r.valid ? (String(r.voltage, 1) + " V")   : "--", TFT_WHITE);
    _drawField(58,  "Current", r.valid ? (String(r.current, 3) + " A")   : "--", TFT_WHITE);
    _drawField(76,  "Power",   r.valid ? (String(r.power, 1) + " W")     : "--", TFT_WHITE);
    _drawField(94,  "Energy",  r.valid ? (String(r.energyWh, 1) + " Wh") : "--", TFT_WHITE);

    // Temperature
    const uint32_t tColor = (s.fireRisk || s.overheat) ? TFT_RED : TFT_GREEN;
    _drawField(122, "Temp Socket", isnan(s.tempSocketC) ? "--" : (String(s.tempSocketC, 1) + " C"), tColor);
    _drawField(140, "Temp SSR",    isnan(s.tempSsrC)    ? "--" : (String(s.tempSsrC, 1) + " C"),    tColor);

    // Connectivity
    _drawField(168, "WiFi", snap.wifiConnected ? "CONNECTED" : "DISCONNECTED",
               snap.wifiConnected ? TFT_GREEN : TFT_ORANGE);
    _drawField(186, "MQTT", snap.mqttConnected ? "CONNECTED" : "DISCONNECTED",
               snap.mqttConnected ? TFT_GREEN : TFT_ORANGE);

    // Charge status
    _drawField(204, "SSR",         snap.relayOn   ? "ON"     : "OFF",    snap.relayOn   ? TFT_GREEN : TFT_DARKGREY);
    _drawField(222, "Charge Mode", snap.smartMode  ? "SMART"  : "MANUAL", snap.smartMode ? TFT_CYAN  : TFT_WHITE);
    _drawField(240, "Stop Reason", _stopReasonStr(snap.stopReason), TFT_YELLOW);

    // Alert bar
    _tft.fillRect(0, 268, 320, 24, TFT_BLACK);
    _tft.setTextColor(snap.fireLocked ? TFT_RED : TFT_CYAN, TFT_BLACK);
    _tft.drawString(snap.fireLocked
                        ? "FIRE LOCK ACTIVE: HARD RESET REQUIRED"
                        : snap.lastAlert,
                    4, 274);

    // Button hint
    _tft.fillRect(0, 300, 320, 20, TFT_BLACK);
    _tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    _tft.drawString("BTN1: Tap=reset Wh | Hold 2s=smart mode  BTN2: Tap=screen | Hold 3s=WiFi", 4, 306);
}

void DisplayUI::_drawStatic() {
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(TFT_CYAN, TFT_BLACK);
    _tft.drawString("ENDER SOCKET", 4, 8);
    _tft.drawFastHLine(0, 28, 320, TFT_DARKGREY);
}

void DisplayUI::_drawField(int16_t y, const char *label, const String &value, uint32_t color) {
    _tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    _tft.fillRect(0, y, 320, 16, TFT_BLACK);
    _tft.drawString(label, 4, y);
    _tft.setTextColor(color, TFT_BLACK);
    _tft.drawString(value, 150, y);
}

const char *DisplayUI::_stopReasonStr(ChargeStopReason r) const {
    switch (r) {
        case ChargeStopReason::NONE:    return "NONE";
        case ChargeStopReason::MANUAL:  return "MANUAL";
        case ChargeStopReason::SAFETY:  return "SAFETY";
        case ChargeStopReason::FULL:    return "FULL";
        case ChargeStopReason::UNPLUG:  return "UNPLUG";
        case ChargeStopReason::TIMEOUT: return "TIMEOUT";
        case ChargeStopReason::SPIKE:   return "SPIKE";
        default:                        return "UNKNOWN";
    }
}
