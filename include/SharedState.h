#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "ChargeManager.h"   // PowerReading, ChargeStopReason
#include "SafetyMonitor.h"   // SafetyStatus

// ─────────────────────────────────────────────────────────────────────────────
//  Alert message — fixed-size to avoid heap allocation inside queues
// ─────────────────────────────────────────────────────────────────────────────
#define ALERT_TEXT_MAX 96

struct AlertMessage {
    char text[ALERT_TEXT_MAX];
};

// ─────────────────────────────────────────────────────────────────────────────
//  Command sent from Core 0 → Core 1 via gCommandQueue
// ─────────────────────────────────────────────────────────────────────────────
enum class CmdType : uint8_t {
    RELAY,         ///< value: true = ON, false = OFF
    SMART_MODE,    ///< value: true = enable, false = disable
    RESET_ENERGY,  ///< value: ignored
};

struct RelayCommand {
    CmdType type  = CmdType::RELAY;
    bool    value = false;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Shared state snapshot
//  Written by Core 1 (taskChargeSafety), read by Core 0 (taskNetComms).
//  Always access under gStateMutex.
// ─────────────────────────────────────────────────────────────────────────────
struct SharedState {
    PowerReading     power{};
    SafetyStatus     safety{};
    bool             relayOn       = false;
    bool             smartMode     = true;
    ChargeStopReason stopReason    = ChargeStopReason::NONE;
    bool             pzemFault     = true;
    bool             fireLocked    = false;
    // Written by Core 0
    bool             wifiConnected = false;
    bool             mqttConnected = false;
    bool             configMode    = false;
    String           lastAlert     = "System boot";
};

// ─────────────────────────────────────────────────────────────────────────────
//  Global RTOS primitives — defined in main.cpp, extern here for shared access
// ─────────────────────────────────────────────────────────────────────────────
extern SemaphoreHandle_t gStateMutex;    ///< Protects gSharedState
extern QueueHandle_t     gCommandQueue;  ///< Core 0 → Core 1 (RelayCommand)
extern QueueHandle_t     gAlertQueue;    ///< Core 1 → Core 0 (AlertMessage)
extern SharedState       gSharedState;
