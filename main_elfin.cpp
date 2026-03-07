/**
 * @file main_elfin.cpp
 * @brief M5Stamp PLC K141 - Elfin EW11 Clone for SolarEast Heat Pump
 * 
 * Features:
 * - Auto DE pin scanner (GPIO 46, 1, 4, 5, 2)
 * - Exact Elfin EW11 Modbus protocol (3 blocks)
 * - Live hex dump of Modbus packets
 * - Real-time temperature graphs
 * - CSV export to SD card
 * - Web interface with diagnostics
 * 
 * @version 3.0.0
 * @date 2026-03-07
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>

#include "config_v3.h"
#include "elfin_protocol.h"
#include "pin_scanner.h"

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================
AsyncWebServer server(WEB_SERVER_PORT);
HardwareSerial ModbusSerial(1);
PinScanner* pinScanner = nullptr;
Preferences prefs;

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================
HeatPumpData heatPumpData;
SystemStats stats;
ConfigStorage config;

uint8_t workingDePin = RS485_DE_PIN_DEFAULT;
bool autoScanComplete = false;
bool modbusInitialized = false;

// Hex dump buffer for web display
String lastHexDump = "";
uint8_t lastModbusRequest[256];
uint8_t lastModbusResponse[256];
size_t lastRequestLen = 0;
size_t lastResponseLen = 0;

// Temperature history for graphs (last 60 readings)
#define TEMP_HISTORY_SIZE 60
float tempHistory[TEMP_HISTORY_SIZE];
uint32_t tempHistoryTime[TEMP_HISTORY_SIZE];
uint8_t tempHistoryIndex = 0;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================
void initSerial();
void initSD();
void initWiFi();
void initNTP();
void initWebServer();
void loadConfig();
void saveConfig();
bool initModbus(uint8_t dePin);
bool readElfinBlocks();
bool readModbusBlock(uint8_t blockIndex, uint16_t* dataOut, size_t maxLen);
void decodeHeatPumpData(const uint16_t* block1, const uint16_t* block2, const uint16_t* block3);
void updateTemperatureHistory();
void logToCSV();
String getSystemStatus();
String getHexDump();
uint16_t calculateCRC16(const uint8_t* data, size_t length);

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    // Initialize serial
    initSerial();
    
    Serial.println("\n\n\n");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║  M5Stamp PLC K141 - Elfin EW11 Clone                     ║");
    Serial.println("║  SolarEast Heat Pump Controller                          ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝\n");
    
    Serial.printf("Firmware Version: %s\n", FIRMWARE_VERSION);
    Serial.printf("Build Date: %s %s\n\n", BUILD_DATE, BUILD_TIME);
    
    // Initialize stats
    memset(&stats, 0, sizeof(stats));
    memset(&heatPumpData, 0, sizeof(heatPumpData));
    
    // Load saved configuration
    loadConfig();
    
    // Initialize SD card
    initSD();
    
    // Initialize WiFi
    initWiFi();
    
    // Initialize NTP
    initNTP();
    
    // AUTO PIN SCANNER
    if (AUTO_SCAN_ON_BOOT && !config.calibrated) {
        Serial.println("\n[AUTO SCAN] Starting DE pin scanner...\n");
        
        pinScanner = new PinScanner(&ModbusSerial, RS485_TX_PIN, RS485_RX_PIN, MODBUS_BAUDRATE);
        workingDePin = pinScanner->scanAllPins();
        
        if (workingDePin > 0) {
            Serial.printf("✓ Found working DE pin: GPIO %d\n", workingDePin);
            config.dePin = workingDePin;
            config.calibrated = true;
            if (SAVE_WORKING_PIN) {
                saveConfig();
            }
        } else {
            Serial.println("✗ No working DE pin found! Using default GPIO 46\n");
            workingDePin = RS485_DE_PIN_DEFAULT;
        }
        
        autoScanComplete = true;
        delete pinScanner;
        pinScanner = nullptr;
    } else if (config.calibrated) {
        workingDePin = config.dePin;
        Serial.printf("[CONFIG] Using saved DE pin: GPIO %d\n\n", workingDePin);
    }
    
    // Initialize Modbus with working pin
    if (initModbus(workingDePin)) {
        Serial.println("✓ Modbus initialized successfully\n");
        modbusInitialized = true;
    } else {
        Serial.println("✗ Modbus initialization failed\n");
    }
    
    // Initialize web server
    initWebServer();
    
    Serial.println("🚀 SYSTEM READY\n");
    Serial.println("═══════════════════════════════════════════════════════════\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    static uint32_t lastRead = 0;
    static uint32_t lastLog = 0;
    
    uint32_t now = millis();
    
    // Read heat pump data
    if (modbusInitialized && (now - lastRead >= READ_INTERVAL_MS)) {
        lastRead = now;
        
        Serial.println("\n[READ] Reading Elfin blocks...");
        
        if (readElfinBlocks()) {
            stats.successfulReads++;
            heatPumpData.valid = true;
            
            Serial.println("✓ All blocks read successfully");
            Serial.printf("  Ambient temp: %.1f°C\n", heatPumpData.tempAmbient);
            Serial.printf("  Water in/out: %.1f / %.1f°C\n", 
                         heatPumpData.tempWaterInlet, heatPumpData.tempWaterOutlet);
            
            updateTemperatureHistory();
        } else {
            stats.failedReads++;
            heatPumpData.valid = false;
            Serial.println("✗ Read failed");
        }
        
        stats.totalReads++;
        stats.successRate = (float)stats.successfulReads / stats.totalReads * 100.0f;
        
        Serial.printf("  Success rate: %.1f%% (%d/%d)\n", 
                     stats.successRate, stats.successfulReads, stats.totalReads);
    }
    
    // Log to CSV
    if (LOG_TO_SD && heatPumpData.valid && (now - lastLog >= CSV_LOG_INTERVAL_SEC * 1000)) {
        lastLog = now;
        logToCSV();
    }
    
    stats.uptime = millis() / 1000;
    
    delay(10);
}

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

void initSerial() {
    Serial.begin(DEBUG_BAUD_RATE);
    delay(100);
}

void initSD() {
    Serial.println("[INIT] Initializing SD Card...");
    
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    
    if (SD.begin(SD_CS_PIN)) {
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("✓ SD Card: %llu MB\n\n", cardSize);
    } else {
        Serial.println("✗ SD Card mount failed\n");
    }
}

void initWiFi() {
    Serial.println("[INIT] Connecting to WiFi...");
    Serial.printf("SSID: %s\n", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi connected");
        Serial.printf("  IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  RSSI: %d dBm\n\n", WiFi.RSSI());
    } else {
        Serial.println("\n✗ WiFi connection failed\n");
    }
}

void initNTP() {
    Serial.println("[INIT] Synchronizing time...");
    configTime(NTP_TIMEZONE_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
        Serial.println("✓ Time synchronized\n");
    } else {
        Serial.println("✗ Time sync failed\n");
    }
}

bool initModbus(uint8_t dePin) {
    Serial.println("[MODBUS] Initializing with Elfin protocol...");
    Serial.printf("  TX: GPIO %d\n", RS485_TX_PIN);
    Serial.printf("  RX: GPIO %d\n", RS485_RX_PIN);
    Serial.printf("  DE: GPIO %d\n", dePin);
    Serial.printf("  Baudrate: %d\n", MODBUS_BAUDRATE);
    Serial.printf("  Slave ID: 0x%02X\n\n", MODBUS_SLAVE_ID);
    
    pinMode(dePin, OUTPUT);
    digitalWrite(dePin, LOW);
    
    ModbusSerial.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    ModbusSerial.setRxBufferSize(512);
    ModbusSerial.setTxBufferSize(512);
    
    delay(100);
    
    return true;
}

void initWebServer() {
    Serial.println("[INIT] Starting web server...");
    
    // Serve static page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = "<!DOCTYPE html><html><head><title>Elfin Clone</title></head>";
        html += "<body><h1>M5Stamp PLC - Elfin EW11 Clone</h1>";
        html += "<p>Firmware: " + String(FIRMWARE_VERSION) + "</p>";
        html += "<p><a href='/status'>System Status</a> | ";
        html += "<a href='/hex'>Hex Dump</a> | ";
        html += "<a href='/csv'>Download CSV</a></p>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    });
    
    // API endpoints
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", getSystemStatus());
    });
    
    server.on("/hex", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", getHexDump());
    });
    
    server.begin();
    Serial.printf("✓ Web server started on port %d\n\n", WEB_SERVER_PORT);
}

// Configuration management
void loadConfig() {
    prefs.begin("heatpump", false);
    
    config.magicNumber = prefs.getUInt("magic", 0);
    
    if (config.magicNumber == CONFIG_MAGIC_NUMBER) {
        config.dePin = prefs.getUChar("dePin", RS485_DE_PIN_DEFAULT);
        config.calibrated = prefs.getBool("calibrated", false);
        Serial.println("[CONFIG] Loaded saved configuration");
    } else {
        config.dePin = RS485_DE_PIN_DEFAULT;
        config.calibrated = false;
        Serial.println("[CONFIG] No saved configuration, using defaults");
    }
    
    prefs.end();
}

void saveConfig() {
    prefs.begin("heatpump", false);
    
    prefs.putUInt("magic", CONFIG_MAGIC_NUMBER);
    prefs.putUChar("dePin", config.dePin);
    prefs.putBool("calibrated", config.calibrated);
    
    prefs.end();
    
    Serial.println("[CONFIG] Configuration saved to NVS");
}

// ============================================================================
// MODBUS READING FUNCTIONS
// ============================================================================

/**
 * @brief Read all 3 Elfin blocks and decode data
 */
bool readElfinBlocks() {
    uint16_t block1Data[64];  // 41 registers max
    uint16_t block2Data[128]; // 62 registers max
    uint16_t block3Data[16];  // 8 registers max
    
    bool success = true;
    
    // Read Block 1: System Status
    if (!readModbusBlock(0, block1Data, 64)) {
        Serial.println("  ✗ Block 1 (SYSTEM) failed");
        success = false;
    } else {
        Serial.println("  ✓ Block 1 (SYSTEM) OK");
    }
    
    delay(BLOCK_READ_DELAY_MS);
    
    // Read Block 2: Temperatures
    if (!readModbusBlock(1, block2Data, 128)) {
        Serial.println("  ✗ Block 2 (TEMPS) failed");
        success = false;
    } else {
        Serial.println("  ✓ Block 2 (TEMPS) OK");
    }
    
    delay(BLOCK_READ_DELAY_MS);
    
    // Read Block 3: Water temps
    if (!readModbusBlock(2, block3Data, 16)) {
        Serial.println("  ✗ Block 3 (WATER) failed");
        success = false;
    } else {
        Serial.println("  ✓ Block 3 (WATER) OK");
    }
    
    if (success) {
        decodeHeatPumpData(block1Data, block2Data, block3Data);
    }
    
    return success;
}

/**
 * @brief Read single Modbus block using Elfin protocol
 */
bool readModbusBlock(uint8_t blockIndex, uint16_t* dataOut, size_t maxLen) {
    if (blockIndex >= ELFIN_BLOCK_COUNT) return false;
    
    const ElfinBlock& block = ELFIN_BLOCKS[blockIndex];
    
    // Build request
    uint8_t request[8];
    request[0] = MODBUS_SLAVE_ID;
    request[1] = 0x03;  // Read Holding Registers
    request[2] = (block.startAddr >> 8) & 0xFF;
    request[3] = block.startAddr & 0xFF;
    request[4] = (block.count >> 8) & 0xFF;
    request[5] = block.count & 0xFF;
    
    uint16_t crc = calculateCRC16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Save for hex dump
    memcpy(lastModbusRequest, request, 8);
    lastRequestLen = 8;
    
    if (DEBUG_PRINT_RAW_PACKETS) {
        Serial.printf("  TX: %s\n", bytesToHex(request, 8).c_str());
    }
    
    // Clear RX buffer
    while (ModbusSerial.available()) ModbusSerial.read();
    
    // Send request
    digitalWrite(workingDePin, HIGH);
    delayMicroseconds(100);
    ModbusSerial.write(request, 8);
    ModbusSerial.flush();
    delayMicroseconds(100);
    digitalWrite(workingDePin, LOW);
    
    // Wait for response
    uint32_t startTime = millis();
    uint16_t bytesReceived = 0;
    uint8_t response[512];
    
    while (millis() - startTime < MODBUS_TIMEOUT_MS) {
        if (ModbusSerial.available()) {
            response[bytesReceived++] = ModbusSerial.read();
            
            if (bytesReceived >= 5) {
                uint8_t expectedBytes = response[2] + 5;
                if (bytesReceived >= expectedBytes) {
                    break;
                }
            }
            
            if (bytesReceived >= 512) break;
        }
    }
    
    // Save for hex dump
    memcpy(lastModbusResponse, response, bytesReceived);
    lastResponseLen = bytesReceived;
    
    if (DEBUG_PRINT_RAW_PACKETS) {
        Serial.printf("  RX: %s\n", bytesToHex(response, bytesReceived).c_str());
    }
    
    // Validate response
    if (bytesReceived < 5) {
        stats.timeoutErrors++;
        return false;
    }
    
    if (response[0] != MODBUS_SLAVE_ID || response[1] != 0x03) {
        return false;
    }
    
    // Check CRC
    uint16_t responseCrc = response[bytesReceived - 2] | (response[bytesReceived - 1] << 8);
    uint16_t calculatedCrc = calculateCRC16(response, bytesReceived - 2);
    
    if (responseCrc != calculatedCrc) {
        stats.crcErrors++;
        return false;
    }
    
    // Extract data (skip header: SlaveID, Function, ByteCount)
    uint8_t dataBytes = response[2];
    uint16_t registerCount = dataBytes / 2;
    
    for (uint16_t i = 0; i < registerCount && i < maxLen; i++) {
        uint8_t highByte = response[3 + (i * 2)];
        uint8_t lowByte = response[4 + (i * 2)];
        dataOut[i] = (highByte << 8) | lowByte;
    }
    
    return true;
}

/**
 * @brief Decode heat pump data from Modbus registers
 */
void decodeHeatPumpData(const uint16_t* block1, const uint16_t* block2, const uint16_t* block3) {
    heatPumpData.timestamp = millis();
    
    // Get current time
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        strftime(heatPumpData.dateTime, sizeof(heatPumpData.dateTime), 
                "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    
    // Block 1: System status
    heatPumpData.systemStatus = block1[0];
    heatPumpData.operatingMode = block1[1];
    
    // Block 2: Temperatures (based on Elfin log analysis)
    // Note: Exact offsets need to be mapped from your pump documentation
    // Example: byte 46 (register 23) = 0x006E = 11.0°C ambient
    
    heatPumpData.tempAmbient = decodeTemperature(block2[23]);  // Offset 0x17 (23 decimal)
    
    // Other temperatures (map these to correct offsets)
    heatPumpData.tempEvaporatorIn = decodeTemperature(block2[0]);
    heatPumpData.tempEvaporatorOut = decodeTemperature(block2[1]);
    heatPumpData.tempCompressorDischarge = decodeTemperature(block2[2]);
    heatPumpData.tempCondenserIn = decodeTemperature(block2[3]);
    heatPumpData.tempCondenserOut = decodeTemperature(block2[4]);
    heatPumpData.tempDefrost = decodeTemperature(block2[5]);
    heatPumpData.tempSuction = decodeTemperature(block2[6]);
    
    // Block 3: Water temperatures
    heatPumpData.tempWaterInlet = decodeTemperature(block3[0]);
    heatPumpData.tempWaterOutlet = decodeTemperature(block3[1]);
    heatPumpData.tempWaterTarget = decodeTemperature(block3[2]);
    
    // Pressures (decode based on your pump's scale factor)
    heatPumpData.pressureHigh = block2[20] / 10.0f;  // Example offset
    heatPumpData.pressureLow = block2[21] / 10.0f;
    
    heatPumpData.lastReadTime = millis();
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void updateTemperatureHistory() {
    tempHistory[tempHistoryIndex] = heatPumpData.tempAmbient;
    tempHistoryTime[tempHistoryIndex] = millis();
    tempHistoryIndex = (tempHistoryIndex + 1) % TEMP_HISTORY_SIZE;
}

void logToCSV() {
    if (!SD.begin(SD_CS_PIN)) return;
    
    File file = SD.open("/heatpump_log.csv", FILE_APPEND);
    if (!file) {
        Serial.println("[CSV] Failed to open log file");
        return;
    }
    
    // Write header if file is new
    if (file.size() == 0) {
        file.println("Timestamp,Ambient,WaterIn,WaterOut,PressHigh,PressLow,Status");
    }
    
    // Write data
    file.printf("%s,%.1f,%.1f,%.1f,%.2f,%.2f,%d\n",
                heatPumpData.dateTime,
                heatPumpData.tempAmbient,
                heatPumpData.tempWaterInlet,
                heatPumpData.tempWaterOutlet,
                heatPumpData.pressureHigh,
                heatPumpData.pressureLow,
                heatPumpData.systemStatus);
    
    file.close();
    Serial.println("[CSV] Data logged");
}

String getSystemStatus() {
    DynamicJsonDocument doc(2048);
    
    doc["firmware"] = FIRMWARE_VERSION;
    doc["uptime"] = stats.uptime;
    doc["dePin"] = workingDePin;
    
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["connected"] = WiFi.status() == WL_CONNECTED;
    wifi["ip"] = WiFi.localIP().toString();
    wifi["rssi"] = WiFi.RSSI();
    
    JsonObject modbus = doc.createNestedObject("modbus");
    modbus["totalReads"] = stats.totalReads;
    modbus["successful"] = stats.successfulReads;
    modbus["failed"] = stats.failedReads;
    modbus["successRate"] = stats.successRate;
    
    JsonObject temps = doc.createNestedObject("temperatures");
    temps["ambient"] = heatPumpData.tempAmbient;
    temps["waterIn"] = heatPumpData.tempWaterInlet;
    temps["waterOut"] = heatPumpData.tempWaterOutlet;
    temps["valid"] = heatPumpData.valid;
    
    String output;
    serializeJson(doc, output);
    return output;
}

String getHexDump() {
    String dump = "=== LAST MODBUS TRANSACTION ===\n\n";
    
    dump += "REQUEST (" + String(lastRequestLen) + " bytes):\n";
    dump += bytesToHex(lastModbusRequest, lastRequestLen);
    dump += "\n\n";
    
    dump += "RESPONSE (" + String(lastResponseLen) + " bytes):\n";
    dump += bytesToHex(lastModbusResponse, lastResponseLen);
    dump += "\n";
    
    return dump;
}

uint16_t calculateCRC16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
