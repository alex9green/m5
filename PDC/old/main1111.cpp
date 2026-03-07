/**
 * @file main.cpp
 * @brief M5Stamp PLC K141 Heat Pump Controller - Main Application
 * 
 * Hardware: M5StampS3 PLC Controller (Model K141)
 * Project: EO-AI4HP - ProEnergy Green SRL / ThermXpert
 * 
 * Features:
 * - MODBUS RTU communication with Micoe heat pump
 * - Weather compensated temperature control
 * - SD card data logging (CSV + JSON)
 * - WiFi web server for monitoring
 * - OTA updates
 * - Real-time COP calculation
 * 
 * @author ProEnergy Green SRL
 * @date 2026-03-06
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "ModbusRTU.h"

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

// Hardware Serial for MODBUS RS485
HardwareSerial RS485Serial(1);

// MODBUS Client
ModbusRTU modbus(RS485Serial, MODBUS_SLAVE_ID, MODBUS_TIMEOUT_MS);

// SPI for SD Card
SPIClass spiSD(HSPI);

// Web Server
AsyncWebServer server(WEB_SERVER_PORT);

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_OFFSET_SEC, NTP_UPDATE_INTERVAL);

// Data structures
HeatPumpData hpData;
ControlConfig controlConfig;
SystemStats stats;

// State variables
unsigned long lastReadTime = 0;
unsigned long lastControlTime = 0;
unsigned long lastSDWriteTime = 0;
unsigned long lastStateChange = 0;
unsigned long bootTime = 0;
bool sdCardAvailable = false;
bool wifiConnected = false;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void setupSerial();
void setupModbus();
void setupSDCard();
void setupWiFi();
void setupOTA();
void setupWebServer();
void setupWatchdog();

bool readAllModbusData();
void calculateDerivedValues();
void controlLoop();
void logDataToSD();
void printStatus();

float calculateWeatherCurveTarget(float T_outdoor, float T_indoor_setpoint);
bool sendStartStopCommand(bool start);
bool setTemperatureSetpoint(uint8_t mode, float temperature);

String getStatusJSON();
String getStatusHTML();

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Record boot time
    bootTime = millis();
    
    // Initialize M5 (if using M5Unified lib)
    M5.begin();
    
    // Setup Serial Console
    setupSerial();
    
    Serial.println("\n" + String('=', 80));
    Serial.println("M5Stamp PLC K141 - Heat Pump Controller");
    Serial.println("ProEnergy Green SRL / ThermXpert");
    Serial.println("EO-AI4HP Project");
    Serial.println(String('=', 80) + "\n");
    
    // Setup LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    // Setup MODBUS
    setupModbus();
    delay(500);
    
    // Setup SD Card
    setupSDCard();
    delay(500);
    
    // Setup WiFi (optional)
    #if ENABLE_WIFI
    setupWiFi();
    #endif
    
    // Setup OTA Updates
    #if ENABLE_OTA
    if (wifiConnected) {
        setupOTA();
    }
    #endif
    
    // Setup Web Server
    #if ENABLE_WEB_SERVER
    if (wifiConnected) {
        setupWebServer();
        server.begin();
        Serial.println("✓ Web server started on port " + String(WEB_SERVER_PORT));
    }
    #endif
    
    // Setup Watchdog Timer
    setupWatchdog();
    
    // Initialize NTP
    if (wifiConnected) {
        timeClient.begin();
        timeClient.update();
        Serial.println("✓ NTP time synchronized");
    }
    
    Serial.println("\n🚀 SYSTEM READY - Starting main loop...\n");
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    unsigned long now = millis();
    
    // Handle Serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toLowerCase();
        
        if (cmd == "help") {
            Serial.println("\n📋 AVAILABLE COMMANDS:");
            Serial.println("  scan        - Scan WiFi networks");
            Serial.println("  modbus      - Test MODBUS (auto-detect slave ID)");
            Serial.println("  modbusraw   - Raw MODBUS register dump (0x0000-0x005D)");
            Serial.println("  status      - Show detailed system status");
            Serial.println("  stats       - Show statistics");
            Serial.println("  read        - Force MODBUS read now");
            Serial.println("  start       - Send START command to pump");
            Serial.println("  stop        - Send STOP command to pump");
            Serial.println("  restart     - Restart ESP32");
            Serial.println("  wifi SSID PASSWORD - Connect to WiFi");
            Serial.println();
        }
        else if (cmd == "scan") {
            Serial.println("\n=== WiFi Scan ===");
            int n = WiFi.scanNetworks();
            Serial.printf("Found %d networks:\n", n);
            for (int i = 0; i < n; i++) {
                Serial.printf("  %d: %-32s %4d dBm  Ch:%2d  %s\n", 
                             i + 1, 
                             WiFi.SSID(i).c_str(), 
                             WiFi.RSSI(i),
                             WiFi.channel(i),
                             WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Encrypted");
            }
            Serial.println();
        }
        else if (cmd == "modbus") {
            Serial.println("\n=== MODBUS Manual Test ===");
            Serial.println("Scanning slave IDs 1-16...\n");
            
            bool found = false;
            for (uint8_t id = 1; id <= 16; id++) {
                modbus.setSlaveId(id);
                uint16_t testReg[2];
                
                Serial.printf("[%2d/16] Slave ID %2d... ", id, id);
                Serial.flush();
                
                if (modbus.readHoldingRegisters(0x0000, 2, testReg)) {
                    Serial.printf("✓ FOUND! Status1=0x%04X Status2=0x%04X\n", testReg[0], testReg[1]);
                    found = true;
                    
                    // Try reading temperature
                    delay(100);
                    if (modbus.readHoldingRegisters(REG_TEMP_AMBIENT, 1, testReg)) {
                        Serial.printf("        T_ambient: %.1f°C\n", DECODE_TEMP(testReg[0]));
                    }
                } else {
                    uint8_t err = modbus.getLastError();
                    Serial.printf("No response (err=0x%02X)\n", err);
                }
                
                delay(150);
            }
            
            if (!found) {
                Serial.println("\n✗ No device found");
                Serial.println("Check wiring and try swapping A+/B-");
            }
            Serial.println();
        }
        else if (cmd == "modbusraw") {
            Serial.println("\n=== MODBUS Raw Register Dump ===\n");
            
            struct RegBlock {
                uint16_t start;
                uint16_t count;
                const char* name;
            };
            
            RegBlock blocks[] = {
                {0x0000, 5, "Status (0x0000-0x0004)"},
                {0x0021, 1, "Fault Code (0x0021)"},
                {0x0040, 8, "Operating Params (0x0040-0x0047)"},
                {0x0048, 2, "Pressure Sat (0x0048-0x0049)"},
                {0x004A, 11, "Temperatures (0x004A-0x0054)"},
                {0x0057, 7, "Flow/Power (0x0057-0x005D)"},
                {0x0300, 6, "Setpoints (0x0300-0x0305)"}
            };
            
            for (auto& block : blocks) {
                Serial.printf("%s:\n", block.name);
                uint16_t regs[16];
                
                if (modbus.readHoldingRegisters(block.start, block.count, regs)) {
                    for (uint16_t i = 0; i < block.count; i++) {
                        Serial.printf("  0x%04X: 0x%04X (%5d)\n", 
                                     block.start + i, regs[i], (int16_t)regs[i]);
                    }
                } else {
                    Serial.println("  ✗ Read failed");
                }
                Serial.println();
                delay(100);
            }
        }
        else if (cmd == "status") {
            Serial.println("\n" + String('=', 80));
            Serial.println("SYSTEM STATUS");
            Serial.println(String('=', 80));
            
            // System info
            Serial.printf("Uptime: %lu seconds (%.1f hours)\n", 
                         stats.uptime_sec, stats.uptime_sec / 3600.0);
            Serial.printf("Free Heap: %d KB / %d KB\n", 
                         ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024);
            Serial.printf("CPU Temp: %.1f°C\n", temperatureRead());
            
            // WiFi
            Serial.println("\nWiFi:");
            Serial.printf("  Status: %s\n", wifiConnected ? "Connected" : "Disconnected");
            if (wifiConnected) {
                Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
                Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
                Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
                Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
            }
            
            // SD Card
            Serial.println("\nSD Card:");
            Serial.printf("  Status: %s\n", sdCardAvailable ? "Available" : "Not available");
            
            // MODBUS
            Serial.println("\nMODBUS:");
            Serial.printf("  Slave ID: %d\n", MODBUS_SLAVE_ID);
            Serial.printf("  Baudrate: %d\n", MODBUS_BAUDRATE);
            Serial.printf("  Last read: %s\n", hpData.read_success ? "Success" : "Failed");
            
            // Heat Pump Status
            if (hpData.read_success) {
                Serial.println("\nHeat Pump:");
                Serial.printf("  Running: %s\n", hpData.running ? "YES" : "NO");
                Serial.printf("  Mode: %d\n", hpData.mode);
                Serial.printf("  T_ambient: %.1f°C\n", hpData.T_ambient);
                Serial.printf("  T_water_out: %.1f°C\n", hpData.T_water_outlet);
                Serial.printf("  T_water_ret: %.1f°C\n", hpData.T_water_return);
                Serial.printf("  Power: %d W\n", hpData.power);
                Serial.printf("  COP: %.2f\n", hpData.cop);
                Serial.printf("  Fault: %s (0x%04X)\n", 
                             hpData.has_fault ? "YES" : "NO", hpData.fault_code);
            }
            
            Serial.println(String('=', 80) + "\n");
        }
        else if (cmd == "stats") {
            Serial.println("\n=== Statistics ===");
            Serial.printf("Total reads: %lu\n", stats.total_reads);
            Serial.printf("Successful: %lu (%.1f%%)\n", 
                         stats.successful_reads,
                         stats.total_reads > 0 ? 100.0 * stats.successful_reads / stats.total_reads : 0);
            Serial.printf("Failed: %lu\n", stats.failed_reads);
            Serial.printf("MODBUS errors: %lu\n", stats.modbus_errors);
            Serial.printf("SD write errors: %lu\n", stats.sd_write_errors);
            Serial.println();
        }
        else if (cmd == "read") {
            Serial.println("\n=== Force MODBUS Read ===");
            bool success = readAllModbusData();
            Serial.printf("Result: %s\n", success ? "SUCCESS" : "FAILED");
            if (success) {
                calculateDerivedValues();
                Serial.printf("COP: %.2f\n", hpData.cop);
                Serial.printf("Power: %d W\n", hpData.power);
                Serial.printf("T_out: %.1f°C\n", hpData.T_water_outlet);
            }
            Serial.println();
        }
        else if (cmd == "start") {
            Serial.println("\nSending START command...");
            bool success = sendStartStopCommand(true);
            Serial.printf("Result: %s\n\n", success ? "✓ Command sent" : "✗ Failed");
        }
        else if (cmd == "stop") {
            Serial.println("\nSending STOP command...");
            bool success = sendStartStopCommand(false);
            Serial.printf("Result: %s\n\n", success ? "✓ Command sent" : "✗ Failed");
        }
        else if (cmd == "restart") {
            Serial.println("\n🔄 Restarting ESP32 in 2 seconds...\n");
            delay(2000);
            ESP.restart();
        }
        else if (cmd.startsWith("wifi ")) {
            int firstSpace = cmd.indexOf(' ');
            int secondSpace = cmd.indexOf(' ', firstSpace + 1);
            
            if (secondSpace > 0) {
                String newSSID = cmd.substring(firstSpace + 1, secondSpace);
                String newPass = cmd.substring(secondSpace + 1);
                
                Serial.printf("\nConnecting to: %s\n", newSSID.c_str());
                WiFi.disconnect();
                WiFi.begin(newSSID.c_str(), newPass.c_str());
                
                int attempts = 0;
                while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                    delay(500);
                    Serial.print(".");
                    attempts++;
                }
                
                if (WiFi.status() == WL_CONNECTED) {
                    wifiConnected = true;
                    Serial.println("\n✓ Connected!");
                    Serial.printf("IP: %s\n\n", WiFi.localIP().toString().c_str());
                } else {
                    wifiConnected = false;
                    Serial.println("\n✗ Connection failed\n");
                }
            } else {
                Serial.println("\nUsage: wifi SSID PASSWORD\n");
            }
        }
        else {
            Serial.println("\nUnknown command. Type 'help' for list of commands.\n");
        }
    }
    
    // Update watchdog
    esp_task_wdt_reset();
    
    // WiFi auto-reconnect
    #if ENABLE_WIFI
    static unsigned long lastWiFiCheck = 0;
    if (now - lastWiFiCheck > 30000) {  // Check every 30 seconds
        lastWiFiCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("⚠ WiFi disconnected - attempting reconnect...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                wifiConnected = true;
                Serial.println("\n✓ WiFi reconnected: " + WiFi.localIP().toString());
            } else {
                wifiConnected = false;
                Serial.println("\n✗ WiFi reconnect failed");
            }
        }
    }
    #endif
    
    // Update OTA
    #if ENABLE_OTA
    ArduinoOTA.handle();
    #endif
    
    // Update NTP periodically
    static unsigned long lastNTPUpdate = 0;
    if (wifiConnected && now - lastNTPUpdate > 3600000) {  // Every hour
        timeClient.update();
        lastNTPUpdate = now;
    }
    
    // Determine logging interval (fast during transitions/faults)
    uint16_t interval = LOG_INTERVAL_SEC * 1000;
    if (hpData.has_fault || abs(hpData.delta_t_water) > 3.0) {
        interval = LOG_INTERVAL_FAST_SEC * 1000;
    }
    
    // Main data acquisition and control loop
    if (now - lastReadTime >= interval) {
        lastReadTime = now;
        
        // Blink LED to show activity
        digitalWrite(LED_PIN, HIGH);
        
        // 1. Read all MODBUS data
        bool readSuccess = readAllModbusData();
        
        if (readSuccess) {
            stats.successful_reads++;
            
            // 2. Calculate derived values (COP, thermal power, etc.)
            calculateDerivedValues();
            
            // 3. Control loop (weather curve + hysteresis)
            if (controlConfig.enabled) {
                controlLoop();
            }
            
            // 4. Log data to SD card
            if (sdCardAvailable) {
                logDataToSD();
            }
            
            // 5. Print status to serial (every 10 reads)
            if (stats.total_reads % 10 == 0) {
                printStatus();
            }
            
        } else {
            stats.failed_reads++;
            Serial.println("⚠ MODBUS read failed");
        }
        
        digitalWrite(LED_PIN, LOW);
    }
    
    // Small delay to prevent tight loop
    delay(100);
}

// ============================================================================
// SETUP FUNCTIONS
// ============================================================================

void setupSerial() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n" + String('=', 80));
    Serial.println("  M5Stamp PLC K141 - Heat Pump MODBUS Controller");
    Serial.println("  ProEnergy Green SRL / ThermXpert - EO-AI4HP Project");
    Serial.println(String('=', 80));
    Serial.printf("Build Date: %s %s\n", __DATE__, __TIME__);
    Serial.printf("ESP32 Chip: Rev %d, %d cores\n", ESP.getChipRevision(), ESP.getChipCores());
    Serial.printf("CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Free Heap: %d KB\n", ESP.getFreeHeap() / 1024);
    Serial.printf("PSRAM: %d KB\n", ESP.getPsramSize() / 1024);
    Serial.println(String('=', 80));
    
    Serial.println("\n📋 SERIAL COMMANDS AVAILABLE:");
    Serial.println("  scan        - Scan WiFi networks");
    Serial.println("  modbus      - Test MODBUS (scan slave IDs 1-16)");
    Serial.println("  modbusraw   - Raw MODBUS register dump");
    Serial.println("  status      - Show detailed system status");
    Serial.println("  stats       - Show statistics");
    Serial.println("  read        - Force MODBUS read now");
    Serial.println("  start       - Send START command to pump");
    Serial.println("  stop        - Send STOP command to pump");
    Serial.println("  restart     - Restart ESP32");
    Serial.println("  help        - Show this menu");
    Serial.println("  wifi SSID PASSWORD - Connect to different WiFi");
    Serial.println(String('-', 80) + "\n");
}

void setupModbus() {
    Serial.println("\n[MODBUS] ===== INITIALIZATION =====");
    Serial.printf("[MODBUS] TX Pin: GPIO %d\n", RS485_TX_PIN);
    Serial.printf("[MODBUS] RX Pin: GPIO %d\n", RS485_RX_PIN);
    Serial.printf("[MODBUS] Baudrate: %d bps\n", MODBUS_BAUDRATE);
    Serial.printf("[MODBUS] Configured Slave ID: %d\n", MODBUS_SLAVE_ID);
    Serial.printf("[MODBUS] Timeout: %d ms\n", MODBUS_TIMEOUT_MS);
    Serial.println("[MODBUS] Protocol: RTU (8N1, CRC16)");
    
    // Initialize UART
    Serial.println("[MODBUS] Initializing UART...");
    modbus.begin(MODBUS_BAUDRATE, RS485_RX_PIN, RS485_TX_PIN);
    delay(500);
    Serial.println("[MODBUS] ✓ UART initialized");
    
    // Auto-detect slave ID
    Serial.println("\n[MODBUS] ===== AUTO-DETECTION =====");
    Serial.println("[MODBUS] Scanning for devices (slave ID 1-16)...");
    Serial.println("[MODBUS] This may take 20-30 seconds...\n");
    
    bool deviceFound = false;
    uint8_t foundSlaveId = 0;
    uint16_t foundStatus = 0;
    
    for (uint8_t testID = 1; testID <= 16; testID++) {
        modbus.setSlaveId(testID);
        uint16_t testReg[1];
        
        Serial.printf("[MODBUS] [%2d/16] Testing slave ID %2d... ", testID, testID);
        Serial.flush();
        
        // Try reading status register (0x0000)
        bool result = modbus.readHoldingRegisters(REG_STATUS_1, 1, testReg);
        
        if (result) {
            Serial.printf("✓ FOUND! Status: 0x%04X", testReg[0]);
            
            // Decode status bits
            if (testReg[0] & STATUS_BIT_RUNNING) Serial.print(" [RUNNING]");
            if (testReg[0] & STATUS_BIT_FAULT) Serial.print(" [FAULT]");
            if (testReg[0] & STATUS_BIT_DEFROST) Serial.print(" [DEFROST]");
            Serial.println();
            
            deviceFound = true;
            foundSlaveId = testID;
            foundStatus = testReg[0];
            
            // Try reading a temperature to confirm it's really a Micoe heat pump
            uint16_t tempReg[1];
            delay(150);
            if (modbus.readHoldingRegisters(REG_TEMP_AMBIENT, 1, tempReg)) {
                float temp = DECODE_TEMP(tempReg[0]);
                Serial.printf("[MODBUS]          → Ambient temp: %.1f°C (confirms heat pump!)\n", temp);
            }
            
            break;  // Stop after first device found
            
        } else {
            Serial.println("No response");
            
            // Show error code
            uint8_t err = modbus.getLastError();
            if (err != 0) {
                Serial.printf("[MODBUS]          Error code: 0x%02X ", err);
                switch(err) {
                    case 0xFF: Serial.println("(Timeout)"); break;
                    case 0xFE: Serial.println("(Invalid response)"); break;
                    case 0xFD: Serial.println("(CRC error)"); break;
                    case 0xFC: Serial.println("(Wrong slave ID)"); break;
                    default: Serial.printf("(MODBUS exception %d)\n", err); break;
                }
            }
        }
        
        delay(150);  // Delay between attempts
    }
    
    Serial.println();
    Serial.println("[MODBUS] ===== SCAN COMPLETE =====");
    
    if (deviceFound) {
        Serial.println("[MODBUS] ✓✓✓ DEVICE FOUND ✓✓✓");
        Serial.printf("[MODBUS] Detected slave ID: %d\n", foundSlaveId);
        Serial.printf("[MODBUS] Initial status: 0x%04X\n", foundStatus);
        
        if (foundSlaveId != MODBUS_SLAVE_ID) {
            Serial.println("[MODBUS] ⚠⚠⚠ WARNING ⚠⚠⚠");
            Serial.printf("[MODBUS] Device found at ID %d, but config.h uses ID %d\n", 
                         foundSlaveId, MODBUS_SLAVE_ID);
            Serial.println("[MODBUS] SOLUTION:");
            Serial.println("[MODBUS]   1. Update config.h: #define MODBUS_SLAVE_ID " + String(foundSlaveId));
            Serial.println("[MODBUS]   2. Recompile and upload");
            Serial.println("[MODBUS]   OR change pump's slave ID to " + String(MODBUS_SLAVE_ID));
            
            // Use found ID for now
            modbus.setSlaveId(foundSlaveId);
            Serial.printf("[MODBUS] Using slave ID %d for this session\n", foundSlaveId);
        } else {
            Serial.println("[MODBUS] ✓ Slave ID matches configuration");
            modbus.setSlaveId(MODBUS_SLAVE_ID);
        }
        
        // Test reading multiple registers
        Serial.println("\n[MODBUS] ===== EXTENDED TEST =====");
        Serial.println("[MODBUS] Reading multiple register blocks...\n");
        
        uint16_t regs[16];
        
        // Test temperatures
        Serial.print("[MODBUS] Reading temperatures (0x004A-0x0054)... ");
        if (modbus.readHoldingRegisters(REG_TEMP_AMBIENT, 11, regs)) {
            Serial.println("✓ OK");
            Serial.printf("[MODBUS]   T_ambient: %.1f°C\n", DECODE_TEMP(regs[0]));
            Serial.printf("[MODBUS]   T_water_return: %.1f°C\n", DECODE_TEMP(regs[5]));
            Serial.printf("[MODBUS]   T_water_outlet: %.1f°C\n", DECODE_TEMP(regs[6]));
        } else {
            Serial.println("✗ FAILED");
        }
        
        delay(150);
        
        // Test operating parameters
        Serial.print("[MODBUS] Reading operating params (0x0040-0x0047)... ");
        if (modbus.readHoldingRegisters(REG_COMP_FREQUENCY, 8, regs)) {
            Serial.println("✓ OK");
            Serial.printf("[MODBUS]   Compressor freq: %d Hz\n", regs[0]);
            Serial.printf("[MODBUS]   Fan freq: %d Hz\n", regs[1]);
        } else {
            Serial.println("✗ FAILED");
        }
        
        delay(150);
        
        // Test power
        Serial.print("[MODBUS] Reading power (0x005C)... ");
        if (modbus.readHoldingRegisters(REG_POWER_UNIT, 1, regs)) {
            Serial.printf("✓ OK (%d W)\n", regs[0]);
        } else {
            Serial.println("✗ FAILED");
        }
        
        Serial.println("\n[MODBUS] ✓✓✓ COMMUNICATION ESTABLISHED ✓✓✓");
        Serial.println("[MODBUS] Ready for normal operation\n");
        
    } else {
        Serial.println("[MODBUS] ✗✗✗ NO DEVICE FOUND ✗✗✗");
        Serial.println("[MODBUS] TROUBLESHOOTING:");
        Serial.println("[MODBUS]   1. Check RS485 wiring:");
        Serial.println("[MODBUS]      - M5Stamp A+ → Pump A+ (or D+)");
        Serial.println("[MODBUS]      - M5Stamp B- → Pump B- (or D-)");
        Serial.println("[MODBUS]      - M5Stamp GND → Pump GND");
        Serial.println("[MODBUS]   2. If no response, try SWAPPING A+ and B-");
        Serial.println("[MODBUS]   3. Verify pump is powered ON");
        Serial.println("[MODBUS]   4. Check pump MODBUS is enabled (menu settings)");
        Serial.println("[MODBUS]   5. Verify baudrate is 9600 (default for Micoe)");
        Serial.println("[MODBUS]   6. Check cable length < 100m");
        Serial.println("[MODBUS]   7. Try 'modbus' command in Serial Monitor");
        Serial.println();
    }
    
    Serial.println("[MODBUS] ===== END INITIALIZATION =====\n");
}

void setupSDCard() {
    Serial.println("[INIT] Initializing SD Card...");
    
    spiSD.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    if (SD.begin(SD_CS_PIN, spiSD)) {
        sdCardAvailable = true;
        
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("✓ SD Card mounted: %llu MB\n", cardSize);
        
        // Create log files if they don't exist
        if (!SD.exists(LOG_FILE_CSV)) {
            File file = SD.open(LOG_FILE_CSV, FILE_WRITE);
            if (file) {
                // Write CSV header
                file.println("timestamp,T_amb,T_ret,T_out,T_exh,comp_hz,fan_hz,flow,power,cop,thermal_kw,delta_t,running,fault");
                file.close();
                Serial.println("✓ Created CSV log file");
            }
        }
        
    } else {
        Serial.println("✗ SD Card mount failed!");
        sdCardAvailable = false;
    }
}

void setupWiFi() {
    Serial.println("[INIT] WiFi Setup...");
    Serial.printf("SSID: %s\n", WIFI_SSID);
    
    // Scan pentru rețele disponibile
    Serial.println("Scanning WiFi networks...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks:\n", n);
    
    bool ssidFound = false;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String encryption = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted";
        
        Serial.printf("  %d: %s (%d dBm) %s", i + 1, ssid.c_str(), rssi, encryption.c_str());
        
        if (ssid == WIFI_SSID) {
            ssidFound = true;
            Serial.print(" <- TARGET NETWORK!");
        }
        Serial.println();
    }
    
    if (!ssidFound) {
        Serial.println("\n⚠ WARNING: Target network NOT FOUND in scan!");
        Serial.printf("Looking for: '%s'\n", WIFI_SSID);
        Serial.println("Please check:");
        Serial.println("  1. SSID spelling (case-sensitive!)");
        Serial.println("  2. Router is powered on and broadcasting");
        Serial.println("  3. Router is on 2.4 GHz (ESP32 doesn't support 5 GHz)");
        Serial.println("  4. Device is in range of router\n");
    }
    
    // Încearcă conectarea
    Serial.println("Attempting connection...");
    
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    
    // Setări WiFi optimizate
    WiFi.setSleep(false);  // Disable WiFi sleep
    WiFi.setAutoReconnect(true);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    int dots = 0;
    
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
        dots++;
        if (dots % 40 == 0) Serial.println();
        
        // Debug status
        if (dots % 10 == 0) {
            wl_status_t status = WiFi.status();
            Serial.printf("\n[Status: ");
            switch(status) {
                case WL_IDLE_STATUS: Serial.print("IDLE"); break;
                case WL_NO_SSID_AVAIL: Serial.print("NO_SSID"); break;
                case WL_SCAN_COMPLETED: Serial.print("SCAN_DONE"); break;
                case WL_CONNECTED: Serial.print("CONNECTED"); break;
                case WL_CONNECT_FAILED: Serial.print("FAILED"); break;
                case WL_CONNECTION_LOST: Serial.print("LOST"); break;
                case WL_DISCONNECTED: Serial.print("DISCONNECTED"); break;
                default: Serial.printf("UNKNOWN(%d)", status); break;
            }
            Serial.print("] ");
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\n✓ WiFi connected!");
        Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("   Hostname: %s\n", WIFI_HOSTNAME);
        Serial.printf("   Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("   DNS: %s\n", WiFi.dnsIP().toString().c_str());
        
        // Start mDNS
        if (MDNS.begin(WIFI_HOSTNAME)) {
            MDNS.addService("http", "tcp", WEB_SERVER_PORT);
            Serial.printf("   mDNS: http://%s.local\n", WIFI_HOSTNAME);
        }
    } else {
        Serial.println("\n✗ WiFi connection timeout");
        Serial.printf("   Final status: %d\n", WiFi.status());
        wifiConnected = false;
        
        // Reîncearcă cu static IP (debug)
        Serial.println("\nTrying with static IP configuration...");
        IPAddress local_IP(192, 168, 0, 100);  // Schimbă dacă e necesar
        IPAddress gateway(192, 168, 0, 1);
        IPAddress subnet(255, 255, 255, 0);
        
        if (WiFi.config(local_IP, gateway, subnet)) {
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            
            startTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000) {
                delay(500);
                Serial.print(".");
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                wifiConnected = true;
                Serial.println("\n✓ WiFi connected with static IP!");
                Serial.printf("   IP: %s\n", WiFi.localIP().toString().c_str());
            } else {
                Serial.println("\n✗ Static IP connection also failed");
            }
        }
    }
    
    Serial.println();
}

void setupOTA() {
    Serial.println("[INIT] Setting up OTA updates...");
    
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setPort(OTA_PORT);
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("OTA Update Start: " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\nOTA Update Complete");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    
    ArduinoOTA.begin();
    Serial.println("✓ OTA ready");
}

void setupWebServer() {
    Serial.println("[INIT] Setting up web server...");
    
    // Root page - HTML dashboard
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", getStatusHTML());
    });
    
    // API endpoint - JSON data
    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", getStatusJSON());
    });
    
    // Control API - Start/Stop
    server.on("/api/control/start", HTTP_POST, [](AsyncWebServerRequest *request){
        bool success = sendStartStopCommand(true);
        request->send(200, "application/json", success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    });
    
    server.on("/api/control/stop", HTTP_POST, [](AsyncWebServerRequest *request){
        bool success = sendStartStopCommand(false);
        request->send(200, "application/json", success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    });
    
    // Set temperature setpoint
    server.on("/api/control/setpoint", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("temp", true) && request->hasParam("mode", true)) {
            float temp = request->getParam("temp", true)->value().toFloat();
            uint8_t mode = request->getParam("mode", true)->value().toInt();
            bool success = setTemperatureSetpoint(mode, temp);
            request->send(200, "application/json", success ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
        } else {
            request->send(400, "application/json", "{\"status\":\"bad_request\"}");
        }
    });
    
    // Download CSV log
    server.on("/download/csv", HTTP_GET, [](AsyncWebServerRequest *request){
        if (sdCardAvailable && SD.exists(LOG_FILE_CSV)) {
            request->send(SD, LOG_FILE_CSV, "text/csv", true);
        } else {
            request->send(404, "text/plain", "File not found");
        }
    });
}

void setupWatchdog() {
    Serial.println("[INIT] Setting up watchdog timer...");
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);
    Serial.printf("✓ Watchdog timer: %d seconds\n", WATCHDOG_TIMEOUT_SEC);
}

// ============================================================================
// MODBUS DATA ACQUISITION
// ============================================================================

bool readAllModbusData() {
    stats.total_reads++;
    
    bool success = true;
    uint16_t regs[32];  // Buffer for MODBUS registers
    
    // Enable detailed logging every 10 reads
    bool verbose = (stats.total_reads % 10 == 1);
    
    if (verbose) Serial.println("\n[MODBUS] Reading registers...");
    
    // Block 1: Temperatures (0x004A - 0x0054) - 11 registers
    if (verbose) Serial.print("  Block 1 (Temps 0x004A-0x0054)... ");
    if (modbus.readHoldingRegisters(REG_TEMP_AMBIENT, 11, regs)) {
        hpData.T_ambient = DECODE_TEMP(regs[0]);
        hpData.T_outer_coil = DECODE_TEMP(regs[1]);
        hpData.T_inner_coil = DECODE_TEMP(regs[2]);
        hpData.T_return_air = DECODE_TEMP(regs[3]);
        hpData.T_exhaust = DECODE_TEMP(regs[4]);
        hpData.T_water_return = DECODE_TEMP(regs[5]);
        hpData.T_water_outlet = DECODE_TEMP(regs[6]);
        hpData.T_economizer_in = DECODE_TEMP(regs[7]);
        hpData.T_economizer_out = DECODE_TEMP(regs[8]);
        hpData.T_water_tank = DECODE_TEMP(regs[10]);
        if (verbose) Serial.printf("OK (T_amb=%.1f°C)\n", hpData.T_ambient);
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
        
        // Get detailed error
        uint8_t err = modbus.getLastError();
        Serial.printf("    Error code: 0x%02X\n", err);
        return false;  // Early exit on first error
    }
    
    delay(50);
    
    // Block 2: Operating parameters (0x0040 - 0x0047) - 8 registers
    if (verbose) Serial.print("  Block 2 (Params 0x0040-0x0047)... ");
    if (modbus.readHoldingRegisters(REG_COMP_FREQUENCY, 8, regs)) {
        hpData.compressor_freq = regs[0];
        hpData.fan_freq = regs[1];
        hpData.eev_steps = regs[2];
        hpData.evi_steps = regs[3];
        hpData.voltage_ac = DECODE_VOLTAGE(regs[4]);
        hpData.current_ac = DECODE_CURRENT(regs[5]);
        hpData.current_compressor = DECODE_CURRENT(regs[6]);
        hpData.T_ipm = DECODE_TEMP(regs[7]);
        if (verbose) Serial.printf("OK (Comp=%dHz)\n", hpData.compressor_freq);
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
    }
    
    delay(50);
    
    // Block 3: Pressure sat temps (0x0048 - 0x0049) - 2 registers
    if (verbose) Serial.print("  Block 3 (Pressure 0x0048-0x0049)... ");
    if (modbus.readHoldingRegisters(REG_TEMP_SAT_HIGH, 2, regs)) {
        hpData.T_sat_high_pressure = DECODE_TEMP(regs[0]);
        hpData.T_sat_low_pressure = DECODE_TEMP(regs[1]);
        if (verbose) Serial.println("OK");
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
    }
    
    delay(50);
    
    // Block 4: Flow and power (0x0057 - 0x005D) - 7 registers
    if (verbose) Serial.print("  Block 4 (Flow/Power 0x0057-0x005D)... ");
    if (modbus.readHoldingRegisters(REG_WATER_PUMP_PWM, 7, regs)) {
        hpData.water_pump_pwm = regs[0];
        hpData.water_flow = regs[1];
        hpData.voltage_unit = DECODE_VOLTAGE(regs[3]);
        hpData.current_unit = DECODE_CURRENT(regs[4]);
        hpData.power = regs[5];
        hpData.energy_total = regs[6];
        if (verbose) Serial.printf("OK (Power=%dW)\n", hpData.power);
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
    }
    
    delay(50);
    
    // Block 5: Status (0x0000 - 0x0004) - 5 registers
    if (verbose) Serial.print("  Block 5 (Status 0x0000-0x0004)... ");
    if (modbus.readHoldingRegisters(REG_STATUS_1, 5, regs)) {
        hpData.status_1 = regs[0];
        hpData.status_2 = regs[1];
        hpData.fault_status_1 = regs[2];
        hpData.fault_status_2 = regs[3];
        hpData.fault_status_3 = regs[4];
        
        hpData.running = (hpData.status_1 & STATUS_BIT_RUNNING) != 0;
        hpData.defrosting = (hpData.status_1 & STATUS_BIT_DEFROST) != 0;
        hpData.has_fault = (hpData.fault_status_1 != 0 || hpData.fault_status_2 != 0 || hpData.fault_status_3 != 0);
        if (verbose) Serial.printf("OK (Running=%d)\n", hpData.running);
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
    }
    
    delay(50);
    
    // Block 6: Fault code (0x0021) - 1 register
    if (verbose) Serial.print("  Block 6 (Fault 0x0021)... ");
    if (modbus.readHoldingRegisters(REG_FAULT_CODE, 1, regs)) {
        hpData.fault_code = regs[0];
        if (hpData.fault_code != 0xFFFF) {
            hpData.has_fault = true;
        }
        if (verbose) Serial.printf("OK (Code=0x%04X)\n", hpData.fault_code);
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
    }
    
    delay(50);
    
    // Block 7: Setpoints (0x0300 - 0x0305) - 6 registers
    if (verbose) Serial.print("  Block 7 (Setpoints 0x0300-0x0305)... ");
    if (modbus.readHoldingRegisters(REG_SET_TEMP_COOLING, 6, regs)) {
        hpData.set_temp_cooling = DECODE_TEMP(regs[0]);
        hpData.set_temp_heating = DECODE_TEMP(regs[1]);
        hpData.set_temp_hot_water = DECODE_TEMP(regs[2]);
        hpData.mode = regs[4];
        hpData.power_on = (regs[5] == 1);
        if (verbose) Serial.println("OK");
    } else {
        success = false;
        stats.modbus_errors++;
        if (verbose) Serial.println("FAILED");
    }
    
    // Update timestamp
    if (wifiConnected && timeClient.isTimeSet()) {
        hpData.timestamp = timeClient.getEpochTime();
    } else {
        hpData.timestamp = millis() / 1000;
    }
    
    hpData.read_success = success;
    
    if (verbose) {
        Serial.printf("[MODBUS] Read complete: %s\n", success ? "SUCCESS" : "FAILED");
        Serial.printf("  Total errors: %lu / %lu reads\n", stats.modbus_errors, stats.total_reads);
    }
    
    return success;
}

// ============================================================================
// CALCULATIONS
// ============================================================================

void calculateDerivedValues() {
    // Calculate COP
    if (hpData.water_flow > 0 && hpData.power > 100) {
        // Thermal power: Q = flow × cp × ΔT
        // flow [L/h], cp = 4186 J/(kg·K)
        hpData.delta_t_water = hpData.T_water_outlet - hpData.T_water_return;
        
        float thermal_power_w = (hpData.water_flow * 4186.0 * hpData.delta_t_water) / 3600.0;
        hpData.thermal_power_kw = thermal_power_w / 1000.0;
        
        hpData.cop = SAFE_DIV(thermal_power_w, hpData.power, 0.0);
    } else {
        hpData.cop = 0.0;
        hpData.thermal_power_kw = 0.0;
        hpData.delta_t_water = 0.0;
    }
    
    // Calculate superheat
    if (hpData.T_exhaust > 0 && hpData.T_sat_high_pressure > 0) {
        hpData.superheat = hpData.T_exhaust - hpData.T_sat_high_pressure;
    } else {
        hpData.superheat = 0.0;
    }
}

// ============================================================================
// CONTROL LOGIC
// ============================================================================

void controlLoop() {
    unsigned long now = millis();
    
    // Calculate target water temperature using weather curve
    float T_target = calculateWeatherCurveTarget(hpData.T_ambient, controlConfig.setpoint_indoor);
    
    // Control logic with hysteresis
    bool should_run = false;
    
    if (controlConfig.mode == MODE_HEATING) {
        // Heating mode
        if (!hpData.running) {
            // Start if water temp below (target - hysteresis)
            should_run = (hpData.T_water_outlet < T_target - controlConfig.hysteresis);
        } else {
            // Stop if water temp above (target + hysteresis)
            should_run = (hpData.T_water_outlet < T_target + controlConfig.hysteresis);
        }
    } else if (controlConfig.mode == MODE_COOLING) {
        // Cooling mode (inverted logic)
        if (!hpData.running) {
            should_run = (hpData.T_water_outlet > T_target + controlConfig.hysteresis);
        } else {
            should_run = (hpData.T_water_outlet > T_target - controlConfig.hysteresis);
        }
    }
    
    // Enforce minimum runtime / stoptime
    unsigned long timeSinceChange = now - lastStateChange;
    
    if (hpData.running && !should_run) {
        // Wants to stop - check min runtime
        if (timeSinceChange < controlConfig.min_runtime_sec * 1000UL) {
            DEBUG_PRINTF("Min runtime protection: %lu/%lu sec\n", 
                        timeSinceChange/1000, controlConfig.min_runtime_sec);
            should_run = true;  // Force continue running
        }
    } else if (!hpData.running && should_run) {
        // Wants to start - check min stoptime
        if (timeSinceChange < controlConfig.min_stoptime_sec * 1000UL) {
            DEBUG_PRINTF("Min stoptime protection: %lu/%lu sec\n", 
                        timeSinceChange/1000, controlConfig.min_stoptime_sec);
            should_run = false;  // Force stay stopped
        }
    }
    
    // Apply control command
    if (should_run != hpData.running) {
        DEBUG_PRINTF("Control: T_target=%.1f°C, T_actual=%.1f°C → %s\n",
                    T_target, hpData.T_water_outlet, should_run ? "START" : "STOP");
        
        if (sendStartStopCommand(should_run)) {
            lastStateChange = now;
            
            // Log event
            if (sdCardAvailable) {
                File eventFile = SD.open(LOG_FILE_EVENTS, FILE_APPEND);
                if (eventFile) {
                    eventFile.printf("%lu,%s,T_target=%.1f,T_actual=%.1f\n",
                                   hpData.timestamp,
                                   should_run ? "START" : "STOP",
                                   T_target,
                                   hpData.T_water_outlet);
                    eventFile.close();
                }
            }
        }
    }
}

float calculateWeatherCurveTarget(float T_outdoor, float T_indoor_setpoint) {
    // Weather compensated curve: T_water = offset + slope × (T_indoor_target - T_outdoor)
    float T_water = controlConfig.curve_offset + 
                    controlConfig.curve_slope * (T_indoor_setpoint - T_outdoor);
    
    // Limit to min/max range
    if (T_water < controlConfig.min_water_temp) {
        T_water = controlConfig.min_water_temp;
    } else if (T_water > controlConfig.max_water_temp) {
        T_water = controlConfig.max_water_temp;
    }
    
    return T_water;
}

bool sendStartStopCommand(bool start) {
    uint16_t value = start ? 1 : 0;
    
    bool success = modbus.writeSingleRegister(REG_POWER_ONOFF, value);
    
    if (success) {
        Serial.printf("✓ Command %s sent\n", start ? "START" : "STOP");
    } else {
        Serial.printf("✗ Command %s failed\n", start ? "START" : "STOP");
    }
    
    return success;
}

bool setTemperatureSetpoint(uint8_t mode, float temperature) {
    uint16_t address;
    
    switch (mode) {
        case MODE_COOLING:
            address = REG_SET_TEMP_COOLING;
            break;
        case MODE_HEATING:
            address = REG_SET_TEMP_HEATING;
            break;
        case MODE_HOT_WATER:
            address = REG_SET_TEMP_HOT_WATER;
            break;
        default:
            return false;
    }
    
    uint16_t value = ENCODE_TEMP(temperature);
    
    bool success = modbus.writeSingleRegister(address, value);
    
    if (success) {
        Serial.printf("✓ Setpoint mode %d: %.1f°C\n", mode, temperature);
    }
    
    return success;
}

// ============================================================================
// DATA LOGGING
// ============================================================================

void logDataToSD() {
    if (!sdCardAvailable) {
        return;
    }
    
    // Write to CSV file
    File csvFile = SD.open(LOG_FILE_CSV, FILE_APPEND);
    if (csvFile) {
        csvFile.printf("%lu,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d,%.2f,%.2f,%.1f,%d,%d\n",
                      hpData.timestamp,
                      hpData.T_ambient,
                      hpData.T_water_return,
                      hpData.T_water_outlet,
                      hpData.T_exhaust,
                      hpData.compressor_freq,
                      hpData.fan_freq,
                      hpData.water_flow,
                      hpData.power,
                      hpData.cop,
                      hpData.thermal_power_kw,
                      hpData.delta_t_water,
                      hpData.running ? 1 : 0,
                      hpData.has_fault ? 1 : 0);
        csvFile.close();
    } else {
        stats.sd_write_errors++;
    }
    
    // Write to JSON file (more detailed)
    File jsonFile = SD.open(LOG_FILE_JSON, FILE_APPEND);
    if (jsonFile) {
        JsonDocument doc;
        
        doc["ts"] = hpData.timestamp;
        doc["T_amb"] = round(hpData.T_ambient * 10) / 10.0;
        doc["T_ret"] = round(hpData.T_water_return * 10) / 10.0;
        doc["T_out"] = round(hpData.T_water_outlet * 10) / 10.0;
        doc["comp_hz"] = hpData.compressor_freq;
        doc["flow"] = hpData.water_flow;
        doc["power"] = hpData.power;
        doc["cop"] = round(hpData.cop * 100) / 100.0;
        doc["running"] = hpData.running;
        doc["fault"] = hpData.fault_code;
        
        serializeJson(doc, jsonFile);
        jsonFile.println();
        jsonFile.close();
    }
    
    // Check for file rotation (if > 5MB)
    if (SD.exists(LOG_FILE_CSV)) {
        File checkFile = SD.open(LOG_FILE_CSV);
        if (checkFile) {
            size_t fileSize = checkFile.size();
            checkFile.close();
            
            if (fileSize > LOG_MAX_SIZE_KB * 1024) {
                // Rotate file
                char backupName[64];
                snprintf(backupName, sizeof(backupName), "/heatpump_data_%lu.csv", hpData.timestamp);
                SD.rename(LOG_FILE_CSV, backupName);
                Serial.printf("✓ Rotated CSV log to: %s\n", backupName);
            }
        }
    }
}

// ============================================================================
// STATUS DISPLAY
// ============================================================================

void printStatus() {
    Serial.println("\n" + String('=', 80));
    Serial.println("HEAT PUMP STATUS");
    Serial.println(String('=', 80));
    
    // Operating state
    Serial.printf("Running:       %s\n", hpData.running ? "YES" : "NO");
    Serial.printf("Mode:          %d (%s)\n", hpData.mode, 
                 hpData.mode == MODE_HEATING ? "Heating" : 
                 hpData.mode == MODE_COOLING ? "Cooling" : "Other");
    Serial.printf("Defrosting:    %s\n", hpData.defrosting ? "YES" : "NO");
    Serial.printf("Fault:         %s (0x%04X)\n", hpData.has_fault ? "YES" : "NO", hpData.fault_code);
    
    Serial.println(String('-', 80));
    
    // Temperatures
    Serial.printf("T outdoor:     %.1f°C\n", hpData.T_ambient);
    Serial.printf("T water out:   %.1f°C\n", hpData.T_water_outlet);
    Serial.printf("T water ret:   %.1f°C\n", hpData.T_water_return);
    Serial.printf("Delta-T:       %.1f°C\n", hpData.delta_t_water);
    Serial.printf("T exhaust:     %.1f°C\n", hpData.T_exhaust);
    Serial.printf("Superheat:     %.1f°C\n", hpData.superheat);
    
    Serial.println(String('-', 80));
    
    // Operating parameters
    Serial.printf("Compressor:    %d Hz\n", hpData.compressor_freq);
    Serial.printf("Fan:           %d Hz\n", hpData.fan_freq);
    Serial.printf("Water flow:    %d L/h\n", hpData.water_flow);
    Serial.printf("EEV steps:     %d\n", hpData.eev_steps);
    
    Serial.println(String('-', 80));
    
    // Power & COP
    Serial.printf("Power:         %d W\n", hpData.power);
    Serial.printf("Thermal:       %.2f kW\n", hpData.thermal_power_kw);
    Serial.printf("COP:           %.2f\n", hpData.cop);
    Serial.printf("Energy total:  %d kWh\n", hpData.energy_total);
    
    Serial.println(String('-', 80));
    
    // Statistics
    stats.uptime_sec = (millis() - bootTime) / 1000;
    stats.heap_free_kb = ESP.getFreeHeap() / 1024;
    
    Serial.printf("Uptime:        %lu sec (%.1f hours)\n", stats.uptime_sec, stats.uptime_sec / 3600.0);
    Serial.printf("Reads total:   %lu\n", stats.total_reads);
    Serial.printf("Reads OK:      %lu (%.1f%%)\n", stats.successful_reads, 
                 100.0 * stats.successful_reads / (stats.total_reads > 0 ? stats.total_reads : 1));
    Serial.printf("Reads fail:    %lu\n", stats.failed_reads);
    Serial.printf("MODBUS errors: %lu\n", stats.modbus_errors);
    Serial.printf("SD errors:     %lu\n", stats.sd_write_errors);
    Serial.printf("Free heap:     %d KB\n", stats.heap_free_kb);
    
    Serial.println(String('=', 80) + "\n");
}

String getStatusJSON() {
    JsonDocument doc;
    
    doc["timestamp"] = hpData.timestamp;
    doc["running"] = hpData.running;
    doc["mode"] = hpData.mode;
    doc["fault"] = hpData.has_fault;
    doc["fault_code"] = hpData.fault_code;
    
    JsonObject temps = doc["temperatures"].to<JsonObject>();
    temps["ambient"] = hpData.T_ambient;
    temps["water_outlet"] = hpData.T_water_outlet;
    temps["water_return"] = hpData.T_water_return;
    temps["exhaust"] = hpData.T_exhaust;
    temps["delta_t"] = hpData.delta_t_water;
    
    JsonObject perf = doc["performance"].to<JsonObject>();
    perf["cop"] = hpData.cop;
    perf["power_w"] = hpData.power;
    perf["thermal_kw"] = hpData.thermal_power_kw;
    perf["compressor_hz"] = hpData.compressor_freq;
    perf["flow_lh"] = hpData.water_flow;
    
    JsonObject system = doc["system"].to<JsonObject>();
    system["uptime_sec"] = stats.uptime_sec;
    system["reads_ok"] = stats.successful_reads;
    system["reads_fail"] = stats.failed_reads;
    system["heap_kb"] = stats.heap_free_kb;
    
    String json;
    serializeJson(doc, json);
    return json;
}

String getStatusHTML() {
    String html = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Heat Pump Monitor</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f5f5f5; }
        .container { max-width: 1000px; margin: auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #2c3e50; margin: 0 0 20px 0; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }
        .card { background: #ecf0f1; padding: 15px; border-radius: 8px; }
        .label { font-size: 12px; color: #7f8c8d; text-transform: uppercase; }
        .value { font-size: 28px; font-weight: bold; color: #2c3e50; margin: 5px 0; }
        .unit { font-size: 16px; color: #95a5a6; }
        .status-ok { color: #27ae60; }
        .status-error { color: #e74c3c; }
        .refresh { margin: 20px 0; text-align: center; color: #7f8c8d; }
        button { background: #3498db; color: white; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; margin: 5px; }
        button:hover { background: #2980b9; }
        button.stop { background: #e74c3c; }
        button.stop:hover { background: #c0392b; }
    </style>
    <script>
        setInterval(function(){ location.reload(); }, 5000);
    </script>
</head>
<body>
    <div class="container">
        <h1>Heat Pump Monitor</h1>
        <div class="refresh">Auto-refresh: 5 seconds</div>
        
        <div class="grid">
            <div class="card">
                <div class="label">Status</div>
                <div class="value )=====";
    
    html += hpData.running ? "status-ok\">RUNNING" : "status-error\">STOPPED";
    html += R"=====(</div>
            </div>
            
            <div class="card">
                <div class="label">COP</div>
                <div class="value">)=====";
    html += String(hpData.cop, 2);
    html += R"=====(</div>
            </div>
            
            <div class="card">
                <div class="label">T Outdoor</div>
                <div class="value">)=====";
    html += String(hpData.T_ambient, 1);
    html += R"=====( <span class="unit">C</span></div>
            </div>
            
            <div class="card">
                <div class="label">T Water Out</div>
                <div class="value">)=====";
    html += String(hpData.T_water_outlet, 1);
    html += R"=====( <span class="unit">C</span></div>
            </div>
            
            <div class="card">
                <div class="label">Delta-T</div>
                <div class="value">)=====";
    html += String(hpData.delta_t_water, 1);
    html += R"=====( <span class="unit">C</span></div>
            </div>
            
            <div class="card">
                <div class="label">Power</div>
                <div class="value">)=====";
    html += String(hpData.power);
    html += R"=====( <span class="unit">W</span></div>
            </div>
            
            <div class="card">
                <div class="label">Compressor</div>
                <div class="value">)=====";
    html += String(hpData.compressor_freq);
    html += R"=====( <span class="unit">Hz</span></div>
            </div>
            
            <div class="card">
                <div class="label">Flow</div>
                <div class="value">)=====";
    html += String(hpData.water_flow);
    html += R"=====( <span class="unit">L/h</span></div>
            </div>
            
            <div class="card">
                <div class="label">Fault</div>
                <div class="value )=====";
    html += hpData.has_fault ? "status-error\">YES" : "status-ok\">OK";
    html += R"=====(</div>
            </div>
        </div>
        
        <div style="margin-top: 30px; text-align: center;">
            <button onclick="fetch('/api/control/start', {method:'POST'})">START</button>
            <button class="stop" onclick="fetch('/api/control/stop', {method:'POST'})">STOP</button>
            <button onclick="window.location.href='/download/csv'">Download CSV</button>
        </div>
        
        <hr>
        <p style="text-align: center; color: #7f8c8d; font-size: 12px;">
            M5Stamp PLC K141 | ProEnergy Green SRL | Uptime: )=====";
    html += String(stats.uptime_sec);
    html += R"=====( sec
        </p>
    </div>
</body>
</html>
)=====";
    
    return html;
}
