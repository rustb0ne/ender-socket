# Quick Start Guide - Smart Socket Dashboard

## 🚀 5-Minute Setup

### Step 1: Install Node.js
Download and install from: https://nodejs.org (LTS version recommended)

### Step 2: Install Dependencies
Open Command Prompt/PowerShell and run:
```bash
cd web-dashboard
npm install
```

### Step 3: Start Dashboard
```bash
npm start
```

You should see:
```
🚀 Smart Socket Dashboard listening on http://localhost:3000
📡 MQTT Broker: broker.hivemq.com:1883
```

### Step 4: Open in Browser
Go to **http://localhost:3000**

---

## 📊 What You'll See

1. **Status Indicators** (top)
   - MQTT Connection: Should show "Connected"
   - Device Status: Will show "Online" once ESP32 sends data

2. **Control Panel**
   - Turn relay ON/OFF buttons
   - Toggle Smart Mode button

3. **Live Metrics**
   - Voltage, Current, Power, Energy
   - Socket & SSR temperatures
   - Relay state, Smart mode state

4. **Activity Log**
   - Shows all commands sent and received

---

## 🛠️ Ensure ESP32 is Ready

Before dashboard will work, your ESP32 must be:

1. **Powered ON** and connected to WiFi
2. **Running latest firmware** (with MQTT ACK support)
3. **Connected to same MQTT broker** as dashboard

Check ESP32 status:
- Should connect to `broker.hivemq.com` automatically
- Monitor serial at **115200 baud** for connection logs

---

## ⚙️ Default Configuration

The dashboard uses public free MQTT broker:
- **Broker**: `broker.hivemq.com`
- **Port**: `1883`
- **Dashboard Port**: `3000`

To use a **local MQTT broker** instead, edit `.env`:
```env
MQTT_BROKER=192.168.1.100
MQTT_PORT=1883
```

Then restart dashboard: `npm start`

---

## 📱 Access from Other Devices

### Same Wi-Fi Network
Once running, access from any device on same network:

```
http://<your-pc-ip>:3000
```

Find your PC IP:
- **Windows**: Open Command Prompt, type `ipconfig`, look for "IPv4 Address"
- **Mac/Linux**: Terminal, type `ifconfig`

Example: `http://192.168.1.100:3000`

---

## 🔧 Troubleshooting

### "Cannot find module 'mqtt'"
```bash
npm install
npm start
```

### "MQTT connection timeout"
1. Check internet connection
2. Verify `broker.hivemq.com` is reachable
3. Try public test broker: `test.mosquitto.org`

### "Device status shows Offline"
1. Check ESP32 is connected to WiFi
2. Verify both using same MQTT broker
3. Monitor ESP's serial output at 115200 baud

### "Relay commands not working"
1. Check Command Received ACK in browser console (F12)
2. Monitor `socket/control/ack` topic
3. Ensure ESP firmware was uploaded successfully

---

## 📚 Command Acknowledgment Mechanism

This dashboard uses a **confirmation protocol**:

1. You click "Turn ON" button
2. Web app sends: `{"relay": "ON"}` to `socket/control`
3. ESP32 receives and executes command
4. ESP32 sends back: `{"command":"relay","actual_value":"ON","status":"confirmed"}` to `socket/control/ack`
5. Dashboard receives ACK and **updates display**
6. User sees: ✓ Relay is ON

This ensures **relay state is always accurate** - you only see ON if ESP confirms it's ON.

---

## 🌐 MQTT Topics Used

| Topic | Data |
|-------|------|
| socket/telemetry | All sensor readings (every 1s) |
| socket/control | Send commands to ESP |
| socket/control/ack | ESP replies with confirmation |
| socket/alert | Critical alerts from ESP |

---

## 🔌 Testing MQTT Communication

To debug MQTT, install Mosquitto client:

**Windows**:
```bash
choco install mosquitto-clients
```

**Subscribe to all socket topics**:
```bash
mosquitto_sub -h broker.hivemq.com -t "socket/#" -v
```

You should see:
```
socket/telemetry {"voltage":220.5,"current":0.001,...}
socket/control/ack {"command":"relay","actual_value":"ON",...}
socket/alert {"alert":"System boot"}
```

---

## 📞 Support

If dashboard won't connect:

1. **Check Node.js installed**: `node --version`
2. **Check npm installed**: `npm --version`
3. **Check logs**: Look at terminal output when running `npm start`
4. **Check browser console**: Press F12, look for JavaScript errors
5. **Restart everything**: Kill dashboard, restart ESP32, restart dashboard

---

**Enjoy your Smart Socket Dashboard!** ⚡
