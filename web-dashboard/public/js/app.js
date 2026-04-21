// Connect to Socket.IO server
const socket = io();

// Log entries
const logs = [];
const MAX_LOGS = 20;
let lastDeviceTelemetryTs = 0;
const DEVICE_OFFLINE_TIMEOUT_MS = 5000;

// Socket.IO Events
socket.on('connect', () => {
    console.log('✓ Connected to dashboard server');
    updateIndicator('mqtt-indicator', false);
    updateIndicator('device-indicator', false);
    addLog('Dashboard connected to server', 'success');
});

socket.on('disconnect', () => {
    console.log('✗ Disconnected from dashboard server');
    updateIndicator('mqtt-indicator', false);
    updateIndicator('device-indicator', false);
    addLog('Dashboard disconnected from server', 'warning');
});

socket.on('state_update', (state) => {
    if (typeof state.timestamp === 'number') {
        lastDeviceTelemetryTs = state.timestamp;
    }

    // Update all metrics and status from telemetry (source of truth)
    updateMetrics(state);
    updateStatus(state);
    updateIndicators(state);
});

socket.on('command_sent', (data) => {
    // Brief feedback that command was queued
    console.log('📤 Command sent:', data);
    showAlert(`📤 ${data.command} command sent`, 'info');
    addLog(`Command sent: ${data.command}`, 'info');
});

socket.on('command_acked', (data) => {
    // ACK feedback for audit/logging - state already updated from telemetry
    console.log('✓ Command ACK received:', data);
    showAlert(`✓ ${data.command} confirmed`, 'success');
    addLog(`Command confirmed: ${data.command}`, 'success');
});

socket.on('alert_received', (data) => {
    console.warn('🚨 Alert:', data);
    showAlert(data.message, data.severity);
    
    const severity = data.severity === 'critical' ? '🔥' : '⚠️';
    addLog(`${severity} ${data.message}`, 'warning');
});

// UI Update Functions
function updateMetrics(state) {
    // Power readings
    document.getElementById('voltage').textContent = state.voltage.toFixed(1);
    document.getElementById('current').textContent = state.current.toFixed(3);
    document.getElementById('power').textContent = state.power.toFixed(1);
    document.getElementById('energy').textContent = state.energy_wh.toFixed(1);
    
    // Temperature readings
    const temp1Text = isNaN(state.temp1) ? '--' : state.temp1.toFixed(1);
    const temp2Text = isNaN(state.temp2) ? '--' : state.temp2.toFixed(1);
    document.getElementById('temp1').textContent = temp1Text;
    document.getElementById('temp2').textContent = temp2Text;
    
    // Status text
    document.getElementById('mqtt-status').textContent = state.mqtt;
}

function isDeviceOnline() {
    if (!lastDeviceTelemetryTs) return false;
    return (Date.now() - lastDeviceTelemetryTs) < DEVICE_OFFLINE_TIMEOUT_MS;
}

function updateDeviceStatusText() {
    document.getElementById('device-status').textContent = isDeviceOnline() ? 'Online' : 'Offline';
}

function updateStatus(state) {
    // Relay state
    const relayOn = state.relay_state === 'ON';
    document.getElementById('relay-state-text').textContent = state.relay_state;
    document.getElementById('relay-status').className = 
        'status-indicator ' + (relayOn ? 'on' : 'off');
    
    // Smart mode
    const modeOn = state.charge_mode === 'ON';
    document.getElementById('smart-mode-text').textContent = state.charge_mode;
    document.getElementById('mode-status').className = 
        'status-indicator ' + (modeOn ? 'on' : 'off');
    
    // Fire risk
    document.getElementById('fire-risk-text').textContent = 
        state.fire_risk ? '🔥 YES - CRITICAL' : '✓ No';
    document.getElementById('fire-status').className = 
        'status-indicator ' + (state.fire_risk ? 'off' : 'on');
}

function updateIndicators(state) {
    // MQTT connection
    updateIndicator('mqtt-indicator', state.mqtt === 'Connected');
    
    // Device connection (based on latest telemetry freshness)
    updateIndicator('device-indicator', isDeviceOnline());
    updateDeviceStatusText();
}

function updateIndicator(elementId, isConnected) {
    const element = document.getElementById(elementId);
    if (isConnected) {
        element.classList.remove('disconnected');
        element.classList.add('connected');
    } else {
        element.classList.remove('connected');
        element.classList.add('disconnected');
    }
}

// Control Commands - Buttons always enabled
function sendRelayCommand(state) {
    console.log(`📤 Sending relay command: ${state}`);
    socket.emit('control_relay', { value: state });
    // Button remains enabled - state updates come from telemetry only
}

function toggleSmartMode() {
    console.log('📤 Toggling smart mode');
    socket.emit('toggle_smart_mode');
    // Button remains enabled - state updates come from telemetry only
}

// UI Helpers
function showAlert(message, severity = 'warning') {
    const container = document.getElementById('alert-container');
    
    const alertClass = severity === 'critical' ? 'alert-critical' : 
                      severity === 'success' ? 'alert-success' : 'alert-warning';
    
    const icon = severity === 'critical' ? '🔥' : 
                severity === 'success' ? '✓' : '⚠️';
    
    const alertDiv = document.createElement('div');
    alertDiv.className = `alert-box ${alertClass}`;
    alertDiv.innerHTML = `<span class="alert-icon">${icon}</span><span>${message}</span>`;
    
    container.insertBefore(alertDiv, container.firstChild);
    
    // Remove after 5 seconds
    setTimeout(() => {
        alertDiv.style.transition = 'opacity 0.3s ease';
        alertDiv.style.opacity = '0';
        setTimeout(() => alertDiv.remove(), 300);
    }, 5000);
}

function addLog(message, type = 'info') {
    const timestamp = new Date().toLocaleTimeString();
    logs.unshift({ message, type, timestamp });
    
    // Keep only last MAX_LOGS entries
    if (logs.length > MAX_LOGS) {
        logs.pop();
    }
    
    // Update UI
    const logContainer = document.getElementById('system-log');
    logContainer.innerHTML = logs.map(log => `
        <div class="log-entry">
            <span>${log.message}</span>
            <span class="timestamp">${log.timestamp}</span>
        </div>
    `).join('');
}

// Initial request for current state
window.addEventListener('load', () => {
    setTimeout(() => {
        socket.emit('request_state');
        addLog('Dashboard initialized', 'success');
    }, 500);
});

// Periodic state request (every 30 seconds)
setInterval(() => {
    socket.emit('request_state');
}, 30000);

// Heartbeat check to ensure device online/offline is updated even without incoming events
setInterval(() => {
    const online = isDeviceOnline();
    updateIndicator('device-indicator', online);
    updateDeviceStatusText();
}, 1000);

// Graceful disconnect on page close
window.addEventListener('beforeunload', () => {
    socket.disconnect();
});
