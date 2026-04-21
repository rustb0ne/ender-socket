const express = require('express');
const mqtt = require('mqtt');
const socketIO = require('socket.io');
const { v4: uuidv4 } = require('uuid');
const path = require('path');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;
const MQTT_BROKER = process.env.MQTT_BROKER || '413f71f93ddf45f4a270637f32f088fd.s1.eu.hivemq.cloud';
const MQTT_PORT = process.env.MQTT_PORT || 8883;
const MQTT_USERNAME = process.env.MQTT_USERNAME;
const MQTT_PASSWORD = process.env.MQTT_PASSWORD;

// MQTT Topics
const TOPICS = {
  TELEMETRY: 'socket/telemetry',
  CONTROL: 'socket/control',
  ACK: 'socket/control/ack',
  ALERT: 'socket/alert'
};

// Current device state - built from telemetry (source of truth)
let deviceState = {
  voltage: 0,
  current: 0,
  power: 0,
  energy_wh: 0,
  temp1: null,
  temp2: null,
  relay_state: 'OFF',
  charge_mode: 'OFF',
  wifi: 'Disconnected',
  mqtt: 'Disconnected',
  fire_risk: false,
  last_alert: 'System boot',
  timestamp: Date.now()
};

// Command history - track what user requested
let commandHistory = [];
const MAX_HISTORY = 50;

// Setup Express
app.use(express.static(path.join(__dirname, 'public')));
app.use(express.json());

// Setup Socket.IO
const server = require('http').createServer(app);
const io = socketIO(server, {
  cors: { origin: '*' }
});

// Setup MQTT Client
const mqttClient = mqtt.connect(`mqtts://${MQTT_BROKER}:${MQTT_PORT}`, {
  clientId: `web-dashboard-${uuidv4().slice(0, 8)}`,
  clean: true,
  connectTimeout: 8000,
  reconnectPeriod: 5000,
  username: MQTT_USERNAME,
  password: MQTT_PASSWORD,
  rejectUnauthorized: false,
});

// MQTT Connection Events
mqttClient.on('connect', () => {
  console.log('✓ Connected to MQTT broker');
  deviceState.mqtt = 'Connected';
  
  // Subscribe to all topics
  mqttClient.subscribe([TOPICS.TELEMETRY, TOPICS.ACK, TOPICS.ALERT], (err) => {
    if (err) console.error('Subscribe error:', err);
    else console.log('✓ Subscribed to topics');
  });
  
  broadcastState();
});

mqttClient.on('disconnect', () => {
  console.log('✗ Disconnected from MQTT broker');
  deviceState.mqtt = 'Disconnected';
  broadcastState();
});

mqttClient.on('error', (err) => {
  console.error('MQTT error:', err);
});

// MQTT Message Handler
mqttClient.on('message', (topic, message) => {
  try {
    const payload = message.toString();
    
    switch (topic) {
      case TOPICS.TELEMETRY:
        handleTelemetry(payload);
        break;
      
      case TOPICS.ACK:
        handleAck(payload);
        break;
      
      case TOPICS.ALERT:
        handleAlert(payload);
        break;
    }
  } catch (error) {
    console.error(`Error processing message from ${topic}:`, error);
  }
});

// Handle telemetry data - SOURCE OF TRUTH
function handleTelemetry(payload) {
  try {
    const data = JSON.parse(payload);
    
    // Update device state from telemetry (must contain all fields)
    deviceState = {
      voltage: data.voltage ?? 0,
      current: data.current ?? 0,
      power: data.power ?? 0,
      energy_wh: data.energy_wh ?? 0,
      temp1: data.temp1 ?? null,
      temp2: data.temp2 ?? null,
      relay_state: data.relay_state ?? 'OFF',
      charge_mode: data.charge_mode ?? 'OFF',
      wifi: deviceState.wifi, // Keep from previous
      mqtt: deviceState.mqtt,  // Keep from previous
      fire_risk: data.fire_risk ?? false,
      last_alert: data.last_alert ?? deviceState.last_alert,
      timestamp: Date.now()
    };
    
    // Broadcast new state to all connected clients
    broadcastState();
    
    console.log(`📊 Telemetry: V=${data.voltage}V I=${data.current}A Relay=${data.relay_state} Temp1=${data.temp1}°C`);
  } catch (error) {
    console.error('Telemetry parse error:', error);
  }
}

// Handle ACK from ESP - confirms command execution
function handleAck(payload) {
  try {
    const ack = JSON.parse(payload);
    
    console.log(`✓ ACK: command="${ack.command}" value="${ack.actual_value}"`);
    
    // Add to command history
    addCommandHistory({
      type: 'ack_received',
      command: ack.command,
      value: ack.actual_value,
      timestamp: ack.timestamp || Date.now()
    });
    
    // Emit ACK event to all clients (for visual feedback)
    io.emit('command_acked', {
      command: ack.command,
      value: ack.actual_value,
      timestamp: ack.timestamp || Date.now()
    });
    
  } catch (error) {
    console.error('ACK parse error:', error);
  }
}

// Handle alert messages
function handleAlert(payload) {
  try {
    const alert = JSON.parse(payload);
    
    console.log(`🚨 Alert: ${alert.alert}`);
    
    // Update last alert and fire risk flag
    deviceState.last_alert = alert.alert;
    
    if (alert.alert.includes('FIRE') || alert.alert.includes('>75')) {
      deviceState.fire_risk = true;
    } else if (alert.alert.includes('configured') || alert.alert.includes('boot')) {
      deviceState.fire_risk = false;
    }
    
    broadcastState();
    
    // Emit alert to clients
    io.emit('alert_received', {
      message: alert.alert,
      severity: deviceState.fire_risk ? 'critical' : 'warning',
      timestamp: Date.now()
    });
  } catch (error) {
    console.error('Alert parse error:', error);
  }
}

// Broadcast state to all connected clients
function broadcastState() {
  io.emit('state_update', deviceState);
}

// Add to command history
function addCommandHistory(entry) {
  commandHistory.unshift(entry);
  if (commandHistory.length > MAX_HISTORY) {
    commandHistory.pop();
  }
}

// Socket.IO Connection
io.on('connection', (socket) => {
  console.log(`🔌 Client connected: ${socket.id}`);
  
  // Send initial state
  socket.emit('state_update', deviceState);
  
  // Send command history
  socket.emit('command_history', commandHistory);
  
  // Handle control commands - NO WAITING, just publish
  socket.on('control_relay', (data) => {
    const { value } = data; // ON or OFF
    
    console.log(`📤 Client "${socket.id}" requesting: Relay ${value}`);
    
    // Add to history
    addCommandHistory({
      type: 'relay_requested',
      value: value,
      clientId: socket.id,
      timestamp: Date.now()
    });
    
    // Publish to MQTT immediately (don't wait for ACK)
    const payload = JSON.stringify({ relay: value });
    mqttClient.publish(TOPICS.CONTROL, payload);
    
    // Emit to all clients that command was sent (no ACK needed yet)
    io.emit('command_sent', {
      command: 'relay',
      value: value,
      clientId: socket.id,
      timestamp: Date.now()
    });
  });
  
  socket.on('toggle_smart_mode', () => {
    console.log(`📤 Client "${socket.id}" requesting: Toggle Smart Mode`);
    
    const newMode = deviceState.charge_mode === 'OFF' ? 'ON' : 'OFF';
    
    // Add to history
    addCommandHistory({
      type: 'mode_requested',
      value: newMode,
      clientId: socket.id,
      timestamp: Date.now()
    });
    
    // Publish to MQTT immediately
    const payload = JSON.stringify({ charge_mode: newMode });
    mqttClient.publish(TOPICS.CONTROL, payload);
    
    // Emit to all clients
    io.emit('command_sent', {
      command: 'charge_mode',
      value: newMode,
      clientId: socket.id,
      timestamp: Date.now()
    });
  });
  
  socket.on('disconnect', () => {
    console.log(`🔌 Client disconnected: ${socket.id}`);
  });
  
  socket.on('request_state', () => {
    socket.emit('state_update', deviceState);
  });
});

// REST API endpoints
app.get('/api/status', (req, res) => {
  res.json(deviceState);
});

app.post('/api/control', (req, res) => {
  const { command, value } = req.body;
  
  if (command === 'relay') {
    io.emit('control_relay', { value });
    res.json({ success: true, command, value });
  } else if (command === 'toggle_mode') {
    io.emit('toggle_smart_mode');
    res.json({ success: true, command });
  } else {
    res.status(400).json({ success: false, message: 'Unknown command' });
  }
});

// Command history endpoints
app.get('/api/history', (req, res) => {
  const limit = req.query.limit ? parseInt(req.query.limit) : 50;
  const history = commandHistory.slice(-limit).reverse(); // Latest first
  res.json({
    total: commandHistory.length,
    results: history
  });
});

app.get('/api/command/:id', (req, res) => {
  const command = commandHistory.find(c => c.id === req.params.id);
  if (command) {
    res.json(command);
  } else {
    res.status(404).json({ error: 'Command not found' });
  }
});

app.get('/api/command/status/:id', (req, res) => {
  const command = commandHistory.find(c => c.id === req.params.id);
  if (command) {
    res.json({
      id: command.id,
      status: command.status,
      acked: !!command.ack_timestamp,
      ack_timestamp: command.ack_timestamp,
      response: command.ack_response
    });
  } else {
    res.status(404).json({ error: 'Command not found' });
  }
});

// Root route
app.get('/', (req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

// Start server
server.listen(PORT, () => {
  console.log(`\n🚀 Smart Socket Dashboard listening on http://localhost:${PORT}`);
  console.log(`📡 MQTT Broker: ${MQTT_BROKER}:${MQTT_PORT}\n`);
});
