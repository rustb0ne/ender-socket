#include "ChargeManager.h"

void ChargeManager::begin() {
    pinMode(RELAY_PIN, OUTPUT);
    _setRelay(false);
}

void ChargeManager::commandRelay(bool on, bool fireLocked) {
    if (on && fireLocked) {
        _lastStopReason = ChargeStopReason::SAFETY;
        return;
    }

    if (on) {
        _setRelay(true);
        _resetChargeSession();
        _chargeStartMs = millis();
        _lastStopReason = ChargeStopReason::NONE;
    } else {
        forceRelayOff(ChargeStopReason::MANUAL);
    }
}

void ChargeManager::forceRelayOff(ChargeStopReason reason) {
    if (_relayOn) {
        _setRelay(false);
    }
    _lastStopReason = reason;
    _resetChargeSession();
}

void ChargeManager::update(const PowerReading &r, bool fireLocked) {
    if (!_relayOn || fireLocked || !r.valid) {
        _lastCurrent = r.valid ? r.current : 0.0f;
        return;
    }

    const uint32_t now = millis();
    const float current = r.current;

    _pushCurrent(current);

    if ((now - _chargeStartMs) >= CHARGE_TIMEOUT_MS) {
        forceRelayOff(ChargeStopReason::TIMEOUT);
        return;
    }

    if (!_smartMode) {
        _lastCurrent = current;
        return;
    }

    // Unplug detection: abrupt transition from charging current to near-zero current.
    if (_lastCurrent > UNPLUG_FROM_A && current <= UNPLUG_CURRENT_A) {
        _unplugLowCount++;
    } else {
        _unplugLowCount = 0;
    }
    if (_unplugLowCount >= 2) {
        forceRelayOff(ChargeStopReason::UNPLUG);
        return;
    }

    // Full charge detection: stay under trickle threshold for 5 minutes continuously.
    if (current < FULL_CURRENT_A) {
        if (_lowCurrentStartMs == 0) _lowCurrentStartMs = now;
        if (now - _lowCurrentStartMs >= FULL_HOLD_MS) {
            forceRelayOff(ChargeStopReason::FULL);
            return;
        }
    } else {
        _lowCurrentStartMs = 0;
    }

    // Spike detection: current jumps >30% above 5-minute rolling average.
    if (_winCount >= ROLLING_WINDOW_SEC) {
        const float avg = _rollingAvg();
        if (avg > 0.02f && current > (avg * SPIKE_FACTOR)) {
            forceRelayOff(ChargeStopReason::SPIKE);
            return;
        }
    }

    _lastCurrent = current;
}

void ChargeManager::_setRelay(bool on) {
    _relayOn = on;
    digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}

void ChargeManager::_resetChargeSession() {
    _chargeStartMs = 0;
    _lowCurrentStartMs = 0;
    _unplugLowCount = 0;
    _lastCurrent = 0.0f;
    _winCount = 0;
    _winIndex = 0;
    _winSum = 0.0f;
    for (uint16_t i = 0; i < ROLLING_WINDOW_SEC; i++) {
        _window[i] = 0.0f;
    }
}

void ChargeManager::_pushCurrent(float current) {
    if (_winCount < ROLLING_WINDOW_SEC) {
        _window[_winIndex] = current;
        _winSum += current;
        _winCount++;
        _winIndex = (_winIndex + 1U) % ROLLING_WINDOW_SEC;
        return;
    }

    _winSum -= _window[_winIndex];
    _window[_winIndex] = current;
    _winSum += current;
    _winIndex = (_winIndex + 1U) % ROLLING_WINDOW_SEC;
}

float ChargeManager::_rollingAvg() const {
    if (_winCount == 0) return 0.0f;
    return _winSum / (float)_winCount;
}
