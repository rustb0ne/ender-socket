#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "SharedState.h"   // SharedState, ChargeStopReason (via ChargeManager.h)

// DisplayUI runs exclusively on Core 0 (APP core).
// It receives a SharedState snapshot from taskNetComms and never touches
// ChargeManager or SafetyMonitor directly.
class DisplayUI {
public:
    DisplayUI() = default;

    void begin();
    void update(const SharedState &snap);

    void toggleDisplay();
    bool isDisplayOn() const { return _displayOn; }

private:
    TFT_eSPI _tft{};
    uint32_t _lastRefreshMs = 0;
    bool     _displayOn     = true;

    void        _drawStatic();
    void        _drawField(int16_t y, const char *label, const String &value, uint32_t color = TFT_WHITE);
    const char *_stopReasonStr(ChargeStopReason r) const;
};
