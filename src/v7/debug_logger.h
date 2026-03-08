// ============================================================================
// debug_logger.h - SISTEM DE LOGGING v7
// Identic cu v5 dar cu referinte la config_v7.h si RS485Stats v7
// ============================================================================

#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "config_v7.h"

// ============================================================================
// LOG MACROS
// ============================================================================
#define LOG_E(cat, fmt, ...) DebugLogger::log(LOG_LEVEL_ERROR,   cat, fmt, ##__VA_ARGS__)
#define LOG_W(cat, fmt, ...) DebugLogger::log(LOG_LEVEL_WARN,    cat, fmt, ##__VA_ARGS__)
#define LOG_I(cat, fmt, ...) DebugLogger::log(LOG_LEVEL_INFO,    cat, fmt, ##__VA_ARGS__)
#define LOG_D(cat, fmt, ...) DebugLogger::log(LOG_LEVEL_DEBUG,   cat, fmt, ##__VA_ARGS__)
#define LOG_V(cat, fmt, ...) DebugLogger::log(LOG_LEVEL_VERBOSE, cat, fmt, ##__VA_ARGS__)

#define LOG_HEX_TX(data, len) DebugLogger::logHex("TX", data, len)
#define LOG_HEX_RX(data, len) DebugLogger::logHex("RX", data, len)
#define LOG_TIMING(label, ms) DebugLogger::logTiming(label, ms)
#define LOG_SEPARATOR()       DebugLogger::logSeparator()

// ============================================================================
// DebugLogger CLASS
// ============================================================================
class DebugLogger {
public:

    static void init(bool sdAvail) {
        _sdAvailable  = sdAvail;
        _writeCount   = 0;
        _errorCount   = 0;
        _currentLevel = CURRENT_LOG_LEVEL;

        logSeparator();
        log(LOG_LEVEL_INFO, "SYSTEM", "=== BOOT v%s (%s %s) ===",
            FW_VERSION, FW_BUILD_DATE, FW_BUILD_TIME);
        log(LOG_LEVEL_INFO, "SYSTEM", "RS485: TX=GPIO%d RX=GPIO%d DE=GPIO%d [CONFIRMAT]",
            RS485_TX_PIN, RS485_RX_PIN, RS485_DE_PIN);
        log(LOG_LEVEL_INFO, "SYSTEM", "SD Card: %s",
            sdAvail ? "DISPONIBIL" : "LIPSA - log doar serial!");
        log(LOG_LEVEL_INFO, "SYSTEM", "Log Level: %s", levelName(_currentLevel));
        logSeparator();
    }

    static void setLevel(uint8_t level) {
        _currentLevel = level;
        log(LOG_LEVEL_INFO, "SYSTEM", "Log level → %s", levelName(level));
    }

    static uint8_t getLevel() { return _currentLevel; }

    static void log(uint8_t level, const char* category, const char* fmt, ...) {
        if (level > _currentLevel) return;

        char msg[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        char line[640];
        snprintf(line, sizeof(line), "[%8lu] [%s] [%-6s] %s",
                 millis(), levelChar(level), category, msg);

        if (LOG_TO_SERIAL) {
            Serial.println(line);
            if (level == LOG_LEVEL_ERROR) Serial.flush();
        }

        if (LOG_TO_SD && _sdAvailable) {
            const char* f = (level == LOG_LEVEL_ERROR)
                            ? SD_LOG_FILE_ERRORS : SD_LOG_FILE_DEBUG;
            writeToSD(f, line);
            if (level == LOG_LEVEL_ERROR) writeToSD(SD_LOG_FILE_DEBUG, line);
        }

        if (level == LOG_LEVEL_ERROR) _errorCount++;
    }

    static void logHex(const char* dir, const uint8_t* data, size_t len) {
        if (LOG_LEVEL_DEBUG > _currentLevel) return;
        char hexStr[len * 3 + 8];
        hexStr[0] = '\0';
        for (size_t i = 0; i < len; i++) {
            char b[4]; snprintf(b, sizeof(b), "%02X ", data[i]);
            strncat(hexStr, b, sizeof(hexStr) - strlen(hexStr) - 1);
        }
        log(LOG_LEVEL_DEBUG, "RS485", "%s [%zu bytes]: %s", dir, len, hexStr);
    }

    static void logTiming(const char* label, uint32_t ms) {
        if (!LOG_TIMING || LOG_LEVEL_DEBUG > _currentLevel) return;
        log(LOG_LEVEL_DEBUG, "TIMING", "%s: %lu ms", label, ms);
    }

    static void logEchoDetected(const uint8_t* tx, const uint8_t* rx, size_t len) {
        log(LOG_LEVEL_ERROR, "RS485", "ECHO DETECTAT! TX=RX (DE pin nu comuta)");
        char txH[64] = "", rxH[64] = "";
        for (size_t i = 0; i < len && i < 16; i++) {
            char b[4];
            snprintf(b, sizeof(b), "%02X ", tx[i]); strncat(txH, b, sizeof(txH)-strlen(txH)-1);
            snprintf(b, sizeof(b), "%02X ", rx[i]); strncat(rxH, b, sizeof(rxH)-strlen(rxH)-1);
        }
        log(LOG_LEVEL_ERROR, "RS485", "  TX: %s", txH);
        log(LOG_LEVEL_ERROR, "RS485", "  RX: %s", rxH);
    }

    static void logRS485Stats(const RS485Stats& s) {
        logSeparator();
        log(LOG_LEVEL_INFO, "RS485", "=== STATISTICI RS485 v7 ===");
        log(LOG_LEVEL_INFO, "RS485", "  TX=GPIO%d  RX=GPIO%d  DE=GPIO%d",
            s.txPin, s.rxPin, s.dePin);
        log(LOG_LEVEL_INFO, "RS485", "  Baud: %lu bps", s.baudrate);
        log(LOG_LEVEL_INFO, "RS485", "  HW Mode: %s",
            s.hwModeEnabled ? "ACTIV" : "MANUAL");
        log(LOG_LEVEL_INFO, "RS485", "  TX=%lu  RX=%lu  Valid=%lu",
            s.txCount, s.rxCount, s.rxValidCount);
        log(LOG_LEVEL_INFO, "RS485", "  Echo=%lu  CRC_Err=%lu  Timeout=%lu",
            s.rxEchoCount, s.rxCrcErrors, s.rxTimeouts);
        logSeparator();
    }

    static void logSystemStats(const SystemStats& s) {
        logSeparator();
        log(LOG_LEVEL_INFO, "SYSTEM", "=== STATISTICI SISTEM ===");
        log(LOG_LEVEL_INFO, "SYSTEM", "  Uptime: %lu sec", s.uptime / 1000);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Citiri: %lu total, %lu succes (%.1f%%), %lu erori",
            s.totalReads, s.successfulReads, s.successRate, s.failedReads);
        log(LOG_LEVEL_INFO, "SYSTEM", "  CRC=%lu  Timeout=%lu  Echo=%lu",
            s.crcErrors, s.timeoutErrors, s.echoDetected);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Avg read: %.0f ms", s.avgReadTimeMs);
        logSeparator();
    }

    static void logHeatPumpData(const HeatPumpData& d) {
        if (!d.valid) { log(LOG_LEVEL_WARN, "DATA", "Date invalide"); return; }
        log(LOG_LEVEL_INFO, "DATA", "=== DATE POMPA (%s) ===", d.dateTime);
        log(LOG_LEVEL_INFO, "DATA", "  Ambient:      %.1f°C", d.tempAmbient);
        log(LOG_LEVEL_INFO, "DATA", "  Water In/Out: %.1f / %.1f°C",
            d.tempWaterInlet, d.tempWaterOutlet);
        log(LOG_LEVEL_INFO, "DATA", "  Water Target: %.1f°C", d.tempWaterTarget);
        log(LOG_LEVEL_INFO, "DATA", "  Evap In/Out:  %.1f / %.1f°C",
            d.tempEvapIn, d.tempEvapOut);
        log(LOG_LEVEL_INFO, "DATA", "  Comp Disch:   %.1f°C", d.tempCompDisch);
        log(LOG_LEVEL_INFO, "DATA", "  Press H/L:    %.2f / %.2f bar",
            d.pressHigh, d.pressLow);
        log(LOG_LEVEL_INFO, "DATA", "  Status=0x%04X Mode=0x%04X Fault=0x%04X Quality=%d%%",
            d.systemStatus, d.operatingMode, d.faultCode, d.dataQuality);
    }

    static void logSeparator() {
        if (LOG_TO_SERIAL)
            Serial.println(F("================================================================================"));
        if (LOG_TO_SD && _sdAvailable)
            writeToSD(SD_LOG_FILE_DEBUG,
                      "================================================================================");
    }

    static bool writeCSV(const HeatPumpData& d) {
        if (!_sdAvailable) return false;

        if (!SD.exists(SD_LOG_FILE_DATA)) {
            File f = SD.open(SD_LOG_FILE_DATA, FILE_WRITE);
            if (f) {
                f.println("Timestamp,DateTime,TempAmbient,TempWaterIn,TempWaterOut,"
                          "TempWaterTarget,TempEvapIn,TempEvapOut,TempCompDisch,"
                          "PressHigh,PressLow,SystemStatus,OpMode,FaultCode,"
                          "DataQuality,ReadTimeMs");
                f.close();
            }
        }

        File f = SD.open(SD_LOG_FILE_DATA, FILE_APPEND);
        if (!f) return false;

        char line[256];
        snprintf(line, sizeof(line),
                 "%lu,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,"
                 "0x%04X,0x%04X,0x%04X,%d,%lu",
                 d.timestamp, d.dateTime,
                 d.tempAmbient, d.tempWaterInlet, d.tempWaterOutlet, d.tempWaterTarget,
                 d.tempEvapIn, d.tempEvapOut, d.tempCompDisch,
                 d.pressHigh, d.pressLow,
                 d.systemStatus, d.operatingMode, d.faultCode,
                 d.dataQuality, d.readDurationMs);
        f.println(line);
        f.close();
        return true;
    }

    static void writeBootLog(const char* info) {
        if (!_sdAvailable) return;
        File f = SD.open(SD_LOG_FILE_BOOT, FILE_APPEND);
        if (f) {
            char line[256];
            snprintf(line, sizeof(line), "[%8lu] %s", millis(), info);
            f.println(line); f.close();
        }
    }

    static bool sdAvailable()             { return _sdAvailable; }
    static void setSDAvailable(bool avail){ _sdAvailable = avail; }
    static uint32_t getErrorCount()       { return _errorCount; }

private:

    static uint8_t  _currentLevel;
    static bool     _sdAvailable;
    static uint32_t _writeCount;
    static uint32_t _errorCount;

    static void writeToSD(const char* filename, const char* line) {
        File f = SD.open(filename, FILE_APPEND);
        if (!f) {
            if (LOG_TO_SERIAL) Serial.printf("[SD ERR] %s\n", filename);
            return;
        }
        f.println(line);
        _writeCount++;
        if (_writeCount % SD_LOG_FLUSH_EVERY == 0) f.flush();
        f.close();
    }

    static const char* levelChar(uint8_t l) {
        switch (l) {
            case LOG_LEVEL_ERROR:   return "E";
            case LOG_LEVEL_WARN:    return "W";
            case LOG_LEVEL_INFO:    return "I";
            case LOG_LEVEL_DEBUG:   return "D";
            case LOG_LEVEL_VERBOSE: return "V";
            default:                return "?";
        }
    }

    static const char* levelName(uint8_t l) {
        switch (l) {
            case LOG_LEVEL_NONE:    return "NONE";
            case LOG_LEVEL_ERROR:   return "ERROR";
            case LOG_LEVEL_WARN:    return "WARN";
            case LOG_LEVEL_INFO:    return "INFO";
            case LOG_LEVEL_DEBUG:   return "DEBUG";
            case LOG_LEVEL_VERBOSE: return "VERBOSE";
            default:                return "?";
        }
    }
};

// Static members
uint8_t  DebugLogger::_currentLevel = LOG_LEVEL_INFO;
bool     DebugLogger::_sdAvailable  = false;
uint32_t DebugLogger::_writeCount   = 0;
uint32_t DebugLogger::_errorCount   = 0;
