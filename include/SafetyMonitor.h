#pragma once
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

struct SafetyStatus {
    float tempSocketC = NAN;
    float tempSsrC = NAN;
    bool overheat = false;
    bool fireRisk = false;
    bool sensorFault = true;
};

class SafetyMonitor {
public:
    void begin();
    void update();

    const SafetyStatus &status() const { return _status; }
    void setPeripheralFault(bool fault) { _peripheralFault = fault; }

    bool shouldForceRelayOff() const { return _status.overheat || _status.fireRisk; }
    bool fireLockRequired() const { return _status.fireRisk; }

private:
    OneWire _oneWire{ONEWIRE_PIN};
    DallasTemperature _ds{&_oneWire};

    SafetyStatus _status{};
    bool _awaitingTemp = false;
    bool _peripheralFault = false;
    uint32_t _tempRequestedMs = 0;
    uint32_t _lastScheduleMs = 0;

    void _updateLedsAndBuzzer();
    bool _isValidTemp(float t) const;
};
