#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"

// WiFi credentials structure
struct WiFiCredentials {
    char ssid[WIFI_SSID_MAX];
    char password[WIFI_PASS_MAX];
};

class WiFiStorage {
public:
    // Initialize SPIFFS
    static bool initSPIFS() {
        if (!SPIFFS.begin(true)) {
            Serial.println("❌ SPIFFS mount failed");
            return false;
        }
        Serial.println("✓ SPIFFS mounted");
        return true;
    }

    // Load WiFi credentials from SPIFFS
    static bool loadWiFiCredentials(WiFiCredentials &creds) {
        if (!SPIFFS.exists(WIFI_CREDS_FILE)) {
            Serial.println("⚠️ WiFi credentials file not found");
            memset(&creds, 0, sizeof(WiFiCredentials));
            return false;
        }

        File file = SPIFFS.open(WIFI_CREDS_FILE, "r");
        if (!file) {
            Serial.println("❌ Failed to open WiFi credentials file");
            return false;
        }

        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, file) != DeserializationError::Ok) {
            Serial.println("❌ Failed to parse WiFi credentials JSON");
            file.close();
            return false;
        }

        file.close();

        // Extract credentials
        if (doc.containsKey("ssid") && doc.containsKey("password")) {
            strncpy(creds.ssid, doc["ssid"], WIFI_SSID_MAX - 1);
            strncpy(creds.password, doc["password"], WIFI_PASS_MAX - 1);
            creds.ssid[WIFI_SSID_MAX - 1] = '\0';
            creds.password[WIFI_PASS_MAX - 1] = '\0';
            
            Serial.print("✓ WiFi credentials loaded: ");
            Serial.println(creds.ssid);
            return true;
        }

        Serial.println("❌ WiFi credentials format invalid");
        return false;
    }

    // Save WiFi credentials to SPIFFS
    static bool saveWiFiCredentials(const char *ssid, const char *password) {
        if (!ssid || !password || strlen(ssid) == 0) {
            Serial.println("❌ Invalid WiFi credentials");
            return false;
        }

        StaticJsonDocument<256> doc;
        doc["ssid"] = ssid;
        doc["password"] = password;

        File file = SPIFFS.open(WIFI_CREDS_FILE, "w");
        if (!file) {
            Serial.println("❌ Failed to open WiFi credentials file for writing");
            return false;
        }

        if (serializeJson(doc, file) == 0) {
            Serial.println("❌ Failed to save WiFi credentials");
            file.close();
            return false;
        }

        file.close();
        Serial.print("✓ WiFi credentials saved: ");
        Serial.println(ssid);
        return true;
    }

    // Clear saved credentials
    static bool clearWiFiCredentials() {
        if (!SPIFFS.exists(WIFI_CREDS_FILE)) {
            return true;
        }
        
        if (SPIFFS.remove(WIFI_CREDS_FILE)) {
            Serial.println("✓ WiFi credentials cleared");
            return true;
        }
        
        Serial.println("❌ Failed to clear WiFi credentials");
        return false;
    }

    // Get SPIFFS info
    static void printSPIFFSInfo() {
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
        Serial.printf("📊 SPIFFS: %d/%d bytes used\n", used, total);
    }
};
