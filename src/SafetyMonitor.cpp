#include "SafetyMonitor.h"

void SafetyMonitor::begin() {
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    _ds.begin();
    _ds.setWaitForConversion(false);
    _ds.requestTemperatures();
    _tempRequestedMs = millis();
    _awaitingTemp = true;
}

void SafetyMonitor::update() {
    const uint32_t now = millis();

    if (!_awaitingTemp && now - _lastScheduleMs >= SENSOR_READ_MS) {
        _ds.requestTemperatures();
        _tempRequestedMs = now;
        _lastScheduleMs = now;
        _awaitingTemp = true;
    }

    if (_awaitingTemp && (now - _tempRequestedMs) >= 800UL) {
        const float t0 = _ds.getTempCByIndex(0);
        const float t1 = _ds.getTempCByIndex(1);

        _status.tempSocketC = t0;
        _status.tempSsrC = t1;

        const bool t0Ok = _isValidTemp(t0);
        const bool t1Ok = _isValidTemp(t1);
        _status.sensorFault = (!t0Ok || !t1Ok);

        float maxT = -127.0f;
        if (t0Ok && t0 > maxT) maxT = t0;
        if (t1Ok && t1 > maxT) maxT = t1;

        _status.overheat = (! _status.sensorFault) && (maxT > TEMP_ALERT_C);
        _status.fireRisk = (! _status.sensorFault) && (maxT > TEMP_FIRE_C);

        _awaitingTemp = false;
    }

    _updateLedsAndBuzzer();
}

void SafetyMonitor::_updateLedsAndBuzzer() {
    const bool hasFault = _status.sensorFault || _peripheralFault;
    const bool redOn = hasFault || _status.overheat || _status.fireRisk;

    digitalWrite(LED_RED_PIN, redOn ? HIGH : LOW);
    digitalWrite(BUZZER_PIN, _status.fireRisk ? HIGH : LOW);
}

bool SafetyMonitor::_isValidTemp(float t) const {
    if (t == DEVICE_DISCONNECTED_C) return false;
    return (t > -40.0f && t < 125.0f);
}
