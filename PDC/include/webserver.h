/**
 * @file webserver.h
 * @brief Complete Web Interface for Heat Pump Controller
 * 
 * Features:
 * - Real-time dashboard with auto-refresh
 * - Serial Monitor commands via web
 * - Live logs display
 * - SD card status and file browser
 * - Modbus diagnostic tools
 * - System configuration
 * 
 * @author ProEnergy Green SRL / ThermXpert
 * @date 2026-03-07
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"

// Forward declarations
extern HeatPumpData hpData;
extern ControlConfig controlConfig;
extern SystemStats stats;
extern bool sdCardAvailable;
extern AsyncWebServer server;
extern ModbusRTU modbus;

// Functions from main.cpp
String getStatusJSON();
bool sendStartStopCommand(bool start);

// Web page storage
String webLog = "";
const int MAX_WEB_LOG_LINES = 200;

// Function to add log entry
void addWebLog(const String& message) {
    String timestamp = String(millis() / 1000);
    webLog = "[" + timestamp + "s] " + message + "\n" + webLog;
    
    // Limit log size
    int lineCount = 0;
    int lastNewline = -1;
    for (int i = 0; i < webLog.length(); i++) {
        if (webLog[i] == '\n') {
            lineCount++;
            if (lineCount > MAX_WEB_LOG_LINES) {
                lastNewline = i;
                break;
            }
        }
    }
    if (lastNewline > 0) {
        webLog = webLog.substring(0, lastNewline);
    }
}

// HTML Pages
const char HTML_HEADER[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Heat Pump Controller</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }
        .container { 
            max-width: 1400px; 
            margin: 0 auto; 
            background: white; 
            border-radius: 15px; 
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }
        .header h1 { font-size: 32px; margin-bottom: 10px; }
        .header p { opacity: 0.9; font-size: 14px; }
        
        .nav {
            background: #f8f9fa;
            border-bottom: 1px solid #dee2e6;
            display: flex;
            flex-wrap: wrap;
            padding: 0;
        }
        .nav button {
            flex: 1;
            min-width: 120px;
            padding: 15px 20px;
            background: transparent;
            border: none;
            border-bottom: 3px solid transparent;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            color: #495057;
            transition: all 0.3s;
        }
        .nav button:hover { background: #e9ecef; }
        .nav button.active {
            color: #667eea;
            border-bottom-color: #667eea;
            background: white;
        }
        
        .content { padding: 30px; }
        .tab-pane { display: none; }
        .tab-pane.active { display: block; }
        
        .grid { 
            display: grid; 
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); 
            gap: 20px; 
            margin-bottom: 30px;
        }
        .card {
            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .card-title {
            font-size: 12px;
            color: #6c757d;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 10px;
            font-weight: 600;
        }
        .card-value {
            font-size: 36px;
            font-weight: bold;
            color: #2c3e50;
        }
        .card-unit { font-size: 18px; color: #95a5a6; margin-left: 5px; }
        
        .status-ok { color: #27ae60; }
        .status-warn { color: #f39c12; }
        .status-error { color: #e74c3c; }
        
        .btn {
            padding: 12px 24px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 14px;
            font-weight: 600;
            transition: all 0.3s;
            margin: 5px;
        }
        .btn-primary { background: #667eea; color: white; }
        .btn-primary:hover { background: #5568d3; }
        .btn-success { background: #27ae60; color: white; }
        .btn-success:hover { background: #229954; }
        .btn-danger { background: #e74c3c; color: white; }
        .btn-danger:hover { background: #c0392b; }
        .btn-secondary { background: #95a5a6; color: white; }
        .btn-secondary:hover { background: #7f8c8d; }
        
        .log-box {
            background: #2c3e50;
            color: #ecf0f1;
            padding: 20px;
            border-radius: 8px;
            font-family: 'Courier New', monospace;
            font-size: 13px;
            height: 500px;
            overflow-y: auto;
            white-space: pre-wrap;
            word-wrap: break-word;
        }
        
        .input-group {
            display: flex;
            gap: 10px;
            margin: 20px 0;
        }
        .input-group input {
            flex: 1;
            padding: 12px;
            border: 2px solid #dee2e6;
            border-radius: 6px;
            font-size: 14px;
        }
        .input-group input:focus {
            outline: none;
            border-color: #667eea;
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
            background: white;
            border-radius: 8px;
            overflow: hidden;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #dee2e6;
        }
        th {
            background: #f8f9fa;
            font-weight: 600;
            color: #495057;
        }
        tr:hover { background: #f8f9fa; }
        
        .badge {
            display: inline-block;
            padding: 4px 12px;
            border-radius: 12px;
            font-size: 12px;
            font-weight: 600;
        }
        .badge-success { background: #d4edda; color: #155724; }
        .badge-danger { background: #f8d7da; color: #721c24; }
        .badge-warning { background: #fff3cd; color: #856404; }
        .badge-info { background: #d1ecf1; color: #0c5460; }
        
        .progress {
            background: #e9ecef;
            border-radius: 4px;
            height: 20px;
            overflow: hidden;
        }
        .progress-bar {
            background: linear-gradient(90deg, #667eea 0%, #764ba2 100%);
            height: 100%;
            transition: width 0.3s;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-size: 12px;
            font-weight: 600;
        }
        
        .alert {
            padding: 15px 20px;
            border-radius: 8px;
            margin: 20px 0;
        }
        .alert-info { background: #d1ecf1; color: #0c5460; border-left: 4px solid #17a2b8; }
        .alert-success { background: #d4edda; color: #155724; border-left: 4px solid #28a745; }
        .alert-warning { background: #fff3cd; color: #856404; border-left: 4px solid #ffc107; }
        .alert-danger { background: #f8d7da; color: #721c24; border-left: 4px solid #dc3545; }
    </style>
</head>
<body>
<div class="container">
    <div class="header">
        <h1>🔥 Heat Pump Controller</h1>
        <p>ProEnergy Green SRL / ThermXpert - EO-AI4HP Project</p>
    </div>
    <div class="nav">
        <button onclick="showTab('dashboard')" class="active">Dashboard</button>
        <button onclick="showTab('commands')">Commands</button>
        <button onclick="showTab('logs')">Logs</button>
        <button onclick="showTab('sd')">SD Card</button>
        <button onclick="showTab('config')">Config</button>
        <button onclick="showTab('diagnostic')">Diagnostic</button>
    </div>
    <div class="content">
)=====";

const char HTML_FOOTER[] PROGMEM = R"=====(
    </div>
</div>
<script>
function showTab(tabName) {
    document.querySelectorAll('.tab-pane').forEach(el => el.classList.remove('active'));
    document.querySelectorAll('.nav button').forEach(el => el.classList.remove('active'));
    document.getElementById(tabName).classList.add('active');
    event.target.classList.add('active');
    
    if(tabName === 'dashboard') startAutoRefresh();
    else stopAutoRefresh();
    
    if(tabName === 'logs') loadLogs();
    if(tabName === 'sd') loadSDStatus();
}

let refreshInterval;
function startAutoRefresh() {
    refreshDashboard();
    refreshInterval = setInterval(refreshDashboard, 5000);
}
function stopAutoRefresh() {
    if(refreshInterval) clearInterval(refreshInterval);
}

async function refreshDashboard() {
    try {
        const response = await fetch('/api/status');
        const data = await response.json();
        
        document.getElementById('status').textContent = data.running ? 'RUNNING' : 'STOPPED';
        document.getElementById('status').className = 'card-value ' + (data.running ? 'status-ok' : 'status-error');
        
        document.getElementById('cop').textContent = data.performance.cop.toFixed(2);
        document.getElementById('t_amb').textContent = data.temperatures.ambient.toFixed(1);
        document.getElementById('t_out').textContent = data.temperatures.water_outlet.toFixed(1);
        document.getElementById('t_ret').textContent = data.temperatures.water_return.toFixed(1);
        document.getElementById('delta_t').textContent = data.temperatures.delta_t.toFixed(1);
        document.getElementById('power').textContent = data.performance.power_w;
        document.getElementById('thermal').textContent = data.performance.thermal_kw.toFixed(2);
        document.getElementById('comp_hz').textContent = data.performance.compressor_hz;
        document.getElementById('flow').textContent = data.performance.flow_lh;
        
        document.getElementById('fault').textContent = data.fault ? 'YES' : 'OK';
        document.getElementById('fault').className = 'card-value ' + (data.fault ? 'status-error' : 'status-ok');
        
        document.getElementById('uptime').textContent = Math.floor(data.system.uptime_sec / 3600) + 'h';
        document.getElementById('reads_ok').textContent = data.system.reads_ok;
        document.getElementById('reads_fail').textContent = data.system.reads_fail;
        
        const successRate = data.system.reads_ok / (data.system.reads_ok + data.system.reads_fail) * 100;
        document.getElementById('success_rate').textContent = successRate.toFixed(1) + '%';
        
    } catch(e) {
        console.error('Error refreshing dashboard:', e);
    }
}

async function sendCommand(cmd) {
    const output = document.getElementById('cmd-output');
    output.textContent += '\n> ' + cmd + '\n';
    
    try {
        const response = await fetch('/api/command', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'cmd=' + encodeURIComponent(cmd)
        });
        const result = await response.text();
        output.textContent += result + '\n';
        output.scrollTop = output.scrollHeight;
    } catch(e) {
        output.textContent += 'Error: ' + e.message + '\n';
    }
}

async function loadLogs() {
    try {
        const response = await fetch('/api/logs');
        const logs = await response.text();
        document.getElementById('logs-content').textContent = logs;
    } catch(e) {
        document.getElementById('logs-content').textContent = 'Error loading logs: ' + e.message;
    }
}

async function loadSDStatus() {
    try {
        const response = await fetch('/api/sd/status');
        const data = await response.json();
        
        let html = '<div class="alert alert-' + (data.available ? 'success' : 'danger') + '">';
        html += '<strong>SD Card Status:</strong> ' + (data.available ? 'Available' : 'Not Available');
        html += '</div>';
        
        if(data.available) {
            html += '<table><tr><th>Property</th><th>Value</th></tr>';
            html += '<tr><td>Card Size</td><td>' + data.card_size_mb + ' MB</td></tr>';
            html += '<tr><td>Used Space</td><td>' + data.used_mb + ' MB</td></tr>';
            html += '<tr><td>Free Space</td><td>' + data.free_mb + ' MB</td></tr>';
            html += '</table>';
            
            html += '<h3>Files:</h3><table><tr><th>Filename</th><th>Size</th><th>Action</th></tr>';
            data.files.forEach(file => {
                html += '<tr><td>' + file.name + '</td><td>' + file.size + ' bytes</td>';
                html += '<td><a href="/download?file=' + file.name + '" class="btn btn-primary btn-sm">Download</a></td></tr>';
            });
            html += '</table>';
        }
        
        document.getElementById('sd-content').innerHTML = html;
    } catch(e) {
        document.getElementById('sd-content').innerHTML = '<div class="alert alert-danger">Error: ' + e.message + '</div>';
    }
}

async function controlPump(action) {
    try {
        const response = await fetch('/api/control/' + action, {method: 'POST'});
        const result = await response.text();
        alert(result);
        refreshDashboard();
    } catch(e) {
        alert('Error: ' + e.message);
    }
}

async function runDiagnostic() {
    const output = document.getElementById('diag-output');
    output.textContent = 'Running diagnostic...\n';
    
    try {
        const response = await fetch('/api/diagnostic');
        const result = await response.text();
        output.textContent = result;
    } catch(e) {
        output.textContent = 'Error: ' + e.message;
    }
}

async function runAutoScan(quickScan) {
    const output = document.getElementById('diag-output');
    
    if (quickScan) {
        output.textContent = '🚀 QUICK AUTO-SCAN STARTED...\n\n';
        output.textContent += 'Testing most common configurations...\n';
        output.textContent += 'Estimated time: 30-60 seconds\n\n';
        output.textContent += 'Please wait...\n';
    } else {
        output.textContent = '🔬 DEEP AUTO-SCAN STARTED...\n\n';
        output.textContent += 'Testing ALL configurations (this will take several minutes!)\n';
        output.textContent += 'Estimated time: 5-15 minutes\n\n';
        output.textContent += '⚠️ DO NOT refresh the page!\n\n';
        output.textContent += 'Please wait...\n';
    }
    
    try {
        const url = '/api/autoscan?quick=' + (quickScan ? '1' : '0');
        const response = await fetch(url);
        const result = await response.text();
        output.textContent = result;
        
        // If success, show recommendation to update config.h
        if (result.includes('WORKING CONFIGURATION FOUND')) {
            output.textContent += '\n\n═══════════════════════════════════════════════════════════\n';
            output.textContent += '✅ NEXT STEP: Update your config.h with the values above!\n';
            output.textContent += '═══════════════════════════════════════════════════════════\n';
        }
    } catch(e) {
        output.textContent = 'Error: ' + e.message + '\n\n';
        output.textContent += 'Network error. Check WiFi connection and try again.';
    }
}

window.onload = () => {
    showTab('dashboard');
};
</script>
</body>
</html>
)=====";

// Setup web server routes
void setupWebServerRoutes() {
    // Main page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = String(HTML_HEADER);
        
        // Dashboard tab
        html += R"=====(
<div id="dashboard" class="tab-pane active">
    <div class="grid">
        <div class="card">
            <div class="card-title">Status</div>
            <div id="status" class="card-value status-error">STOPPED</div>
        </div>
        <div class="card">
            <div class="card-title">COP</div>
            <div class="card-value"><span id="cop">0.00</span></div>
        </div>
        <div class="card">
            <div class="card-title">T Outdoor</div>
            <div class="card-value"><span id="t_amb">0.0</span><span class="card-unit">°C</span></div>
        </div>
        <div class="card">
            <div class="card-title">T Water Out</div>
            <div class="card-value"><span id="t_out">0.0</span><span class="card-unit">°C</span></div>
        </div>
        <div class="card">
            <div class="card-title">T Water Return</div>
            <div class="card-value"><span id="t_ret">0.0</span><span class="card-unit">°C</span></div>
        </div>
        <div class="card">
            <div class="card-title">Delta-T</div>
            <div class="card-value"><span id="delta_t">0.0</span><span class="card-unit">°C</span></div>
        </div>
        <div class="card">
            <div class="card-title">Power</div>
            <div class="card-value"><span id="power">0</span><span class="card-unit">W</span></div>
        </div>
        <div class="card">
            <div class="card-title">Thermal Power</div>
            <div class="card-value"><span id="thermal">0.00</span><span class="card-unit">kW</span></div>
        </div>
        <div class="card">
            <div class="card-title">Compressor</div>
            <div class="card-value"><span id="comp_hz">0</span><span class="card-unit">Hz</span></div>
        </div>
        <div class="card">
            <div class="card-title">Flow</div>
            <div class="card-value"><span id="flow">0</span><span class="card-unit">L/h</span></div>
        </div>
        <div class="card">
            <div class="card-title">Fault</div>
            <div id="fault" class="card-value status-ok">OK</div>
        </div>
        <div class="card">
            <div class="card-title">Uptime</div>
            <div class="card-value"><span id="uptime">0h</span></div>
        </div>
    </div>
    
    <h3>Statistics</h3>
    <table>
        <tr><th>Metric</th><th>Value</th></tr>
        <tr><td>Successful Reads</td><td><span id="reads_ok">0</span></td></tr>
        <tr><td>Failed Reads</td><td><span id="reads_fail">0</span></td></tr>
        <tr><td>Success Rate</td><td><span id="success_rate">0%</span></td></tr>
    </table>
    
    <div style="text-align: center; margin-top: 30px;">
        <button class="btn btn-success" onclick="controlPump('start')">START Pump</button>
        <button class="btn btn-danger" onclick="controlPump('stop')">STOP Pump</button>
        <button class="btn btn-secondary" onclick="refreshDashboard()">Refresh Now</button>
    </div>
</div>
)=====";

        // Commands tab
        html += R"=====(
<div id="commands" class="tab-pane">
    <h2>Serial Monitor Commands</h2>
    <div class="alert alert-info">
        Execute commands remotely (same as Serial Monitor at 115200 baud)
    </div>
    
    <div class="input-group">
        <input type="text" id="cmd-input" placeholder="Enter command (e.g., 'modbus', 'status', 'help')" 
               onkeypress="if(event.key==='Enter') sendCommand(document.getElementById('cmd-input').value)">
        <button class="btn btn-primary" onclick="sendCommand(document.getElementById('cmd-input').value)">Send</button>
    </div>
    
    <div style="margin: 20px 0;">
        <button class="btn btn-secondary" onclick="sendCommand('help')">help</button>
        <button class="btn btn-secondary" onclick="sendCommand('modbus')">modbus</button>
        <button class="btn btn-secondary" onclick="sendCommand('status')">status</button>
        <button class="btn btn-secondary" onclick="sendCommand('stats')">stats</button>
        <button class="btn btn-secondary" onclick="sendCommand('read')">read</button>
        <button class="btn btn-secondary" onclick="sendCommand('modbusraw')">modbusraw</button>
    </div>
    
    <div class="log-box" id="cmd-output">Command output will appear here...</div>
    <button class="btn btn-secondary" onclick="document.getElementById('cmd-output').textContent=''">Clear Output</button>
</div>
)=====";

        // Logs tab
        html += R"=====(
<div id="logs" class="tab-pane">
    <h2>System Logs</h2>
    <div class="alert alert-info">
        Real-time system logs (last 200 entries)
    </div>
    <button class="btn btn-secondary" onclick="loadLogs()">Refresh Logs</button>
    <button class="btn btn-secondary" onclick="fetch('/api/logs/clear', {method:'POST'}).then(()=>loadLogs())">Clear Logs</button>
    <div class="log-box" id="logs-content">Loading...</div>
</div>
)=====";

        // SD Card tab
        html += R"=====(
<div id="sd" class="tab-pane">
    <h2>SD Card Manager</h2>
    <div id="sd-content">Loading...</div>
    <button class="btn btn-secondary" onclick="loadSDStatus()">Refresh</button>
</div>
)=====";

        // Config tab
        html += R"=====(
<div id="config" class="tab-pane">
    <h2>System Configuration</h2>
    <table>
        <tr><th>Parameter</th><th>Value</th></tr>
        <tr><td>RS485 TX Pin</td><td>GPIO )=====";
        html += String(RS485_TX_PIN) + R"=====(</td></tr>
        <tr><td>RS485 RX Pin</td><td>GPIO )=====";
        html += String(RS485_RX_PIN) + R"=====(</td></tr>
        <tr><td>RS485 DE Pin</td><td>GPIO )=====";
        html += String(RS485_DE_PIN) + R"=====(</td></tr>
        <tr><td>Modbus Slave ID</td><td>)=====";
        html += String(MODBUS_SLAVE_ID) + R"=====(</td></tr>
        <tr><td>Modbus Baudrate</td><td>)=====";
        html += String(MODBUS_BAUDRATE) + R"=====( bps</td></tr>
        <tr><td>Modbus Timeout</td><td>)=====";
        html += String(MODBUS_TIMEOUT_MS) + R"=====( ms</td></tr>
        <tr><td>WiFi SSID</td><td>)=====";
        html += String(WiFi.SSID()) + R"=====(</td></tr>
        <tr><td>IP Address</td><td>)=====";
        html += WiFi.localIP().toString() + R"=====(</td></tr>
        <tr><td>MAC Address</td><td>)=====";
        html += WiFi.macAddress() + R"=====(</td></tr>
        <tr><td>Free Heap</td><td>)=====";
        html += String(ESP.getFreeHeap() / 1024) + R"=====( KB</td></tr>
        <tr><td>CPU Temp</td><td>)=====";
        html += String(temperatureRead(), 1) + R"=====( °C</td></tr>
    </table>
</div>
)=====";

        // Diagnostic tab
        html += R"=====(
<div id="diagnostic" class="tab-pane">
    <h2>🔍 Modbus Auto-Scan & Diagnostic</h2>
    
    <div class="alert alert-info">
        <strong>✨ NEW! Auto-Scan Feature</strong><br>
        Automatically test ALL pin configurations, baudrates, and slave IDs to find working setup!
    </div>
    
    <h3>Option 1: Quick Auto-Scan (Recommended)</h3>
    <div class="alert alert-warning">
        <strong>⚡ Quick Mode</strong> - Tests most common configurations first (30-60 seconds)<br>
        Tests: Official pins (42/43/0,2,46,4,5) + 9600 baud + Slave IDs 1-16
    </div>
    <button class="btn btn-success" onclick="runAutoScan(true)">🚀 Quick Auto-Scan</button>
    
    <h3 style="margin-top: 30px;">Option 2: Deep Auto-Scan</h3>
    <div class="alert alert-danger">
        <strong>🔬 Deep Mode</strong> - Tests EVERYTHING (may take 5-15 minutes!)<br>
        Tests: All pins + All baudrates (9600-115200) + All slave IDs (1-247) + All timings
    </div>
    <button class="btn btn-danger" onclick="runAutoScan(false)">🔬 Deep Auto-Scan (Slow)</button>
    
    <h3 style="margin-top: 30px;">Option 3: Standard Diagnostic</h3>
    <div class="alert alert-warning">
        <strong>⚠️ Standard Mode</strong> - Quick test with current config.h settings<br>
        Tests: Current pins + Current baudrate + Slave IDs 1-16 only
    </div>
    <button class="btn btn-primary" onclick="runDiagnostic()">Run Standard Diagnostic</button>
    
    <h3 style="margin-top: 30px;">📊 Results:</h3>
    <div class="log-box" id="diag-output" style="margin-top: 20px;">
        Click one of the buttons above to start testing...
        
        <strong>Quick Auto-Scan:</strong> Fast, tests most common configs (RECOMMENDED)
        <strong>Deep Auto-Scan:</strong> Slow, tests everything (use if Quick fails)
        <strong>Standard Diagnostic:</strong> Fastest, tests current settings only
    </div>
</div>
)=====";

        html += String(HTML_FOOTER);
        request->send(200, "text/html", html);
    });
    
    // API: Status JSON
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = getStatusJSON();
        request->send(200, "application/json", json);
    });
    
    // API: Command execution
    server.on("/api/command", HTTP_POST, [](AsyncWebServerRequest *request) {
        String output = "";
        
        if (request->hasParam("cmd", true)) {
            String cmd = request->getParam("cmd", true)->value();
            cmd.trim();
            cmd.toLowerCase();
            
            // Execute command and capture output
            if (cmd == "help") {
                output = "Available commands:\n";
                output += "  modbus      - Scan Modbus slave IDs (1-16)\n";
                output += "  modbusraw   - Raw register dump\n";
                output += "  status      - System status\n";
                output += "  stats       - Statistics\n";
                output += "  read        - Force Modbus read\n";
                output += "  start       - Start pump\n";
                output += "  stop        - Stop pump\n";
                output += "  restart     - Restart ESP32\n";
            }
            else if (cmd == "modbus") {
                output = "Scanning Modbus slave IDs 1-16...\n\n";
                bool found = false;
                
                for (uint8_t id = 1; id <= 16; id++) {
                    output += "[" + String(id) + "/16] Slave ID " + String(id) + "... ";
                    
                    extern ModbusRTU modbus;
                    modbus.setSlaveId(id);
                    uint16_t testReg[2];
                    
                    if (modbus.readHoldingRegisters(0x0000, 2, testReg)) {
                        output += "✓ FOUND! Status=0x" + String(testReg[0], HEX) + "\n";
                        found = true;
                        break;
                    } else {
                        output += "No response (err=0x" + String(modbus.getLastError(), HEX) + ")\n";
                    }
                    delay(150);
                }
                
                if (!found) {
                    output += "\n✗ No device found\n";
                    output += "Check wiring and try swapping A+/B-\n";
                }
            }
            else if (cmd == "status") {
                output = "=== System Status ===\n";
                output += "Uptime: " + String(stats.uptime_sec) + " sec\n";
                output += "Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
                output += "WiFi: " + String(WiFi.SSID()) + " (" + WiFi.localIP().toString() + ")\n";
                output += "SD Card: " + String(sdCardAvailable ? "Available" : "Not Available") + "\n";
                output += "\nHeat Pump:\n";
                output += "  Running: " + String(hpData.running ? "YES" : "NO") + "\n";
                output += "  T_ambient: " + String(hpData.T_ambient, 1) + "°C\n";
                output += "  T_water_out: " + String(hpData.T_water_outlet, 1) + "°C\n";
                output += "  Power: " + String(hpData.power) + " W\n";
                output += "  COP: " + String(hpData.cop, 2) + "\n";
            }
            else if (cmd == "stats") {
                output = "=== Statistics ===\n";
                output += "Total reads: " + String(stats.total_reads) + "\n";
                output += "Successful: " + String(stats.successful_reads) + "\n";
                output += "Failed: " + String(stats.failed_reads) + "\n";
                float successRate = stats.total_reads > 0 ? 100.0 * stats.successful_reads / stats.total_reads : 0;
                output += "Success rate: " + String(successRate, 1) + "%\n";
            }
            else if (cmd == "restart") {
                output = "Restarting ESP32 in 2 seconds...";
                request->send(200, "text/plain", output);
                delay(2000);
                ESP.restart();
                return;
            }
            else {
                output = "Unknown command: " + cmd + "\nType 'help' for available commands";
            }
            
            addWebLog("Command executed: " + cmd);
        } else {
            output = "Error: No command specified";
        }
        
        request->send(200, "text/plain", output);
    });
    
    // API: Logs
    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", webLog.length() > 0 ? webLog : "No logs yet");
    });
    
    server.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        webLog = "";
        addWebLog("Logs cleared via web interface");
        request->send(200, "text/plain", "Logs cleared");
    });
    
    // API: SD Card status
    server.on("/api/sd/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["available"] = sdCardAvailable;
        
        if (sdCardAvailable) {
            doc["card_size_mb"] = SD.cardSize() / (1024 * 1024);
            doc["used_mb"] = SD.usedBytes() / (1024 * 1024);
            doc["free_mb"] = (SD.cardSize() - SD.usedBytes()) / (1024 * 1024);
            
            JsonArray files = doc["files"].to<JsonArray>();
            File root = SD.open("/");
            File file = root.openNextFile();
            while (file) {
                if (!file.isDirectory()) {
                    JsonObject fileObj = files.add<JsonObject>();
                    fileObj["name"] = String(file.name());
                    fileObj["size"] = file.size();
                }
                file = root.openNextFile();
            }
        }
        
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });
    
    // API: Control pump
    server.on("/api/control/start", HTTP_POST, [](AsyncWebServerRequest *request) {
        extern bool sendStartStopCommand(bool);
        bool success = sendStartStopCommand(true);
        addWebLog("START command sent: " + String(success ? "Success" : "Failed"));
        request->send(200, "text/plain", success ? "✓ START command sent" : "✗ Failed to send command");
    });
    
    server.on("/api/control/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        extern bool sendStartStopCommand(bool);
        bool success = sendStartStopCommand(false);
        addWebLog("STOP command sent: " + String(success ? "Success" : "Failed"));
        request->send(200, "text/plain", success ? "✓ STOP command sent" : "✗ Failed to send command");
    });
    
    // API: Diagnostic
    server.on("/api/diagnostic", HTTP_GET, [](AsyncWebServerRequest *request) {
        String output = "=== FULL DIAGNOSTIC REPORT ===\n\n";
        output += "Generated: " + String(millis() / 1000) + " seconds since boot\n\n";
        
        // Hardware info
        output += "--- HARDWARE ---\n";
        output += "Board: M5StampS3 PLC K141\n";
        output += "CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz\n";
        output += "Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
        output += "Total Heap: " + String(ESP.getHeapSize() / 1024) + " KB\n";
        output += "CPU Temp: " + String(temperatureRead(), 1) + " °C\n\n";
        
        // WiFi info
        output += "--- WIFI ---\n";
        output += "SSID: " + String(WiFi.SSID()) + "\n";
        output += "IP: " + WiFi.localIP().toString() + "\n";
        output += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
        output += "MAC: " + WiFi.macAddress() + "\n\n";
        
        // SD Card info
        output += "--- SD CARD ---\n";
        if (sdCardAvailable) {
            output += "Status: ✓ Available\n";
            output += "Size: " + String(SD.cardSize() / (1024 * 1024)) + " MB\n";
            output += "Used: " + String(SD.usedBytes() / (1024 * 1024)) + " MB\n";
            output += "Free: " + String((SD.cardSize() - SD.usedBytes()) / (1024 * 1024)) + " MB\n";
        } else {
            output += "Status: ✗ Not Available\n";
        }
        output += "\n";
        
        // Modbus configuration
        output += "--- MODBUS CONFIG ---\n";
        output += "TX Pin: GPIO " + String(RS485_TX_PIN) + "\n";
        output += "RX Pin: GPIO " + String(RS485_RX_PIN) + "\n";
        output += "DE Pin: GPIO " + String(RS485_DE_PIN) + "\n";
        output += "Baudrate: " + String(MODBUS_BAUDRATE) + " bps\n";
        output += "Slave ID: " + String(MODBUS_SLAVE_ID) + "\n";
        output += "Timeout: " + String(MODBUS_TIMEOUT_MS) + " ms\n\n";
        
        // Statistics
        output += "--- STATISTICS ---\n";
        output += "Uptime: " + String(stats.uptime_sec / 3600) + " hours\n";
        output += "Total Reads: " + String(stats.total_reads) + "\n";
        output += "Successful: " + String(stats.successful_reads) + "\n";
        output += "Failed: " + String(stats.failed_reads) + "\n";
        float successRate = stats.total_reads > 0 ? 100.0 * stats.successful_reads / stats.total_reads : 0;
        output += "Success Rate: " + String(successRate, 1) + "%\n\n";
        
        // Modbus scan
        output += "--- MODBUS SCAN ---\n";
        output += "Scanning slave IDs 1-16...\n";
        
        extern ModbusRTU modbus;
        bool found = false;
        
        for (uint8_t id = 1; id <= 16; id++) {
            modbus.setSlaveId(id);
            uint16_t testReg[2];
            
            output += "[" + String(id) + "/16] Slave ID " + String(id) + "... ";
            
            if (modbus.readHoldingRegisters(0x0000, 2, testReg)) {
                output += "✓ FOUND! Status=0x" + String(testReg[0], HEX) + "\n";
                found = true;
            } else {
                output += "No response (err=0x" + String(modbus.getLastError(), HEX) + ")\n";
            }
            
            delay(150);
        }
        
        if (!found) {
            output += "\n✗ NO DEVICE FOUND\n";
            output += "\nTROUBLESHOOTING:\n";
            output += "1. Check pump is POWERED ON\n";
            output += "2. Verify RS485 wiring: A+↔A+, B-↔B-, GND↔GND\n";
            output += "3. Install 120Ω termination resistor\n";
            output += "4. Try swapping A+ with B-\n";
            output += "5. Check Modbus enabled in pump menu\n";
        }
        
        output += "\n=== END DIAGNOSTIC ===\n";
        
        addWebLog("Full diagnostic executed");
        request->send(200, "text/plain", output);
    });
    
    // API: Auto-Scan Modbus Configuration
    server.on("/api/autoscan", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool quickScan = false;
        if (request->hasParam("quick")) {
            quickScan = (request->getParam("quick")->value() == "1" || 
                        request->getParam("quick")->value() == "true");
        }
        
        addWebLog("Auto-scan started (quick=" + String(quickScan ? "true" : "false") + ")");
        
        String result = runAutoScan(quickScan);
        
        addWebLog("Auto-scan completed");
        request->send(200, "text/plain", result);
    });
    
    // File download
    server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("file")) {
            String filename = request->getParam("file")->value();
            if (SD.exists(filename)) {
                request->send(SD, filename, "application/octet-stream");
                addWebLog("File downloaded: " + filename);
            } else {
                request->send(404, "text/plain", "File not found");
            }
        } else {
            request->send(400, "text/plain", "Missing file parameter");
        }
    });
    
    addWebLog("Web server routes configured");
}

#endif // WEBSERVER_H
