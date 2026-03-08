// ============================================================================
// main_v7.cpp - HEAT PUMP MONITOR v7.0
// M5Stack StampPLC K141 (ESP32-S3)
// ============================================================================
// v7.0 - PINI CONFIRMATI DIN LOG + BAUD SCAN + INTER-BYTE GAP FIX
//
//   CONFIRMAT din log v5 (2026-03-08):
//     SD:    SCK=7  MOSI=8  MISO=9  CS=10  (K141-v1.0, "OK" in log)
//     RS485: TX=0   RX=39   DE=46          (CRC errors=semnal real!)
//     Hardware setMode(RS485_HALF_DUPLEX) functioneaza.
//
//   DEZACTIVAT: BUTTON_A (GPIO39 = RS485 RX, SIT3088 tine linia LOW)
//   ACTIV:      BUTTON_B (GPIO40), BUTTON_C (GPIO41)
//
//   BAUD SCAN: La primul boot (NVS gol), incearca 9600/4800/19200/38400
//              Salveaza baud-ul corect in NVS pentru booturi viitoare.
//
//   FIX PRINCIPAL: inter-byte gap 20ms (in loc de delay(50/100ms) fix)
//     → captureaza frame Modbus complet la orice baud rate
//
// FLOW BOOT:
//   1. SD init (pini confirmati, fara scan)
//   2. Logger init
//   3. WiFi + NTP
//   4. Modbus init:
//      a. Incarca TX/RX/DE/baud din NVS
//      b. Daca NVS gol sau baud=0: ruleaza scanBaudRate()
//      c. Daca scanBaudRate esueaza: ruleaza pin scan (fallback)
//   5. Web server + OTA
//   6. Prima citire
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <time.h>
#include <SPI.h>
#include <SD.h>

#include "config_v7.h"
#include "debug_logger.h"
#include "modbus_v7.h"
#include "pin_test_v7.h"

// ============================================================================
// GLOBALS
// ============================================================================
HardwareSerial ModbusSerial(1);
WebServer      webServer(WEB_SERVER_PORT);

ModbusV7    modbus;
PinTesterV7 pinTester;

HeatPumpData  currentData;
SystemStats   sysStats;

uint32_t lastReadTime    = 0;
uint32_t lastCSVTime     = 0;
uint32_t lastStatsTime   = 0;
bool     sdAvailable     = false;
bool     wifiConnected   = false;
bool     modbusOK        = false;

// Pini activi - initializati din NVS sau scan
uint8_t  activeTxPin     = RS485_TX_PIN;   // 0
uint8_t  activeRxPin     = RS485_RX_PIN;   // 39
uint8_t  activeDePin     = RS485_DE_PIN;   // 46
uint32_t activeBaud      = MODBUS_BAUDRATE; // 9600 default

#define HISTORY_SIZE 60
HeatPumpData dataHistory[HISTORY_SIZE];
int          historyIndex = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
bool  initSD();
bool  initWifi();
bool  initNTP();
bool  initModbus();
void  initWebServer();
void  initOTA();
void  initButtons();
bool  performRead();
bool  decodeHeatPumpData(const uint8_t* b1, size_t l1,
                          const uint8_t* b2, size_t l2,
                          const uint8_t* b3, size_t l3,
                          HeatPumpData& data);
float decodeTemperature(const uint8_t* data, int byteOffset);
void  getDateTimeString(char* buf, size_t len);
void  handleWebRoot();
void  handleWebStatus();
void  handleWebHistory();
void  handleWebLogs();
void  handleWebDownloadCSV();
void  handleWebPinScan();
void  handleWebBaudScan();
void  handleWebSetLogLevel();
void  checkButtons();
void  printBanner();

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(LOG_SERIAL_BAUD);
    delay(500);
    printBanner();

    // 1. SD card (pini confirmati, fara scan)
    sdAvailable = initSD();
    DebugLogger::init(sdAvailable);

    char bootMsg[160];
    snprintf(bootMsg, sizeof(bootMsg),
             "BOOT v%s | TX=%d RX=%d DE=%d | Build: %s %s",
             FW_VERSION, RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN,
             FW_BUILD_DATE, FW_BUILD_TIME);
    DebugLogger::writeBootLog(bootMsg);

    // 2. WiFi
    wifiConnected = initWifi();

    // 3. NTP
    if (wifiConnected) initNTP();

    // 4. Modbus RS485 - pini confirmati + baud scan
    modbusOK = initModbus();

    // 5. Web + OTA
    if (wifiConnected) {
        initWebServer();
        initOTA();
    }

    // 6. Butoane (BUTTON_A dezactivat - GPIO39 = RS485 RX)
    initButtons();

    memset(&currentData, 0, sizeof(currentData));
    memset(&sysStats,    0, sizeof(sysStats));

    LOG_SEPARATOR();
    LOG_I("BOOT", "=== SETUP COMPLET v%s ===", FW_VERSION);
    LOG_I("BOOT", "RS485: TX=GPIO%d RX=GPIO%d DE=GPIO%d Baud=%lu",
          activeTxPin, activeRxPin, activeDePin, activeBaud);
    LOG_I("BOOT", "HW Mode: %s", modbus.isHWModeActive() ? "ACTIV ✓" : "MANUAL");
    LOG_I("BOOT", "SD: %s | WiFi: %s",
          sdAvailable ? "OK ✓" : "LIPSA",
          wifiConnected ? WiFi.localIP().toString().c_str() : "OFFLINE");
    LOG_I("BOOT", "BUTTON_A: DEZACTIVAT (GPIO39 = RS485 RX)");
    LOG_I("BOOT", "BUTTON_B=GPIO40 BUTTON_C=GPIO41 active.");
    LOG_SEPARATOR();

    if (modbusOK) performRead();
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    uint32_t now = millis();

    if (wifiConnected) {
        ArduinoOTA.handle();
        webServer.handleClient();
    }

    if (now - lastReadTime >= READ_INTERVAL_MS) {
        lastReadTime = now;
        performRead();
    }

    if (now - lastCSVTime >= (CSV_LOG_INTERVAL_SEC * 1000UL)) {
        lastCSVTime = now;
        if (currentData.valid) DebugLogger::writeCSV(currentData);
    }

    if (now - lastStatsTime >= STATS_PRINT_INTERVAL_MS) {
        lastStatsTime = now;
        sysStats.uptime = millis();
        if (sysStats.totalReads > 0)
            sysStats.successRate =
                (float)sysStats.successfulReads / sysStats.totalReads * 100.0f;
        DebugLogger::logSystemStats(sysStats);
        modbus.printStats();
    }

    checkButtons();
    delay(10);
}

// ============================================================================
// initSD() - Pini CONFIRMATI (SCK=7 MOSI=8 MISO=9 CS=10)
// Nu mai facem scan - pinii sunt cunoscuti din log v5.
// ============================================================================
bool initSD() {
    Serial.println("[SD] Init cu pini confirmati: SCK=7 MOSI=8 MISO=9 CS=10");

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    delay(10);

    if (SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQ)) {
        uint64_t mb = SD.cardSize() / (1024 * 1024);
        Serial.printf("[SD] OK! %llu MB\n", mb);
        return true;
    }

    Serial.println("[SD] EROARE! Verifica card si contacte.");
    return false;
}

// ============================================================================
// initWifi()
// ============================================================================
bool initWifi() {
    LOG_I("WIFI", "Conectare la %s...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > WIFI_TIMEOUT_MS) {
            LOG_W("WIFI", "Timeout - continuam fara WiFi");
            return false;
        }
        delay(500);
    }

    LOG_I("WIFI", "Conectat! IP: %s", WiFi.localIP().toString().c_str());
    return true;
}

// ============================================================================
// initNTP()
// ============================================================================
bool initNTP() {
    LOG_I("NTP", "Sincronizare...");
    configTime(NTP_TIMEZONE_OFFSET, NTP_DAYLIGHT_OFFSET, NTP_SERVER1, NTP_SERVER2);
    struct tm tm;
    uint32_t t0 = millis();
    while (!getLocalTime(&tm)) {
        if (millis() - t0 > 5000) { LOG_W("NTP", "Timeout!"); return false; }
        delay(200);
    }
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    LOG_I("NTP", "OK: %s", buf);
    return true;
}

// ============================================================================
// initModbus() - Pini confirmati + baud scan la primul boot
//
// Logica:
//   1. Incearca NVS: daca TX/RX/DE/baud salvate, foloseste-le direct.
//   2. Daca NVS gol sau baud=0: init cu pini confirmati + scanBaudRate().
//   3. Daca baud scan esueaza: pin scan ca fallback extins.
// ============================================================================
bool initModbus() {
    LOG_I("MODBUS", "=== INIT MODBUS v7 ===");
    LOG_I("MODBUS", "Pini CONFIRMATI: TX=%d RX=%d DE=%d", RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);
    LOG_I("MODBUS", "BUTTON_A dezactivat: GPIO%d = RS485 RX (SIT3088 RO)", RS485_RX_PIN);

    uint8_t  nvsTx = RS485_TX_PIN;
    uint8_t  nvsRx = RS485_RX_PIN;
    uint32_t nvsBaud = 0;
    uint8_t  nvsDe = PinTesterV7::loadFromNVS(&nvsTx, &nvsRx, &nvsBaud);

    if (nvsDe != 0xFF && nvsBaud > 0) {
        // Config completa in NVS - folosim direct
        LOG_I("MODBUS", "NVS OK: TX=%d RX=%d DE=%d Baud=%lu", nvsTx, nvsRx, nvsDe, nvsBaud);
        activeTxPin = nvsTx;
        activeRxPin = nvsRx;
        activeDePin = nvsDe;
        activeBaud  = nvsBaud;

        modbus.init(ModbusSerial, activeTxPin, activeRxPin, activeDePin, activeBaud);

        // Test rapid: un singur bloc
        uint8_t b1[256]; size_t l1 = 0;
        l1 = modbus.readBlock(MODBUS_SLAVE_ID, BLOCK1_ADDR, BLOCK1_COUNT, b1, sizeof(b1));
        if (l1 > 0) {
            LOG_I("MODBUS", "✓ Config NVS functioneaza! Block1=%zu bytes", l1);
            return true;
        }

        LOG_W("MODBUS", "Config NVS esuat → baud scan...");
    }

    // Pini confirmati (TX=0 RX=39 DE=46), baud necunoscut sau NVS gol
    activeTxPin = RS485_TX_PIN;  // 0
    activeRxPin = RS485_RX_PIN;  // 39
    activeDePin = RS485_DE_PIN;  // 46

    modbus.init(ModbusSerial, activeTxPin, activeRxPin, activeDePin, MODBUS_BAUDRATE);

    // Baud scan
    LOG_I("MODBUS", "Rulare baud scan: {9600, 4800, 19200, 38400}...");
    uint32_t foundBaud = modbus.scanBaudRate(ModbusSerial);

    if (foundBaud > 0) {
        activeBaud = foundBaud;
        PinTesterV7::saveToNVS(activeDePin, activeTxPin, activeRxPin, activeBaud);
        LOG_I("MODBUS", "✓ Baud confirmat: %lu bps", activeBaud);
        return true;
    }

    // Baud scan esuat → pin scan ca fallback
    LOG_W("MODBUS", "Baud scan esuat → pin scan complet...");
    uint8_t bestDe = pinTester.scanAll(ModbusSerial);

    if (bestDe != 0xFF) {
        activeDePin = bestDe;
        activeTxPin = pinTester.bestTxPin;
        activeRxPin = pinTester.bestRxPin;
        // activeBaud ramane 9600

        modbus.init(ModbusSerial, activeTxPin, activeRxPin, activeDePin, MODBUS_BAUDRATE);

        // Incearca din nou baud scan cu noii pini
        uint32_t b2 = modbus.scanBaudRate(ModbusSerial);
        if (b2 > 0) {
            activeBaud = b2;
            PinTesterV7::saveToNVS(activeDePin, activeTxPin, activeRxPin, activeBaud);
        }

        LOG_I("MODBUS", "Modbus pornit: TX=%d RX=%d DE=%d Baud=%lu",
              activeTxPin, activeRxPin, activeDePin, activeBaud);
        return true;
    }

    LOG_E("MODBUS", "ESEC TOTAL! Verifica conexiunile RS485 (A/B la pompa).");
    return false;
}

// ============================================================================
// performRead()
// ============================================================================
bool performRead() {
    uint32_t t0 = millis();
    sysStats.totalReads++;

    uint8_t b1[256], b2[256], b3[64];
    size_t  l1 = 0, l2 = 0, l3 = 0;

    bool anyOk = modbus.readBlock3(b1, &l1, b2, &l2, b3, &l3);
    uint32_t readMs = millis() - t0;
    LOG_TIMING("Total read", readMs);

    if (!anyOk) {
        sysStats.failedReads++;
        sysStats.crcErrors    += modbus.stats.rxCrcErrors;
        sysStats.timeoutErrors += modbus.stats.rxTimeouts;
        LOG_E("MODBUS", "Citire esuata dupa %lu ms", readMs);
        return false;
    }

    HeatPumpData nd;
    memset(&nd, 0, sizeof(nd));
    nd.lastReadTime   = millis();
    nd.readDurationMs = readMs;
    getDateTimeString(nd.dateTime, sizeof(nd.dateTime));
    nd.timestamp = time(nullptr);

    if (decodeHeatPumpData(b1, l1, b2, l2, b3, l3, nd)) {
        currentData = nd;
        dataHistory[historyIndex % HISTORY_SIZE] = currentData;
        historyIndex++;
        sysStats.successfulReads++;
        sysStats.avgReadTimeMs =
            (sysStats.avgReadTimeMs * (sysStats.successfulReads - 1) + readMs)
            / sysStats.successfulReads;
        DebugLogger::logHeatPumpData(currentData);
        LOG_I("MODBUS", "✓ OK: Ambient=%.1f°C WaterOut=%.1f°C [%lu ms]",
              currentData.tempAmbient, currentData.tempWaterOutlet, readMs);
        return true;
    }

    sysStats.failedReads++;
    return false;
}

// ============================================================================
// decodeHeatPumpData()
// ============================================================================
bool decodeHeatPumpData(const uint8_t* b1, size_t l1,
                         const uint8_t* b2, size_t l2,
                         const uint8_t* b3, size_t l3,
                         HeatPumpData& data) {
    data.dataQuality = 0;
    data.valid = false;

    if (l1 >= 5) {
        const uint8_t* r = b1 + 3;
        data.systemStatus  = (r[0] << 8) | r[1];
        data.operatingMode = (r[2] << 8) | r[3];
        if (l1 >= 11) {
            data.faultCode    = (r[6] << 8) | r[7];
            data.runningHours = (r[8] << 8) | r[9];
        }
        data.dataQuality += 33;
    }

    if (l2 >= 5) {
        const uint8_t* r = b2 + 3;
        data.tempAmbient   = decodeTemperature(r, 14 * 2);
        data.tempEvapIn    = decodeTemperature(r,  0 * 2);
        data.tempEvapOut   = decodeTemperature(r,  2 * 2);
        data.tempCompDisch = decodeTemperature(r,  4 * 2);
        if (l2 >= (size_t)(3 + 20 * 2)) {
            int16_t p = (r[18*2] << 8) | r[18*2+1]; data.pressHigh = p / 100.0f;
            p = (r[19*2] << 8) | r[19*2+1];          data.pressLow  = p / 100.0f;
        }
        data.dataQuality += 33;
    }

    if (l3 >= 5) {
        const uint8_t* r = b3 + 3;
        data.tempWaterInlet  = decodeTemperature(r, 0 * 2);
        data.tempWaterOutlet = decodeTemperature(r, 1 * 2);
        data.tempWaterTarget = decodeTemperature(r, 2 * 2);
        data.dataQuality += 34;
    }

    data.valid = (data.dataQuality > 0);
    return data.valid;
}

float decodeTemperature(const uint8_t* raw, int off) {
    if (!raw) return -999.0f;
    int16_t v = (int16_t)((raw[off] << 8) | raw[off + 1]);
    float t = v / 10.0f;
    return (t >= -50.0f && t <= 150.0f) ? t : -999.0f;
}

void getDateTimeString(char* buf, size_t len) {
    struct tm tm;
    if (getLocalTime(&tm)) strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
    else snprintf(buf, len, "uptime_%lu", millis() / 1000);
}

// ============================================================================
// WEB SERVER
// ============================================================================
void initWebServer() {
    webServer.on("/",            HTTP_GET, handleWebRoot);
    webServer.on("/status",      HTTP_GET, handleWebStatus);
    webServer.on("/history",     HTTP_GET, handleWebHistory);
    webServer.on("/logs",        HTTP_GET, handleWebLogs);
    webServer.on("/download/csv",HTTP_GET, handleWebDownloadCSV);
    webServer.on("/pinscan",     HTTP_GET, handleWebPinScan);
    webServer.on("/baudscan",    HTTP_GET, handleWebBaudScan);
    webServer.on("/loglevel",    HTTP_GET, handleWebSetLogLevel);
    webServer.begin();

    LOG_I("WEB", "Server port %d", WEB_SERVER_PORT);
    LOG_I("WEB", "  / /status /history /logs /download/csv");
    LOG_I("WEB", "  /pinscan /baudscan /loglevel?level=N");
}

void handleWebRoot() {
    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>Heat Pump v7</title>"
        "<meta http-equiv='refresh' content='15'></head><body>"
        "<h1>Heat Pump Monitor v%s</h1>"
        "<h2>RS485 [CONFIRMAT]</h2>"
        "<p>TX=GPIO%d RX=GPIO%d DE=GPIO%d Baud=%lu</p>"
        "<p>HW Mode: <b>%s</b></p>"
        "<h2>Date Curente (%s)</h2>"
        "<p>Ambient: <b>%.1f°C</b></p>"
        "<p>Water In/Out: <b>%.1f / %.1f°C</b></p>"
        "<p>Water Target: <b>%.1f°C</b></p>"
        "<p>Press High/Low: <b>%.2f / %.2f bar</b></p>"
        "<h2>Statistici</h2>"
        "<p>Citiri: %lu total, %lu succes (%.1f%%)</p>"
        "<hr>"
        "<a href='/status'>JSON</a> | "
        "<a href='/history'>History</a> | "
        "<a href='/download/csv'>CSV</a> | "
        "<a href='/pinscan'>Pin Scan</a> | "
        "<a href='/baudscan'>Baud Scan</a>"
        "</body></html>",
        FW_VERSION,
        activeTxPin, activeRxPin, activeDePin, activeBaud,
        modbus.isHWModeActive() ? "HARDWARE ✓" : "MANUAL",
        currentData.dateTime,
        currentData.tempAmbient,
        currentData.tempWaterInlet, currentData.tempWaterOutlet,
        currentData.tempWaterTarget,
        currentData.pressHigh, currentData.pressLow,
        sysStats.totalReads, sysStats.successfulReads, sysStats.successRate
    );
    webServer.send(200, "text/html", html);
}

void handleWebStatus() {
    char json[2048];
    snprintf(json, sizeof(json),
        "{"
        "\"fw\":\"%s\","
        "\"rs485\":{\"tx\":%d,\"rx\":%d,\"de\":%d,\"baud\":%lu,\"hw_mode\":%s},"
        "\"data\":{"
          "\"valid\":%s,\"dt\":\"%s\","
          "\"t_amb\":%.1f,\"t_w_in\":%.1f,\"t_w_out\":%.1f,\"t_w_tgt\":%.1f,"
          "\"t_evap_in\":%.1f,\"t_evap_out\":%.1f,\"t_comp\":%.1f,"
          "\"p_hi\":%.2f,\"p_lo\":%.2f,"
          "\"status\":\"0x%04X\",\"mode\":\"0x%04X\",\"fault\":\"0x%04X\","
          "\"quality\":%d,\"read_ms\":%lu"
        "},"
        "\"stats\":{\"reads\":%lu,\"ok\":%lu,\"fail\":%lu,"
          "\"crc\":%lu,\"tout\":%lu,\"rate\":%.1f}"
        "}",
        FW_VERSION,
        activeTxPin, activeRxPin, activeDePin, activeBaud,
        modbus.isHWModeActive() ? "true" : "false",
        currentData.valid ? "true" : "false", currentData.dateTime,
        currentData.tempAmbient,
        currentData.tempWaterInlet, currentData.tempWaterOutlet, currentData.tempWaterTarget,
        currentData.tempEvapIn, currentData.tempEvapOut, currentData.tempCompDisch,
        currentData.pressHigh, currentData.pressLow,
        currentData.systemStatus, currentData.operatingMode, currentData.faultCode,
        currentData.dataQuality, currentData.readDurationMs,
        sysStats.totalReads, sysStats.successfulReads, sysStats.failedReads,
        sysStats.crcErrors, sysStats.timeoutErrors, sysStats.successRate
    );
    webServer.send(200, "application/json", json);
}

void handleWebHistory() {
    String json = "[";
    int n = min(historyIndex, HISTORY_SIZE);
    for (int i = 0; i < n; i++) {
        HeatPumpData& d = dataHistory[i];
        if (i > 0) json += ",";
        char e[128];
        snprintf(e, sizeof(e), "{\"dt\":\"%s\",\"amb\":%.1f,\"wo\":%.1f}",
                 d.dateTime, d.tempAmbient, d.tempWaterOutlet);
        json += e;
    }
    json += "]";
    webServer.send(200, "application/json", json);
}

void handleWebLogs() {
    if (!sdAvailable) { webServer.send(503, "text/plain", "SD indisponibil"); return; }
    File f = SD.open(SD_LOG_FILE_DEBUG);
    if (!f) { webServer.send(404, "text/plain", "Log inexistent"); return; }
    String c = "";
    while (f.available()) {
        c += f.readStringUntil('\n') + "\n";
        if (c.length() > 50000) c = c.substring(c.length() - 50000);
    }
    f.close();
    webServer.send(200, "text/plain", c);
}

void handleWebDownloadCSV() {
    if (!sdAvailable) { webServer.send(503, "text/plain", "SD indisponibil"); return; }
    File f = SD.open(SD_LOG_FILE_DATA);
    if (!f) { webServer.send(404, "text/plain", "CSV inexistent"); return; }
    webServer.streamFile(f, "text/csv");
    f.close();
}

void handleWebPinScan() {
    LOG_I("WEB", "Pin scan via web...");
    uint8_t de = pinTester.scanAll(ModbusSerial);
    if (de != 0xFF) {
        activeDePin = de;
        activeTxPin = pinTester.bestTxPin;
        activeRxPin = pinTester.bestRxPin;
        modbus.init(ModbusSerial, activeTxPin, activeRxPin, activeDePin, activeBaud);
    }
    char json[256];
    snprintf(json, sizeof(json),
             "{\"tx\":%d,\"rx\":%d,\"de\":%d,\"baud\":%lu,\"hw\":%s}",
             activeTxPin, activeRxPin, activeDePin, activeBaud,
             modbus.isHWModeActive() ? "true" : "false");
    webServer.send(200, "application/json", json);
}

void handleWebBaudScan() {
    LOG_I("WEB", "Baud scan via web...");
    uint32_t b = modbus.scanBaudRate(ModbusSerial);
    if (b > 0) {
        activeBaud = b;
        PinTesterV7::saveToNVS(activeDePin, activeTxPin, activeRxPin, activeBaud);
    }
    char json[128];
    snprintf(json, sizeof(json), "{\"baud\":%lu,\"ok\":%s}", b, b > 0 ? "true" : "false");
    webServer.send(200, "application/json", json);
}

void handleWebSetLogLevel() {
    if (webServer.hasArg("level")) {
        int l = webServer.arg("level").toInt();
        if (l >= 0 && l <= LOG_LEVEL_VERBOSE) {
            DebugLogger::setLevel(l);
            webServer.send(200, "text/plain", "OK");
            return;
        }
    }
    webServer.send(400, "text/plain", "Usage: /loglevel?level=0-5");
}

// ============================================================================
// OTA
// ============================================================================
void initOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.onStart([]() { LOG_I("OTA", "Update start..."); });
    ArduinoOTA.onEnd([]()   { LOG_I("OTA", "Update complet!"); });
    ArduinoOTA.onError([](ota_error_t e) { LOG_E("OTA", "Eroare %u", e); });
    ArduinoOTA.begin();
    LOG_I("OTA", "OTA activ: %s port %d", OTA_HOSTNAME, OTA_PORT);
}

// ============================================================================
// BUTTONS - BUTTON_A dezactivat (GPIO39 = RS485 RX)
// ============================================================================
void initButtons() {
    // BUTTON_A_PIN = 0xFF = dezactivat
    if (BUTTON_B_PIN != 0xFF) pinMode(BUTTON_B_PIN, INPUT_PULLUP);
    if (BUTTON_C_PIN != 0xFF) pinMode(BUTTON_C_PIN, INPUT_PULLUP);
    LOG_I("BTN", "Butoane: A=DEZACTIVAT(GPIO39=RS485_RX) B=GPIO%d C=GPIO%d",
          BUTTON_B_PIN, BUTTON_C_PIN);
}

static bool lastB = HIGH, lastC = HIGH;
static uint32_t dbB = 0, dbC = 0;

void checkButtons() {
    uint32_t now = millis();

    if (BUTTON_B_PIN != 0xFF) {
        bool sB = digitalRead(BUTTON_B_PIN);
        if (sB == LOW && lastB == HIGH && now - dbB >= 300) {
            dbB = now;
            LOG_I("BTN", "KEYB: Citire imediata");
            performRead();
        }
        lastB = sB;
    }

    if (BUTTON_C_PIN != 0xFF) {
        bool sC = digitalRead(BUTTON_C_PIN);
        if (sC == LOW && lastC == HIGH && now - dbC >= 300) {
            dbC = now;
            uint8_t cur = DebugLogger::getLevel();
            uint8_t nxt = (cur <= LOG_LEVEL_INFO) ? LOG_LEVEL_DEBUG :
                          (cur == LOG_LEVEL_DEBUG) ? LOG_LEVEL_VERBOSE : LOG_LEVEL_INFO;
            DebugLogger::setLevel(nxt);
            LOG_I("BTN", "KEYC: Log level → %d", nxt);
        }
        lastC = sC;
    }
}

// ============================================================================
// BANNER
// ============================================================================
void printBanner() {
    Serial.println();
    Serial.println(F("================================================================================"));
    Serial.println(F("  HEAT PUMP MONITOR v7.0 - PINI CONFIRMATI + BAUD SCAN"));
    Serial.println(F("  M5Stack StampPLC K141 (ESP32-S3)"));
    Serial.println(F("================================================================================"));
    Serial.println(F("  [CONFIRMAT] SD:    SCK=7 MOSI=8 MISO=9 CS=10 (K141-v1.0)"));
    Serial.println(F("  [CONFIRMAT] RS485: TX=0  RX=39  DE=46  (SIT3088, CRC=semnal real)"));
    Serial.println(F("  [FIX]       Inter-byte gap 20ms (delay fix era insuficient!)"));
    Serial.println(F("  [NOU]       Baud scan: 9600/4800/19200/38400"));
    Serial.println(F("  [DEZACTIVAT] BUTTON_A: GPIO39 = RS485 RX!"));
    Serial.println(F("================================================================================"));
    Serial.println();
}
