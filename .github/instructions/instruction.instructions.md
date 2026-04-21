Act as an expert C++ embedded developer for the ESP32 platform. Write the complete, modular, and non-blocking source code (using millis(), absolutely no delay() in the loop) for an "IoT Smart Socket" project based on the following detailed requirements.

[1. HARDWARE LIST & ESP32 PINOUT]
- Shared SPI Bus (TFT, Touch, SD): SCK=18, MISO=19, MOSI=23.
- 2.8" TFT Display (ILI9341): CS=17, D/C=16, RESET=5, LED=32.
- Touch Controller (XPT2046/T_): T_CS=21, T_CLK=18, T_DI=23, T_DO=19.
- SD Card: SD_CS=12.
- TTP223 Capacitive Touch Button: GPIO 4.
- SSR Fotek DA (Active HIGH): GPIO 27.
- PZEM-004T V3.0 (AC Power Measurement): Use Hardware Serial2, but REMAP pins to RX=26, TX=25 (since default 16/17 are used by the TFT).
- Temperature Sensors (2x DS18B20 on a single 1-Wire bus): GPIO 13.
- Status LEDs: Green (Normal/WiFi) = GPIO 14, Red (Error/Alert) = GPIO 2.
- 3V Buzzer (Fire Alarm): GPIO 15.

[2. REQUIRED LIBRARIES]
Include and use the following libraries: TFT_eSPI (for GUI), PZEM004Tv30, OneWire, DallasTemperature, PubSubClient (for MQTT), WiFiManager (for AP WiFi provisioning), and Ticker (for non-blocking timers if needed).

[3. CORE LOGIC & FEATURES]
1. PZEM-004T Data: Read Voltage (V), Current (A), Power (W), and Energy (Wh) every 1 second.
2. TTP223 Button Logic (Requires Debounce & Press Duration tracking):
   - Single Click: Reset the Energy (Wh) value on the PZEM and update the display.
   - Long Press < 5 seconds (e.g., 2s - 4.9s): Toggle "Smart Charging Mode" ON/OFF.
   - Hold >= 5 seconds: Enter WiFi Config Mode (Start WiFiManager AP). Blink Green LED every 500ms while configuring.
3. TFT ILI9341 Display: Create a clean UI to display V, A, W, Wh, Temp1, Temp2, WiFi/MQTT status, SSR Status (ON/OFF), and Smart Charging Mode Status. Update values dynamically without flickering.
4. MQTT Communication:
   - Publish telemetry (V, A, W, Wh, Temp1, Temp2, Relay_State) to "socket/telemetry" every 1 second.
   - Subscribe to "socket/control" to receive JSON commands: {"relay": "ON"/"OFF"} or {"charge_mode": "ON"/"OFF"}.
   - Publish error alerts to "socket/alert" (e.g., Overheating, hardware failure).

[4. SAFETY & PROTECTION LOGIC (CRITICAL)]
- Continuously monitor the 2x DS18B20 sensors (one at the AC socket, one at the SSR).
- If any Temp > 60°C: Turn OFF SSR immediately, turn ON Red LED, publish MQTT alert.
- If any Temp > 75°C (Fire risk): Turn ON Buzzer continuously, turn OFF SSR, turn ON Red LED, and trigger a system lock (refuse to turn the relay back on until a hard hardware reset).
- If sensor reading fails (PZEM or DS18B20 disconnected): Turn ON Red LED to indicate a peripheral fault.

[5. SMART CHARGING ALGORITHM]
When "Smart Charging Mode" is ON, execute the following state machine to safely cut off the SSR:
a. Auto-Cutoff on Full: If the current (A) gradually drops and stabilizes at a very low trickle level (e.g., < 0.1A) for 5 continuous minutes -> Turn OFF SSR.
b. Unplug Detection: If the current drops abruptly to 0A (within 1-2 seconds) -> Turn OFF SSR immediately (user unplugged the device).
c. Timeout Limit: Automatically turn OFF SSR after 8 hours of continuous charging, regardless of state.
d. Anomaly/Spike Detection: If the charging current is stable but suddenly spikes (e.g., >30% above the 5-minute rolling average) -> Turn OFF SSR immediately and trigger an alert (suspected battery swelling or adapter fault).

Structure the code professionally. Create a clean setup() and loop(), and extract functionalities into separate modular functions (e.g., handleMQTT(), readSensors(), updateDisplay(), smartChargingLogic(), checkSafety()). Start generating the code now.