/**
 * @file main.cpp
 * @brief Ender Socket — Dual-Core FreeRTOS Firmware
 *
 * Core assignment:
 *   Core 1  taskChargeSafety  prio=3  PZEM, DS18B20, ChargeManager, Relay
 *   Core 0  taskNetComms      prio=1  WiFi, MQTT, DisplayUI, Buttons
 *
 * Inter-core communication:
 *   gCommandQueue  Core0 → Core1  RelayCommand (relay/smartmode/reset)
 *   gAlertQueue    Core1 → Core0  AlertMessage (fixed char buffer)
 *   gStateMutex    protects gSharedState read/write
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Ticker.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <PZEM004Tv30.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "config.h"
#include "WiFiStorage.h"
#include "ChargeManager.h"
#include "SafetyMonitor.h"
#include "DisplayUI.h"
#include "SharedState.h"

// ─────────────────────────────────────────────────────────────────────────────
//  RTOS primitives  (declared extern in SharedState.h)
// ─────────────────────────────────────────────────────────────────────────────
SemaphoreHandle_t gStateMutex   = nullptr;
QueueHandle_t     gCommandQueue = nullptr;
QueueHandle_t     gAlertQueue   = nullptr;
SharedState       gSharedState;

// ─────────────────────────────────────────────────────────────────────────────
//  Task handles
// ─────────────────────────────────────────────────────────────────────────────
static TaskHandle_t hChargeSafety = nullptr;
static TaskHandle_t hNetComms     = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
//  Core-0 globals
//  ONLY accessed from taskNetComms (Core 0) or from setup() before tasks start.
// ─────────────────────────────────────────────────────────────────────────────
static WiFiClientSecure gNetClient;
static PubSubClient     gMqtt(gNetClient);
static WiFiManager      gWm;
static Ticker           gGreenBlinkTicker;
static DisplayUI        gUi;

static bool     gWifiConnected  = false;
static bool     gMqttConnected  = false;
static bool     gConfigMode     = false;
static bool     gPortalStarted  = false;
static String   gLastAlert      = "System boot";

static uint32_t gLastTelemetryMs = 0;
static uint32_t gLastWifiRetryMs = 0;
static uint32_t gLastMqttRetryMs = 0;

// Button 1 state
static bool     gBtn1Stable   = LOW;
static bool     gBtn1Raw      = LOW;
static uint32_t gBtn1Debounce = 0;
static uint32_t gBtn1PressMs  = 0;

// Button 2 state
static bool     gBtn2Stable   = LOW;
static bool     gBtn2Raw      = LOW;
static uint32_t gBtn2Debounce = 0;
static uint32_t gBtn2PressMs  = 0;

// Pending ACK commands (for MQTT command verification)
struct PendingCmd {
    String   command;
    String   value;
    uint32_t submitMs;
    bool     active = false;
};
static PendingCmd gPendingCmds[2];
static constexpr uint32_t CMD_VERIFY_MS = 150;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers — callable from Core 0 only
// ─────────────────────────────────────────────────────────────────────────────
static void blinkGreen() {
    digitalWrite(LED_GREEN_PIN, !digitalRead(LED_GREEN_PIN));
}

/** Publish an alert string to MQTT and write it to gSharedState.lastAlert. */
static void netPublishAlert(const String &msg) {
    gLastAlert = msg;
    if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        gSharedState.lastAlert = msg;
        xSemaphoreGive(gStateMutex);
    }
    if (gMqtt.connected()) {
        const String payload = String("{\"alert\":\"") + msg + "\"}";
        gMqtt.publish(TOPIC_ALERT, payload.c_str(), true);
    }
}

static void addPendingCmd(const String &cmd, const String &val) {
    for (int i = 0; i < 2; i++) {
        if (!gPendingCmds[i].active) {
            gPendingCmds[i] = {cmd, val, millis(), true};
            return;
        }
    }
    Serial.println("⚠️ Pending command buffer full");
}

// ─────────────────────────────────────────────────────────────────────────────
//  MQTT callback  (called from Core 0 inside gMqtt.loop())
// ─────────────────────────────────────────────────────────────────────────────
static void mqttCallback(char *topic, byte *payload, unsigned int length) {
    if (String(topic) != TOPIC_CONTROL) return;

    String msg;
    msg.reserve(length);
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

    RelayCommand cmd{};
    bool sent = false;
    String cmdType, cmdVal;

    // ── Relay commands ──────────────────────────────────────────────────────
    if (msg.indexOf("\"relay\":\"ON\"") >= 0) {
        cmd = {CmdType::RELAY, true};
        xQueueSend(gCommandQueue, &cmd, 0);
        cmdType = "relay"; cmdVal = "ON"; sent = true;
        netPublishAlert("MQTT: Relay ON command received");
    } else if (msg.indexOf("\"relay\":\"OFF\"") >= 0) {
        cmd = {CmdType::RELAY, false};
        xQueueSend(gCommandQueue, &cmd, 0);
        cmdType = "relay"; cmdVal = "OFF"; sent = true;
        netPublishAlert("MQTT: Relay OFF command received");
    }

    // ── Smart-mode commands ─────────────────────────────────────────────────
    if (msg.indexOf("\"charge_mode\":\"ON\"") >= 0) {
        cmd = {CmdType::SMART_MODE, true};
        xQueueSend(gCommandQueue, &cmd, 0);
        cmdType = "charge_mode"; cmdVal = "ON"; sent = true;
        netPublishAlert("MQTT: Smart mode ON command received");
    } else if (msg.indexOf("\"charge_mode\":\"OFF\"") >= 0) {
        cmd = {CmdType::SMART_MODE, false};
        xQueueSend(gCommandQueue, &cmd, 0);
        cmdType = "charge_mode"; cmdVal = "OFF"; sent = true;
        netPublishAlert("MQTT: Smart mode OFF command received");
    }

    if (sent) addPendingCmd(cmdType, cmdVal);
}

// ─────────────────────────────────────────────────────────────────────────────
//  TASK: taskChargeSafety  (Core 1, Priority HIGH)
//
//  Owns (exclusively):
//    PZEM004Tv30 via Serial2 (UART)
//    SafetyMonitor via DS18B20 OneWire
//    ChargeManager — relay digitalWrite(RELAY_PIN)
//    LED_RED_PIN and BUZZER_PIN via SafetyMonitor
//
//  Communicates to Core 0 via:
//    gSharedState (mutex)   — writes power/safety/charge snapshot
//    gAlertQueue            — pushes AlertMessage on safety events
//
//  Receives commands from Core 0 via:
//    gCommandQueue          — RelayCommand
// ─────────────────────────────────────────────────────────────────────────────
static void taskChargeSafety(void * /*param*/) {
    Serial.printf("[Core%d] taskChargeSafety started\n", xPortGetCoreID());

    // Core-1-local objects — never accessed from Core 0
    PZEM004Tv30   pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);
    ChargeManager charger;
    SafetyMonitor safety;

    charger.begin();
    safety.begin();

    // Local working state
    PowerReading     localPower{};
    bool             localFireLocked = false;
    bool             localPzemFault  = true;
    uint32_t         lastPzemRead    = 0;
    ChargeStopReason prevStopReason  = ChargeStopReason::NONE;

    // Helper: push alert to Core 0 without blocking
    auto pushAlert = [](const char *msg) {
        AlertMessage am{};
        strncpy(am.text, msg, ALERT_TEXT_MAX - 1);
        xQueueSend(gAlertQueue, &am, 0);
    };

    TickType_t xLastWake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(CHARGE_TASK_PERIOD_MS));
        const uint32_t now = millis();

        // ── 1. Drain command queue ────────────────────────────────────────────
        RelayCommand cmd{};
        while (xQueueReceive(gCommandQueue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CmdType::RELAY:
                    charger.commandRelay(cmd.value, localFireLocked);
                    Serial.printf("[Core1] Relay → %s\n", cmd.value ? "ON" : "OFF");
                    break;
                case CmdType::SMART_MODE:
                    charger.setSmartMode(cmd.value);
                    Serial.printf("[Core1] SmartMode → %s\n", cmd.value ? "ON" : "OFF");
                    break;
                case CmdType::RESET_ENERGY:
                    if (pzem.resetEnergy()) {
                        pushAlert("Energy counter reset");
                    } else {
                        pushAlert("Failed to reset energy counter");
                    }
                    break;
            }
        }

        // ── 2. Read PZEM @1Hz ────────────────────────────────────────────────
        if (now - lastPzemRead >= SENSOR_READ_MS) {
            lastPzemRead = now;

            const float v    = pzem.voltage();
            const float i    = pzem.current();
            const float p    = pzem.power();
            const float eKwh = pzem.energy();
            const bool  valid = !isnan(v) && !isnan(i) && !isnan(p) && !isnan(eKwh);

            localPower.valid    = valid;
            localPower.voltage  = valid ? v    : 0.0f;
            localPower.current  = valid ? i    : 0.0f;
            localPower.power    = valid ? p    : 0.0f;
            localPower.energyWh = valid ? (eKwh * 1000.0f) : 0.0f;

            if (!valid && !localPzemFault) pushAlert("PZEM read failure or disconnected");
            localPzemFault = !valid;
        }

        // ── 3. Safety monitor ────────────────────────────────────────────────
        safety.setPeripheralFault(localPzemFault);
        safety.update();

        const SafetyStatus &s = safety.status();

        if (s.overheat && !s.fireRisk) {
            pushAlert("Overheating >60C: relay forced OFF");
        }
        if (s.overheat) {
            charger.forceRelayOff(ChargeStopReason::SAFETY);
        }
        if (s.fireRisk && !localFireLocked) {
            localFireLocked = true;
            charger.forceRelayOff(ChargeStopReason::SAFETY);
            pushAlert("FIRE RISK >75C: relay locked until hard reset");
        }

        // ── 4. Charge manager ────────────────────────────────────────────────
        charger.update(localPower, localFireLocked);

        const ChargeStopReason curReason = charger.lastStopReason();
        if (curReason == ChargeStopReason::SPIKE && prevStopReason != ChargeStopReason::SPIKE) {
            pushAlert("Charging current spike detected: relay forced OFF");
        }
        prevStopReason = curReason;

        // ── 5. Write shared state (brief mutex hold) ─────────────────────────
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            gSharedState.power      = localPower;
            gSharedState.safety     = s;
            gSharedState.relayOn    = charger.relayOn();
            gSharedState.smartMode  = charger.smartMode();
            gSharedState.stopReason = curReason;
            gSharedState.pzemFault  = localPzemFault;
            gSharedState.fireLocked = localFireLocked;
            xSemaphoreGive(gStateMutex);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  TASK: taskNetComms  (Core 0, Priority LOW)
//
//  Owns (exclusively):
//    WiFi / WiFiManager / PubSubClient / WiFiClientSecure
//    DisplayUI (TFT_eSPI — SPI bus)
//    LED_GREEN_PIN
//    TOUCH_BTN_PIN, TOUCH_BTN2_PIN
//
//  Communicates to Core 1 via:
//    gCommandQueue — sends RelayCommand on button/MQTT events
//
//  Receives from Core 1 via:
//    gSharedState (mutex) — reads power/safety/charge snapshot for display+telemetry
//    gAlertQueue          — drains AlertMessage, publishes to MQTT
// ─────────────────────────────────────────────────────────────────────────────
static void taskNetComms(void * /*param*/) {
    Serial.printf("[Core%d] taskNetComms started\n", xPortGetCoreID());

    // Init display (SPI must be called from Core 0)
    gUi.begin();

    // Connect WiFi
    WiFiCredentials creds{"", ""};
    WiFi.mode(WIFI_STA);
    if (WiFiStorage::loadWiFiCredentials(creds) && strlen(creds.ssid) > 0) {
        Serial.printf("[Core0] Using saved WiFi: %s\n", creds.ssid);
        WiFi.begin(creds.ssid, creds.password);
    } else {
        Serial.printf("[Core0] Using hardcoded WiFi: %s\n", WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
    }

    gWm.setConfigPortalTimeout(300);
    gWm.setCleanConnect(true);
    gWm.setConfigPortalBlocking(false);

    if (MQTT_USE_TLS) gNetClient.setInsecure();
    gMqtt.setServer(MQTT_HOST, MQTT_PORT);
    gMqtt.setCallback(mqttCallback);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        const uint32_t now = millis();

        // ── Button 1 (TOUCH_BTN_PIN) ─────────────────────────────────────────
        {
            const bool raw = digitalRead(TOUCH_BTN_PIN) == HIGH;
            if (raw != gBtn1Raw) { gBtn1Debounce = now; gBtn1Raw = raw; }
            if ((now - gBtn1Debounce >= BTN_DEBOUNCE_MS) && (raw != gBtn1Stable)) {
                gBtn1Stable = raw;
                if (gBtn1Stable) {
                    gBtn1PressMs = now;
                } else if (gBtn1PressMs != 0) {
                    const uint32_t held = now - gBtn1PressMs;
                    gBtn1PressMs = 0;
                    if (held >= BTN_LONG_MIN_MS) {
                        // Toggle smart mode — read current value from shared state
                        bool curSmart = true;
                        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            curSmart = gSharedState.smartMode;
                            xSemaphoreGive(gStateMutex);
                        }
                        const RelayCommand cmd{CmdType::SMART_MODE, !curSmart};
                        xQueueSend(gCommandQueue, &cmd, 0);
                        netPublishAlert(!curSmart ? "Smart charging mode ON" : "Smart charging mode OFF");
                    } else if (held <= BTN_SINGLE_MAX_MS) {
                        // Reset energy counter
                        const RelayCommand cmd{CmdType::RESET_ENERGY, false};
                        xQueueSend(gCommandQueue, &cmd, 0);
                    }
                }
            }
        }

        // ── Button 2 (TOUCH_BTN2_PIN) ────────────────────────────────────────
        {
            const bool raw = digitalRead(TOUCH_BTN2_PIN) == HIGH;
            if (raw != gBtn2Raw) {
                Serial.printf("[BTN2] raw changed → %s\n", raw ? "HIGH" : "LOW");
                gBtn2Debounce = now;
                gBtn2Raw = raw;
            }
            if ((now - gBtn2Debounce >= BTN_DEBOUNCE_MS) && (raw != gBtn2Stable)) {
                gBtn2Stable = raw;
                if (gBtn2Stable) {
                    gBtn2PressMs = now;
                    Serial.println("[BTN2] press start recorded");
                } else if (gBtn2PressMs != 0) {
                    const uint32_t held = now - gBtn2PressMs;
                    gBtn2PressMs = 0;
                    Serial.printf("[BTN2] released after %lums\n", held);
                    if (held >= BTN2_PORTAL_MIN_MS) {
                        Serial.println("[BTN2] → HOLD: start WiFi portal");
                        if (!gUi.isDisplayOn()) gUi.toggleDisplay();
                        if (!gConfigMode) {
                            gConfigMode    = true;
                            gPortalStarted = false;
                            netPublishAlert("Config portal started - connect to SmartSocket-Setup WiFi");
                        }
                    } else if (held <= BTN2_SINGLE_MAX_MS) {
                        Serial.println("[BTN2] → TAP: toggle display");
                        gUi.toggleDisplay();
                    } else {
                        Serial.printf("[BTN2] → ignored (%lums)\n", held);
                    }
                }
            }
        }

        // ── WiFi management ──────────────────────────────────────────────────
        if (gConfigMode) {
            if (!gPortalStarted) {
                Serial.println("📡 Starting WiFi config portal AP: SmartSocket-Setup");
                WiFi.disconnect(true);
                vTaskDelay(pdMS_TO_TICKS(100));
                gGreenBlinkTicker.attach(0.5f, blinkGreen);
                digitalWrite(LED_GREEN_PIN, LOW);
                gWm.startConfigPortal("SmartSocket-Setup", "12345678");
                gPortalStarted = true;
                Serial.println("📡 Open http://192.168.4.1 after connecting to SmartSocket-Setup");
            }
            gWm.process();
            if (!gWm.getConfigPortalActive() && WiFi.status() == WL_CONNECTED) {
                Serial.printf("✓ WiFi connected: %s\n", WiFi.SSID().c_str());
                WiFiStorage::saveWiFiCredentials(WiFi.SSID().c_str(), WiFi.psk().c_str());
                gConfigMode    = false;
                gPortalStarted = false;
                gGreenBlinkTicker.detach();
                digitalWrite(LED_GREEN_PIN, HIGH);
                digitalWrite(LED_RED_PIN, LOW);
                netPublishAlert("WiFi configured successfully");
            }
            gWifiConnected = (WiFi.status() == WL_CONNECTED) && !gConfigMode;
        } else if (WiFi.status() == WL_CONNECTED) {
            gWifiConnected = true;
            digitalWrite(LED_GREEN_PIN, HIGH);
        } else {
            gWifiConnected = false;
            if (now - gLastWifiRetryMs >= WIFI_RETRY_MS) {
                gLastWifiRetryMs = now;
                Serial.println("🔄 WiFi reconnecting...");
                WiFi.reconnect();
            }
        }

        // ── MQTT management ──────────────────────────────────────────────────
        if (!gWifiConnected) {
            gMqttConnected = false;
        } else if (gMqtt.connected()) {
            gMqttConnected = true;
            gMqtt.loop();   // process inbound (fires mqttCallback → gCommandQueue)
        } else {
            gMqttConnected = false;
            if (now - gLastMqttRetryMs >= MQTT_RETRY_MS) {
                gLastMqttRetryMs = now;
                if (gMqtt.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD)) {
                    gMqtt.subscribe(TOPIC_CONTROL);
                    gMqttConnected = true;
                    netPublishAlert("MQTT connected");
                }
            }
        }

        // ── Drain alert queue from Core 1 ────────────────────────────────────
        AlertMessage am{};
        while (xQueueReceive(gAlertQueue, &am, 0) == pdTRUE) {
            netPublishAlert(String(am.text));
        }

        // ── Process pending ACK commands ─────────────────────────────────────
        {
            // Snapshot to verify against
            SharedState snap{};
            if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                snap = gSharedState;
                xSemaphoreGive(gStateMutex);
            }

            for (int i = 0; i < 2; i++) {
                if (!gPendingCmds[i].active) continue;
                if (now - gPendingCmds[i].submitMs < CMD_VERIFY_MS) continue;

                const String &command  = gPendingCmds[i].command;
                const String &requested = gPendingCmds[i].value;
                String actual;
                bool verified = false;

                if (command == "relay") {
                    actual   = snap.relayOn   ? "ON" : "OFF";
                    verified = (actual == requested);
                } else if (command == "charge_mode") {
                    actual   = snap.smartMode  ? "ON" : "OFF";
                    verified = (actual == requested);
                }

                Serial.printf("%s %s command: req=%s actual=%s\n",
                              verified ? "✓" : "❌", command.c_str(),
                              requested.c_str(), actual.c_str());

                if (gMqtt.connected()) {
                    const String ack =
                        String("{\"command\":\"")     + command +
                        "\",\"requested_value\":\"" + requested +
                        "\",\"actual_value\":\""    + actual +
                        "\",\"timestamp\":"         + String(millis()) +
                        ",\"status\":\""            + (verified ? "confirmed" : "failed") + "\"}";
                    gMqtt.publish(TOPIC_ACK, ack.c_str(), false);
                }
                if (!verified) {
                    netPublishAlert(String("⚠️ Command failed: ") + command +
                                   " req=" + requested + " got=" + actual);
                }
                gPendingCmds[i].active = false;
            }
        }

        // ── Update connectivity flags in shared state ─────────────────────────
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            gSharedState.wifiConnected = gWifiConnected;
            gSharedState.mqttConnected = gMqttConnected;
            gSharedState.configMode    = gConfigMode;
            xSemaphoreGive(gStateMutex);
        }

        // ── Take display snapshot and refresh display ────────────────────────
        SharedState displaySnap{};
        if (xSemaphoreTake(gStateMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            displaySnap = gSharedState;
            xSemaphoreGive(gStateMutex);
        }
        gUi.update(displaySnap);

        // ── Publish telemetry ────────────────────────────────────────────────
        if (gMqttConnected && (now - gLastTelemetryMs >= TELEMETRY_PUBLISH_MS)) {
            gLastTelemetryMs = now;
            const PowerReading &r = displaySnap.power;
            const SafetyStatus &s = displaySnap.safety;

            String payload = "{";
            payload += "\"voltage\":"    + String(r.voltage, 2)  + ",";
            payload += "\"current\":"    + String(r.current, 3)  + ",";
            payload += "\"power\":"      + String(r.power, 2)    + ",";
            payload += "\"energy_wh\":"  + String(r.energyWh, 1) + ",";
            payload += "\"temp1\":"      + (isnan(s.tempSocketC) ? String("null") : String(s.tempSocketC, 1)) + ",";
            payload += "\"temp2\":"      + (isnan(s.tempSsrC)    ? String("null") : String(s.tempSsrC, 1))    + ",";
            payload += "\"relay_state\":\"" + String(displaySnap.relayOn  ? "ON" : "OFF") + "\",";
            payload += "\"charge_mode\":\"" + String(displaySnap.smartMode ? "ON" : "OFF") + "\"";
            payload += "}";
            gMqtt.publish(TOPIC_TELEMETRY, payload.c_str(), false);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup()  — runs on Core 1 before scheduler starts
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n\n══════════════════════════════════");
    Serial.println("  Ender Socket — RTOS Dual-Core");
    Serial.println("══════════════════════════════════");

    // GPIO (safe to do here before both tasks start)
    pinMode(TOUCH_BTN_PIN,  INPUT);
    pinMode(TOUCH_BTN2_PIN, INPUT);
    pinMode(LED_GREEN_PIN,  OUTPUT);
    pinMode(LED_RED_PIN,    OUTPUT);
    pinMode(BUZZER_PIN,     OUTPUT);

    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN,   LOW);
    digitalWrite(BUZZER_PIN,    LOW);

    WiFiStorage::initSPIFS();
    WiFiStorage::printSPIFFSInfo();

    // Create RTOS synchronization primitives
    gStateMutex   = xSemaphoreCreateMutex();
    gCommandQueue = xQueueCreate(RTOS_CMD_QUEUE_DEPTH,   sizeof(RelayCommand));
    gAlertQueue   = xQueueCreate(RTOS_ALERT_QUEUE_DEPTH, sizeof(AlertMessage));

    configASSERT(gStateMutex);
    configASSERT(gCommandQueue);
    configASSERT(gAlertQueue);

    // Spawn tasks
    xTaskCreatePinnedToCore(
        taskChargeSafety, "ChargeSafety",
        RTOS_CHARGE_STACK, nullptr,
        RTOS_CHARGE_PRIO,  &hChargeSafety,
        RTOS_CHARGE_CORE
    );

    xTaskCreatePinnedToCore(
        taskNetComms, "NetComms",
        RTOS_NETCOMMS_STACK, nullptr,
        RTOS_NETCOMMS_PRIO,  &hNetComms,
        RTOS_NETCOMMS_CORE
    );

    Serial.println("✓ Tasks spawned:");
    Serial.printf("  Core %d  ChargeSafety  prio=%d\n", RTOS_CHARGE_CORE,   RTOS_CHARGE_PRIO);
    Serial.printf("  Core %d  NetComms      prio=%d\n", RTOS_NETCOMMS_CORE, RTOS_NETCOMMS_PRIO);
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop()  — Arduino loop task is no longer needed; delete self immediately.
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    vTaskDelete(nullptr);
}
