#pragma once
#include <Arduino.h>
#include "config.h"

struct PowerReading {
    float voltage = 0.0f;
    float current = 0.0f;
    float power = 0.0f;
    float energyWh = 0.0f;
    bool valid = false;
};

enum class ChargeStopReason : uint8_t {
    NONE,
    MANUAL,
    SAFETY,
    FULL,
    UNPLUG,
    TIMEOUT,
    SPIKE,
};

class ChargeManager {
public:
    void begin();
    void setSmartMode(bool enabled) { _smartMode = enabled; }
    bool smartMode() const { return _smartMode; }
    bool relayOn() const { return _relayOn; }
    ChargeStopReason lastStopReason() const { return _lastStopReason; }

    void commandRelay(bool on, bool fireLocked);
    void forceRelayOff(ChargeStopReason reason);
    void update(const PowerReading &r, bool fireLocked);

private:
    bool _relayOn = false;
    bool _smartMode = true;
    ChargeStopReason _lastStopReason = ChargeStopReason::NONE;
    uint32_t _chargeStartMs = 0;
    uint32_t _lowCurrentStartMs = 0;
    uint8_t _unplugLowCount = 0;
    float _lastCurrent = 0.0f;

    float _window[ROLLING_WINDOW_SEC]{};
    uint16_t _winCount = 0;
    uint16_t _winIndex = 0;
    float _winSum = 0.0f;

    void _setRelay(bool on);
    void _resetChargeSession();
    void _pushCurrent(float current);
    float _rollingAvg() const;
};
