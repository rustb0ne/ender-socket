# Smart Socket Web Dashboard

A complete web-based management dashboard for the IoT Smart Socket project. This application runs on your PC and communicates with the ESP32 Smart Socket device via MQTT.

## Features

✅ **Real-time Monitoring**
- Live power readings (Voltage, Current, Power, Energy)
- Temperature monitoring (Socket and SSR sensors)
- Device status and alerts

✅ **Remote Control**
- Turn relay ON/OFF
- Toggle Smart Charging Mode
- Receive command acknowledgments (ACK) from device

✅ **Reliable Communication**
- MQTT-based messaging between dashboard and ESP32
- Command confirmation mechanism - UI only updates after ESP confirms
- Automatic timeout handling for failed commands

✅ **Responsive UI**
- Beautiful, modern dashboard design
- Real-time updates via Socket.IO
- Mobile-friendly interface
- Alert notifications for critical events

## Prerequisites

Before running the dashboard, ensure you have:

- **Node.js** (v14 or higher) - Download from https://nodejs.org
- **npm** (comes with Node.js)
- **MQTT Broker** - Default is `broker.hivemq.com` (public, free broker)
- **ESP32 Smart Socket** - Running the firmware from this project

## Installation

### 1. Install Dependencies

```bash
cd web-dashboard
npm install
```

This will install:
- `express` - Web server framework
- `socket.io` - Real-time communication
- `mqtt` - MQTT client library
- `dotenv` - Environment configuration

### 2. Configure MQTT Broker (Optional)

Edit `.env` file to change MQTT broker settings:

```env
MQTT_BROKER=broker.hivemq.com  # Your MQTT broker address
MQTT_PORT=1883                  # MQTT broker port
PORT=3000                       # Dashboard web server port
```

### 3. Run the Dashboard

```bash
npm start
```

Or for development with auto-reload:

```bash
npm run dev
```

The dashboard will start on **http://localhost:3000**

## Architecture

### Communication Flow

```
┌─────────────────────────────────────────────────────┐
│                   Smart Socket                      │
│  (ESP32 - Firmware from this project)               │
│  • Monitor sensors                                  │
│  • Control relay                                    │
│  • Publish telemetry to socket/telemetry            │
│  • Receive commands from socket/control             │
│  • Confirm actions via socket/control/ack           │
└────────────┬────────────────────────────────────────┘
             │
          MQTT Pub/Sub
          (broker.hivemq.com)
             │
┌────────────▼────────────────────────────────────────┐
│          Web Dashboard Server (Node.js)             │
│  • Connect to MQTT broker                           │
│  • Subscribe to device topics                       │
│  • Publish control commands                         │
│  • Wait for ACK before updating UI                  │
│  • Broadcast state to connected clients             │
└────────────┬────────────────────────────────────────┘
             │
         Socket.IO
    (Real-time WebSocket)
             │
┌────────────▼────────────────────────────────────────┐
│           Web Browser (Client)                      │
│  • Display real-time metrics                        │
│  • Send control commands                            │
│  • Show command status                              │
│  • Display alerts                                   │
└─────────────────────────────────────────────────────┘
```

### MQTT Topics

The ESP32 and dashboard communicate via these MQTT topics:

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `socket/telemetry` | ESP → Dashboard | Power/temperature readings (every 1 second) |
| `socket/control` | Dashboard → ESP | Control commands (relay ON/OFF, smart mode) |
| `socket/control/ack` | ESP → Dashboard | Command acknowledgment after execution |
| `socket/alert` | ESP → Dashboard | Critical alerts (fire risk, overheating, etc) |

### Command Acknowledgment Flow

When you click a button on the dashboard:

1. **Dashboard sends command**: Publishes to `socket/control`
   ```json
   {"relay": "ON"}
   ```

2. **ESP executes command**: Turns relay ON

3. **ESP sends ACK**: Publishes to `socket/control/ack`
   ```json
   {
     "command": "relay",
     "requested_value": "ON",
     "actual_value": "ON",
     "status": "confirmed",
     "timestamp": 1234567890
   }
   ```

4. **Dashboard updates UI**: Only after receiving ACK, the button feedback updates

5. **User sees result**: ✓ Command confirmed

## Configuration

### Change MQTT Broker

If you want to use a different MQTT broker (local or cloud), edit `.env`:

```env
MQTT_BROKER=192.168.1.100    # Your local MQTT broker IP
MQTT_PORT=1883                # Default MQTT port
```

**Popular MQTT Brokers:**
- **Public (Free)**: `broker.hivemq.com`, `test.mosquitto.org`
- **Local**: Your Home Assistant/Mosquitto MQTT server
- **Cloud**: AWS IoT Core, Azure IoT Hub, HiveMQ Cloud

### Change Dashboard Port

To run on a different port (e.g., 8080):

```env
PORT=8080
```

Then access at **http://localhost:8080**

## Usage

### Monitoring Metrics

The dashboard displays:

- **Power Section**
  - Voltage (V)
  - Current (A)
  - Power (W)
  - Energy accumulated (Wh)

- **Temperature Section**
  - Socket temperature (>60°C alerts, >75°C critical)
  - SSR temperature (power regulator)

- **Status Section**
  - Relay state (ON/OFF)
  - Smart Charging Mode (ON/OFF)
  - Fire Risk indicator
  - MQTT connection status

### Control Commands

**Relay Control**
- **Turn ON**: Energizes the socket
- **Turn OFF**: De-energizes the socket

**Smart Mode**
- **Toggle**: Enables/disables smart charging algorithm
  - When ON: Auto-detects full charge and cuts power
  - When OFF: Manual relay control only

### Alerts

The dashboard displays important alerts:

- ⚠️ **Warning**: Overheating (>60°C)
- 🔥 **Critical**: Fire risk (>75°C) - Relay automatically locks
- ✓ **Success**: Command confirmed
- ⏳ **Pending**: Waiting for device ACK

## Troubleshooting

### Dashboard won't connect to MQTT

1. **Check MQTT broker is online**
   - For public broker: Visit https://www.hivemq.com/mqtt-broker/
   - For local broker: Ensure service is running

2. **Check network connection**
   ```bash
   ping broker.hivemq.com
   ```

3. **Check firewall**
   - Ensure port 1883 is not blocked

### Relay commands timeout

1. **Check ESP32 is online**
   - Check ESP's serial monitor for WiFi/MQTT status
   - Verify ESP and dashboard are on same network (if using local MQTT)

2. **Check ESP firmware** 
   - Ensure uploaded latest firmware with ACK support
   - Check `TOPIC_ACK` is defined in `config.h`

3. **Increase timeout** (in `server.js`)
   ```javascript
   setTimeout(() => { ... }, 10000);  // Change 5000 to 10000 ms
   ```

### Metrics not updating

1. **Check ESP is publishing telemetry**
   - Monitor MQTT topic with: `mosquitto_sub -h broker.hivemq.com -t socket/#`

2. **Check dashboard logs** (browser console)
   - Press F12 to open Developer Tools
   - Look for error messages

3. **Restart dashboard**
   ```bash
   Ctrl+C to stop
   npm start  to restart
   ```

## Development

### Project Structure

```
web-dashboard/
├── server.js              # Express + Socket.IO server, MQTT client
├── package.json           # Dependencies
├── .env                   # Configuration
├── public/
│   ├── index.html         # Dashboard UI
│   └── js/
│       └── app.js         # Client-side logic
└── README.md              # This file
```

### Server.js File Structure

- `MQTT Connection`: Handles MQTT communication
- `Socket.IO Server`: Real-time updates to clients
- `Command Queue`: Tracks pending commands waiting for ACK
- `REST API`: Optional HTTP endpoints for external access

### Extending the Dashboard

To add new controls:

1. **Add button in index.html**
   ```html
   <button onclick="sendCustomCommand()">My Command</button>
   ```

2. **Implement in app.js**
   ```javascript
   function sendCustomCommand() {
       socket.emit('custom_command', { data: 'value' });
   }
   ```

3. **Handle in server.js**
   ```javascript
   socket.on('custom_command', (data) => {
       const payload = JSON.stringify({ custom: data });
       mqttClient.publish(TOPICS.CONTROL, payload);
   });
   ```

4. **Process in ESP firmware** (main.cpp)
   ```cpp
   if (msg.indexOf("\"custom\":") >= 0) {
       // Handle command
       publishAlert("Custom command received");
   }
   ```

## Support

For issues or questions:

1. **Check logs**: Browser console (F12) and server terminal
2. **Verify MQTT connection**: Monitor broker output
3. **Test with MQTT client**: Use `mosquitto_sub`/`mosquitto_pub`
4. **Check ESP firmware**: Monitor serial output at 115200 baud

## License

MIT
