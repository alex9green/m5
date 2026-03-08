// ============================================================================
// main_v5.cpp - HEAT PUMP MONITOR v5.0 - HARDWARE RS485 + SD DEBUG LOGGING
// M5Stack StampPLC K141 (ESP32-S3)
// ============================================================================
// CHANGELOG v5.0 (rezolvare finala a tuturor bugurilor RS485):
//
//   🔴 BUG FIX #1: RS485_DE_PIN = 0 (GPIO 0, pin oficial StamPLC.pdf)
//      Toate versiunile anterioare foloseau GPIO 2/46/1/4/5 - GREȘIT!
//
//   🔴 BUG FIX #2: Hardware UART_MODE_RS485_HALF_DUPLEX
//      Nu mai e nevoie de digitalWrite() manual - hardware face totul automat
//      Timing perfect, zero echo, sincronizare la nivel de bit
//
//   🟢 NEW: Debug logging complet pe SD card
//      - /debug_v5.log: Tot ce se intampla (nivel DEBUG/VERBOSE)
//      - /heatpump_v5.csv: Date temperatura (format CSV)
//      - /errors_v5.log: Doar erorile critice
//      - /boot_v5.log: Info boot si configuratie
//
//   🟢 NEW: Echo detection automat
//      Detecteaza si raporteaza daca DE pin nu comuta corect TX/RX
//
//   🟢 NEW: Pin scanner v5 include GPIO 0 (PRIMUL in lista!)
//      Versiunile anterioare skipau GPIO 0 din teama nejustificata
//
//   🟢 NEW: Timing profiler pentru fiecare operatie
//
// UTILIZARE BUTOANE:
//   KEYA (GPIO 39): Reset statistici
//   KEYB (GPIO 40): Forteaza citire imediata
//   KEYC (GPIO 41): Cicleaza log level (INFO→DEBUG→VERBOSE→INFO)
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <time.h>
#include <SPI.h>
#include <SD.h>

// Include v5 headers
#include "config_v5.h"
#include "debug_logger.h"
#include "modbus_v5.h"
#include "pin_test_v5.h"

// ============================================================================
// GLOBALS
// ============================================================================
HardwareSerial ModbusSerial(1);  // UART1 pentru RS485
WebServer      webServer(WEB_SERVER_PORT);
Preferences    preferences;

ModbusV5    modbus;
PinTesterV5 pinTester;

HeatPumpData  currentData;
SystemStats   sysStats;

uint32_t lastReadTime    = 0;
uint32_t lastCSVTime     = 0;
uint32_t lastStatsTime   = 0;
bool     sdAvailable     = false;
bool     wifiConnected   = false;
bool     modbusOK        = false;
uint8_t  activeDePin     = RS485_DE_PIN;

// Bufer pentru history date (web interface)
#define HISTORY_SIZE 60
HeatPumpData dataHistory[HISTORY_SIZE];
int          historyIndex = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
bool    initSD();
bool    initWifi();
bool    initNTP();
bool    initModbus();
void    initWebServer();
void    initOTA();
void    initButtons();
bool    performRead();
bool    decodeHeatPumpData(const uint8_t* b1, size_t l1,
                            const uint8_t* b2, size_t l2,
                            const uint8_t* b3, size_t l3,
                            HeatPumpData& data);
float   decodeTemperature(const uint8_t* data, int regOffset);
bool    isValidTemp(float t);
void    getDateTimeString(char* buf, size_t len);
void    handleWebRoot();
void    handleWebStatus();
void    handleWebHistory();
void    handleWebLogs();
void    handleWebDownloadCSV();
void    handleWebPinScan();
void    handleWebSetLogLevel();
void    checkButtons();
void    printBanner();

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    // -------------------------------------------------------------------------
    // 1. Serial (pentru debug inainte de SD)
    // -------------------------------------------------------------------------
    Serial.begin(LOG_SERIAL_BAUD);
    delay(500);

    printBanner();

    // -------------------------------------------------------------------------
    // 2. SD Card - initializeaza PRIMUL pentru a putea loga totul
    // -------------------------------------------------------------------------
    sdAvailable = initSD();
    DebugLogger::init(sdAvailable);

    // Boot log
    char bootMsg[128];
    snprintf(bootMsg, sizeof(bootMsg),
             "BOOT v%s | DE Pin: GPIO %d | Build: %s %s",
             FW_VERSION, RS485_DE_PIN, FW_BUILD_DATE, FW_BUILD_TIME);
    DebugLogger::writeBootLog(bootMsg);

    // -------------------------------------------------------------------------
    // 3. Verificare GPIO 0 (VALIDATE_GPIO0_ON_BOOT)
    // -------------------------------------------------------------------------
    if (VALIDATE_GPIO0_ON_BOOT) {
        LOG_I("BOOT", "Verificare GPIO 0 dupa boot...");
        delay(GPIO0_SETTLE_MS);

        // GPIO 0 ar trebui sa fie HIGH dupa boot (pull-up in hardware StamPLC)
        pinMode(0, INPUT);
        int gpio0State = digitalRead(0);
        LOG_I("BOOT", "GPIO 0 stare: %s (%d)",
              gpio0State == HIGH ? "HIGH (normal)" : "LOW (boot mode?)", gpio0State);

        if (gpio0State == LOW) {
            LOG_W("BOOT", "GPIO 0 e LOW! Verifica daca BOOT button e apasat.");
            LOG_W("BOOT", "RS485 va functiona dupa ce GPIO 0 se stabilizeaza.");
            delay(200);
        } else {
            LOG_I("BOOT", "GPIO 0 OK - gata pentru RS485 hardware mode ✓");
        }
    }

    // -------------------------------------------------------------------------
    // 4. WiFi
    // -------------------------------------------------------------------------
    wifiConnected = initWifi();

    // -------------------------------------------------------------------------
    // 5. NTP Time (dupa WiFi)
    // -------------------------------------------------------------------------
    if (wifiConnected) {
        initNTP();
    }

    // -------------------------------------------------------------------------
    // 6. Modbus RS485 - HARDWARE MODE cu GPIO 0
    // -------------------------------------------------------------------------
    modbusOK = initModbus();

    // -------------------------------------------------------------------------
    // 7. Web Server & OTA
    // -------------------------------------------------------------------------
    if (wifiConnected) {
        initWebServer();
        initOTA();
    }

    // -------------------------------------------------------------------------
    // 8. Butoane
    // -------------------------------------------------------------------------
    initButtons();

    // -------------------------------------------------------------------------
    // 9. Prima citire imediata
    // -------------------------------------------------------------------------
    memset(&currentData, 0, sizeof(currentData));
    memset(&sysStats, 0, sizeof(sysStats));

    LOG_SEPARATOR();
    LOG_I("BOOT", "=== SETUP COMPLET - v%s ===", FW_VERSION);
    LOG_I("BOOT", "RS485 DE Pin: GPIO %d %s",
          activeDePin,
          activeDePin == 0 ? "(OFICIAL ✓)" : "(NON-STANDARD!)");
    LOG_I("BOOT", "Hardware RS485 mode: %s",
          modbus.isHWModeActive() ? "ACTIV ✓" : "INACTIV ✗");
    LOG_I("BOOT", "SD Card: %s", sdAvailable ? "OK ✓" : "LIPSA ✗");
    LOG_I("BOOT", "WiFi: %s", wifiConnected ? WiFi.localIP().toString().c_str() : "OFFLINE");
    LOG_SEPARATOR();

    if (modbusOK) {
        LOG_I("BOOT", "Prima citire...");
        performRead();
    }
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    uint32_t now = millis();

    // OTA
    if (wifiConnected) {
        ArduinoOTA.handle();
        webServer.handleClient();
    }

    // Citire periodica
    if (now - lastReadTime >= READ_INTERVAL_MS) {
        lastReadTime = now;
        performRead();
    }

    // CSV log periodic
    if (now - lastCSVTime >= (CSV_LOG_INTERVAL_SEC * 1000)) {
        lastCSVTime = now;
        if (currentData.valid) {
            DebugLogger::writeCSV(currentData);
        }
    }

    // Statistici periodice
    if (now - lastStatsTime >= STATS_PRINT_INTERVAL_MS) {
        lastStatsTime = now;
        sysStats.uptime = millis();
        if (sysStats.totalReads > 0) {
            sysStats.successRate =
                (float)sysStats.successfulReads / sysStats.totalReads * 100.0f;
        }
        DebugLogger::logSystemStats(sysStats);
        modbus.printStats();
    }

    // Butoane
    checkButtons();

    delay(10);
}

// ============================================================================
// initSD() - Initializeaza SD card
// ============================================================================
bool initSD() {
    Serial.println("[SD] Initializing SD card...");

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQ)) {
        Serial.println("[SD] EROARE: SD card nu raspunde!");
        return false;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] OK! Marime: %llu MB\n", cardSize);

    return true;
}

// ============================================================================
// initWifi() - Conectare WiFi
// ============================================================================
bool initWifi() {
    LOG_I("WIFI", "Conectare la %s...", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_TIMEOUT_MS) {
            LOG_W("WIFI", "Timeout! Continuam fara WiFi.");
            return false;
        }
        delay(500);
    }

    LOG_I("WIFI", "Conectat! IP: %s", WiFi.localIP().toString().c_str());
    LOG_I("WIFI", "Hostname: %s", WIFI_HOSTNAME);
    return true;
}

// ============================================================================
// initNTP() - Sincronizare timp
// ============================================================================
bool initNTP() {
    LOG_I("NTP", "Sincronizare timp...");
    configTime(NTP_TIMEZONE_OFFSET, NTP_DAYLIGHT_OFFSET,
               NTP_SERVER1, NTP_SERVER2);

    uint32_t startTime = millis();
    struct tm timeInfo;
    while (!getLocalTime(&timeInfo)) {
        if (millis() - startTime > 5000) {
            LOG_W("NTP", "Timeout NTP sync!");
            return false;
        }
        delay(200);
    }

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeInfo);
    LOG_I("NTP", "Ora sincronizata: %s", buf);
    return true;
}

// ============================================================================
// initModbus() - CONFIGURATIE HARDWARE RS485 MODE
// ============================================================================
bool initModbus() {
    LOG_I("MODBUS", "=== Initializare RS485 Modbus v5 ===");

    // Verifica daca avem un pin salvat din sesiunea anterioara
    uint8_t savedPin = PinTesterV5::loadFromNVS();

    if (savedPin != 0xFF) {
        // Avem pin salvat - incearca cu el direct
        LOG_I("MODBUS", "Utilizare pin GPIO %d din NVS", savedPin);
        activeDePin = savedPin;

        bool ok = modbus.init(ModbusSerial, activeDePin);

        if (!ok) {
            LOG_W("MODBUS", "Init cu pin salvat a esuat → rulare pin scan");
            goto do_scan;
        }

        // Verifica ca pin-ul salvat chiar functioneaza
        LOG_I("MODBUS", "Verificare rapida pin GPIO %d...", activeDePin);
        PinTestResult r = pinTester.testPin(ModbusSerial, activeDePin);

        if (r.successRate < PIN_SCAN_SUCCESS_RATE) {
            LOG_W("MODBUS", "Pin GPIO %d nu mai functioneaza (%.0f%%) → rescan",
                  activeDePin, r.successRate);
            goto do_scan;
        }

        LOG_I("MODBUS", "Pin GPIO %d OK (%.0f%%) ✓", activeDePin, r.successRate);
        return true;
    }

do_scan:
    // Pin scan complet - include GPIO 0 PRIMUL!
    LOG_I("MODBUS", "Rulare pin scan complet...");
    uint8_t bestPin = pinTester.scanAll(ModbusSerial);

    if (bestPin == 0xFF) {
        LOG_E("MODBUS", "Pin scan ESUAT - niciun pin valid!");

        // Fallback: incearca cu GPIO 0 (oficial) oricum
        LOG_W("MODBUS", "Fallback: incerc GPIO 0 direct...");
        activeDePin = 0;
        return modbus.init(ModbusSerial, 0);
    }

    activeDePin = bestPin;
    bool ok = modbus.init(ModbusSerial, activeDePin);

    if (ok) {
        LOG_I("MODBUS", "✓ Modbus initializat cu GPIO %d", activeDePin);
    }

    return ok;
}

// ============================================================================
// performRead() - Citeste date de la pompa de caldura
// ============================================================================
bool performRead() {
    uint32_t startMs = millis();

    sysStats.totalReads++;

    // Buffere pentru cele 3 blocuri
    uint8_t b1[256], b2[256], b3[64];
    size_t  l1 = 0, l2 = 0, l3 = 0;

    LOG_D("MODBUS", "=== Citire Modbus ===");

    bool anyOk = modbus.readBlock3(b1, &l1, b2, &l2, b3, &l3);

    uint32_t readMs = millis() - startMs;
    LOG_TIMING("Total read 3 blocks", readMs);

    if (!anyOk) {
        sysStats.failedReads++;
        sysStats.crcErrors += modbus.stats.rxCrcErrors;
        sysStats.timeoutErrors += modbus.stats.rxTimeouts;
        LOG_E("MODBUS", "Citire esuata dupa %lu ms!", readMs);
        return false;
    }

    // Decode date
    HeatPumpData newData;
    memset(&newData, 0, sizeof(newData));
    newData.lastReadTime = millis();
    newData.readDurationMs = readMs;
    getDateTimeString(newData.dateTime, sizeof(newData.dateTime));
    newData.timestamp = time(nullptr);

    bool decoded = decodeHeatPumpData(b1, l1, b2, l2, b3, l3, newData);

    if (decoded) {
        currentData = newData;

        // Adauga in history
        dataHistory[historyIndex % HISTORY_SIZE] = currentData;
        historyIndex++;

        sysStats.successfulReads++;
        sysStats.avgReadTimeMs =
            (sysStats.avgReadTimeMs * (sysStats.successfulReads - 1) + readMs)
            / sysStats.successfulReads;

        // Log date
        DebugLogger::logHeatPumpData(currentData);

        LOG_I("MODBUS", "✓ Citire OK: Ambient=%.1f°C WaterOut=%.1f°C [%lu ms]",
              currentData.tempAmbient, currentData.tempWaterOutlet, readMs);

        return true;
    }

    sysStats.failedReads++;
    LOG_E("MODBUS", "Decode esuat!");
    return false;
}

// ============================================================================
// decodeHeatPumpData() - Extrage date din bufferele raw Modbus
// ============================================================================
bool decodeHeatPumpData(const uint8_t* b1, size_t l1,
                         const uint8_t* b2, size_t l2,
                         const uint8_t* b3, size_t l3,
                         HeatPumpData& data) {

    data.dataQuality = 0;
    data.valid = false;

    // Decode Block 1: System status
    if (l1 >= 5) {
        const uint8_t* raw = b1 + 3;  // Skip: SlaveID, FC, ByteCount
        data.systemStatus  = (raw[0] << 8) | raw[1];
        data.operatingMode = (raw[2] << 8) | raw[3];
        if (l1 >= 11) {
            data.faultCode   = (raw[6] << 8) | raw[7];
            data.runningHours = (raw[8] << 8) | raw[9];
        }
        data.dataQuality += 33;

        LOG_D("DATA", "Block1: Status=0x%04X Mode=0x%04X Fault=0x%04X Hours=%d",
              data.systemStatus, data.operatingMode, data.faultCode, data.runningHours);
    }

    // Decode Block 2: Temperatures
    // Block 2 = addr 0x0040, registru 0x0E (offset 14) = T1_AMBIENT
    if (l2 >= 5) {
        const uint8_t* raw = b2 + 3;

        // Conform Elfin protocol - register 0x0E in Block 2 = T1 Ambient
        data.tempAmbient = decodeTemperature(raw, 14 * 2);

        // Alte temperaturi la offseturi specifice
        data.tempEvapIn       = decodeTemperature(raw, 0 * 2);
        data.tempEvapOut      = decodeTemperature(raw, 2 * 2);
        data.tempCompDisch    = decodeTemperature(raw, 4 * 2);

        // Presiuni
        if (l2 >= (size_t)(3 + 20 * 2)) {
            int16_t rawPress = (raw[18*2] << 8) | raw[18*2+1];
            data.pressHigh = rawPress / 100.0f;
            rawPress = (raw[19*2] << 8) | raw[19*2+1];
            data.pressLow = rawPress / 100.0f;
        }

        data.dataQuality += 33;

        LOG_D("DATA", "Block2: Ambient=%.1f Evap_In=%.1f Out=%.1f Comp=%.1f°C",
              data.tempAmbient, data.tempEvapIn, data.tempEvapOut, data.tempCompDisch);
        LOG_D("DATA", "Block2: PressHigh=%.2f PressLow=%.2f bar",
              data.pressHigh, data.pressLow);
    }

    // Decode Block 3: Water temperatures
    if (l3 >= 5) {
        const uint8_t* raw = b3 + 3;
        data.tempWaterInlet  = decodeTemperature(raw, 0 * 2);
        data.tempWaterOutlet = decodeTemperature(raw, 1 * 2);
        data.tempWaterTarget = decodeTemperature(raw, 2 * 2);

        data.dataQuality += 34;

        LOG_D("DATA", "Block3: WaterIn=%.1f Out=%.1f Target=%.1f°C",
              data.tempWaterInlet, data.tempWaterOutlet, data.tempWaterTarget);
    }

    data.valid = (data.dataQuality > 0);
    return data.valid;
}

// ============================================================================
// decodeTemperature() - Decodifica temperatura din registru Modbus
// Formula: valoare_signed_int16 / 10.0 = temperatura °C
// ============================================================================
float decodeTemperature(const uint8_t* rawData, int byteOffset) {
    if (!rawData) return -999.0f;

    int16_t raw = (int16_t)((rawData[byteOffset] << 8) | rawData[byteOffset + 1]);
    float temp = raw / 10.0f;

    if (!isValidTemp(temp)) {
        LOG_V("DATA", "Temp invalida la offset %d: raw=0x%04X (%.1f°C)",
              byteOffset, (uint16_t)raw, temp);
        return -999.0f;
    }

    return temp;
}

bool isValidTemp(float t) {
    return (t >= -50.0f && t <= 150.0f);
}

// ============================================================================
// getDateTimeString() - Returneaza data/ora curenta ca string
// ============================================================================
void getDateTimeString(char* buf, size_t len) {
    struct tm timeInfo;
    if (getLocalTime(&timeInfo)) {
        strftime(buf, len, "%Y-%m-%d %H:%M:%S", &timeInfo);
    } else {
        snprintf(buf, len, "uptime_%lu", millis() / 1000);
    }
}

// ============================================================================
// WEB SERVER HANDLERS
// ============================================================================
void initWebServer() {
    webServer.on("/", HTTP_GET, handleWebRoot);
    webServer.on("/status", HTTP_GET, handleWebStatus);
    webServer.on("/history", HTTP_GET, handleWebHistory);
    webServer.on("/logs", HTTP_GET, handleWebLogs);
    webServer.on("/download/csv", HTTP_GET, handleWebDownloadCSV);
    webServer.on("/pinscan", HTTP_GET, handleWebPinScan);
    webServer.on("/loglevel", HTTP_GET, handleWebSetLogLevel);
    webServer.begin();

    LOG_I("WEB", "Server pornit pe port %d", WEB_SERVER_PORT);
    LOG_I("WEB", "  GET /         → Status simplu");
    LOG_I("WEB", "  GET /status   → JSON complet");
    LOG_I("WEB", "  GET /history  → Istoric 60 citiri");
    LOG_I("WEB", "  GET /logs     → Ultimele loguri SD");
    LOG_I("WEB", "  GET /download/csv → Download CSV");
    LOG_I("WEB", "  GET /pinscan  → Ruleaza pin scan");
    LOG_I("WEB", "  GET /loglevel?level=N → Schimba log level");
}

void handleWebRoot() {
    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>Heat Pump Monitor v5</title></head><body>"
        "<h1>Heat Pump Monitor v%s</h1>"
        "<h2>RS485 Status</h2>"
        "<p>DE Pin: <b>GPIO %d</b> %s</p>"
        "<p>HW Mode: <b>%s</b></p>"
        "<h2>Date Curente</h2>"
        "<p>Timp: %s</p>"
        "<p>Ambient: <b>%.1f°C</b></p>"
        "<p>Water In: <b>%.1f°C</b></p>"
        "<p>Water Out: <b>%.1f°C</b></p>"
        "<p>Water Target: <b>%.1f°C</b></p>"
        "<p>Press High: <b>%.2f bar</b></p>"
        "<p>Press Low: <b>%.2f bar</b></p>"
        "<h2>Statistici</h2>"
        "<p>Total citiri: %lu</p>"
        "<p>Succes: %lu (%.1f%%)</p>"
        "<p>Echo detectate: %lu</p>"
        "<hr><a href='/status'>JSON Status</a> | "
        "<a href='/history'>History</a> | "
        "<a href='/download/csv'>Download CSV</a> | "
        "<a href='/pinscan'>Pin Scan</a>"
        "</body></html>",
        FW_VERSION,
        activeDePin, activeDePin == 0 ? "(oficial ✓)" : "(non-standard!)",
        modbus.isHWModeActive() ? "ACTIV ✓" : "INACTIV ✗",
        currentData.dateTime,
        currentData.tempAmbient, currentData.tempWaterInlet,
        currentData.tempWaterOutlet, currentData.tempWaterTarget,
        currentData.pressHigh, currentData.pressLow,
        sysStats.totalReads, sysStats.successfulReads, sysStats.successRate,
        sysStats.echoDetected
    );

    webServer.send(200, "text/html", html);
}

void handleWebStatus() {
    char json[2048];
    snprintf(json, sizeof(json),
        "{"
        "\"fw_version\":\"%s\","
        "\"de_pin\":%d,"
        "\"hw_mode\":%s,"
        "\"uptime_ms\":%lu,"
        "\"data\":{"
          "\"valid\":%s,"
          "\"datetime\":\"%s\","
          "\"temp_ambient\":%.1f,"
          "\"temp_water_in\":%.1f,"
          "\"temp_water_out\":%.1f,"
          "\"temp_water_target\":%.1f,"
          "\"temp_evap_in\":%.1f,"
          "\"temp_evap_out\":%.1f,"
          "\"temp_comp_disch\":%.1f,"
          "\"press_high\":%.2f,"
          "\"press_low\":%.2f,"
          "\"system_status\":\"0x%04X\","
          "\"op_mode\":\"0x%04X\","
          "\"fault\":\"0x%04X\","
          "\"quality\":%d,"
          "\"read_ms\":%lu"
        "},"
        "\"stats\":{"
          "\"total_reads\":%lu,"
          "\"success\":%lu,"
          "\"failed\":%lu,"
          "\"crc_errors\":%lu,"
          "\"timeouts\":%lu,"
          "\"echo_detected\":%lu,"
          "\"success_rate\":%.1f"
        "},"
        "\"rs485\":{"
          "\"tx_count\":%lu,"
          "\"rx_valid\":%lu,"
          "\"rx_echo\":%lu,"
          "\"rx_timeouts\":%lu"
        "}"
        "}",
        FW_VERSION,
        activeDePin,
        modbus.isHWModeActive() ? "true" : "false",
        millis(),
        currentData.valid ? "true" : "false",
        currentData.dateTime,
        currentData.tempAmbient, currentData.tempWaterInlet,
        currentData.tempWaterOutlet, currentData.tempWaterTarget,
        currentData.tempEvapIn, currentData.tempEvapOut, currentData.tempCompDisch,
        currentData.pressHigh, currentData.pressLow,
        currentData.systemStatus, currentData.operatingMode, currentData.faultCode,
        currentData.dataQuality, currentData.readDurationMs,
        sysStats.totalReads, sysStats.successfulReads, sysStats.failedReads,
        sysStats.crcErrors, sysStats.timeoutErrors, sysStats.echoDetected,
        sysStats.successRate,
        modbus.stats.txCount, modbus.stats.rxValidCount,
        modbus.stats.rxEchoCount, modbus.stats.rxTimeouts
    );

    webServer.send(200, "application/json", json);
}

void handleWebHistory() {
    String json = "[";
    int count = min(historyIndex, HISTORY_SIZE);
    for (int i = 0; i < count; i++) {
        HeatPumpData& d = dataHistory[i];
        if (i > 0) json += ",";
        char entry[256];
        snprintf(entry, sizeof(entry),
                 "{\"dt\":\"%s\",\"ambient\":%.1f,\"water_out\":%.1f}",
                 d.dateTime, d.tempAmbient, d.tempWaterOutlet);
        json += entry;
    }
    json += "]";
    webServer.send(200, "application/json", json);
}

void handleWebLogs() {
    if (!sdAvailable) {
        webServer.send(503, "text/plain", "SD card indisponibil");
        return;
    }

    // Trimite ultimele 50 linii din debug log
    File f = SD.open(SD_LOG_FILE_DEBUG);
    if (!f) {
        webServer.send(404, "text/plain", "Log file nu exista");
        return;
    }

    String content = "";
    while (f.available()) {
        String line = f.readStringUntil('\n');
        content += line + "\n";
        // Limiteaza la ultimele ~50KB
        if (content.length() > 50000) {
            content = content.substring(content.length() - 50000);
        }
    }
    f.close();

    webServer.send(200, "text/plain", content);
}

void handleWebDownloadCSV() {
    if (!sdAvailable) {
        webServer.send(503, "text/plain", "SD card indisponibil");
        return;
    }

    File f = SD.open(SD_LOG_FILE_DATA);
    if (!f) {
        webServer.send(404, "text/plain", "CSV nu exista");
        return;
    }

    webServer.streamFile(f, "text/csv");
    f.close();
}

void handleWebPinScan() {
    LOG_I("WEB", "Pin scan solicitat via web...");

    // Ruleaza scan si returneaza rezultat
    uint8_t bestPin = pinTester.scanAll(ModbusSerial);

    // Re-initializeaza cu pinul gasit
    if (bestPin != 0xFF) {
        activeDePin = bestPin;
        modbus.init(ModbusSerial, activeDePin);
    }

    char json[512];
    snprintf(json, sizeof(json),
             "{\"status\":\"done\",\"best_pin\":%d,"
             "\"hw_mode\":%s,\"results_count\":%d}",
             bestPin == 0xFF ? -1 : bestPin,
             modbus.isHWModeActive() ? "true" : "false",
             pinTester.resultCount);

    webServer.send(200, "application/json", json);
}

void handleWebSetLogLevel() {
    if (webServer.hasArg("level")) {
        int level = webServer.arg("level").toInt();
        if (level >= 0 && level <= LOG_LEVEL_VERBOSE) {
            DebugLogger::setLevel(level);
            webServer.send(200, "text/plain", "Log level schimbat");
            return;
        }
    }
    webServer.send(400, "text/plain",
                   "Usage: /loglevel?level=N (0=none, 1=error, 2=warn, 3=info, 4=debug, 5=verbose)");
}

// ============================================================================
// initOTA() - Over-the-Air updates
// ============================================================================
void initOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setPort(OTA_PORT);

    ArduinoOTA.onStart([]() {
        LOG_I("OTA", "Update inceput...");
    });
    ArduinoOTA.onEnd([]() {
        LOG_I("OTA", "Update complet! Restart...");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        LOG_E("OTA", "Eroare: %u", error);
    });

    ArduinoOTA.begin();
    LOG_I("OTA", "OTA activ pe port %d", OTA_PORT);
}

// ============================================================================
// initButtons() - Configureaza butoane
// ============================================================================
void initButtons() {
    pinMode(BUTTON_A_PIN, INPUT_PULLUP);
    pinMode(BUTTON_B_PIN, INPUT_PULLUP);
    pinMode(BUTTON_C_PIN, INPUT_PULLUP);
    LOG_D("BUTTONS", "Butoane configurate: A=%d B=%d C=%d",
          BUTTON_A_PIN, BUTTON_B_PIN, BUTTON_C_PIN);
}

// ============================================================================
// checkButtons() - Verifica butoane
// ============================================================================
static uint32_t lastBtnPress = 0;

void checkButtons() {
    if (millis() - lastBtnPress < 300) return;  // Debounce

    if (digitalRead(BUTTON_A_PIN) == LOW) {
        lastBtnPress = millis();
        LOG_I("BTN", "KEYA: Reset statistici");
        memset(&sysStats, 0, sizeof(sysStats));
        modbus.stats = {0};
        modbus.stats.dePin = activeDePin;
        modbus.stats.hwModeEnabled = modbus.isHWModeActive() ? 1 : 0;
    }

    if (digitalRead(BUTTON_B_PIN) == LOW) {
        lastBtnPress = millis();
        LOG_I("BTN", "KEYB: Citire imediata");
        performRead();
    }

    if (digitalRead(BUTTON_C_PIN) == LOW) {
        lastBtnPress = millis();
        // Cicleaza log level: INFO → DEBUG → VERBOSE → INFO
        uint8_t current = DebugLogger::getLevel();
        uint8_t next;
        if (current <= LOG_LEVEL_INFO)       next = LOG_LEVEL_DEBUG;
        else if (current == LOG_LEVEL_DEBUG) next = LOG_LEVEL_VERBOSE;
        else                                  next = LOG_LEVEL_INFO;
        DebugLogger::setLevel(next);
        LOG_I("BTN", "KEYC: Log level → %d", next);
    }
}

// ============================================================================
// printBanner() - Banner la startup
// ============================================================================
void printBanner() {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.println(F("  HEAT PUMP MONITOR v5.0 - HARDWARE RS485 + SD DEBUG LOGGING"));
    Serial.println(F("  M5Stack StampPLC K141 (ESP32-S3)"));
    Serial.println(F("================================================================================"));
    Serial.println(F("  FIX #1: RS485_DE_PIN = GPIO 0 (pin oficial StamPLC.pdf!)"));
    Serial.println(F("  FIX #2: setMode(UART_MODE_RS485_HALF_DUPLEX) - hardware auto"));
    Serial.println(F("  NEW:    Debug logging complet pe SD card"));
    Serial.println(F("  NEW:    Echo detection automat"));
    Serial.println(F("  NEW:    Pin scan include GPIO 0 (PRIMUL in lista!)"));
    Serial.println(F("================================================================================"));
    Serial.println();
}
