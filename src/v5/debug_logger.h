// ============================================================================
// debug_logger.h - SISTEM COMPLET DE DEBUG LOGGING
// Versiune 5.0 - Salvare pe SD card + Serial
// ============================================================================
// Features:
//   - Log levels: ERROR, WARN, INFO, DEBUG, VERBOSE
//   - Output: Serial + SD card simultan
//   - Timestamp la fiecare linie
//   - Categorii: [RS485] [MODBUS] [SD] [WIFI] [SYSTEM]
//   - Hex dump pentru pachete RS485
//   - Timing profiler (ms per operatie)
//   - Detectie echo RS485
//   - Auto-flush la erori critice
// ============================================================================

#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "config_v5.h"

// ============================================================================
// LOG MACROS - Foloseste acestea in cod
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

    // ------------------------------------------------------------------------
    // init() - Initializeaza logger
    // Apeleaza in setup() DUPA ce SD e initializat
    // ------------------------------------------------------------------------
    static void init(bool sdAvailable) {
        _sdAvailable = sdAvailable;
        _writeCount = 0;
        _errorCount = 0;
        _currentLevel = CURRENT_LOG_LEVEL;

        if (LOG_TO_SERIAL) {
            Serial.begin(LOG_SERIAL_BAUD);
            while (!Serial) { delay(10); }
        }

        // Header boot log
        logSeparator();
        log(LOG_LEVEL_INFO, "SYSTEM", "=== BOOT v%s (%s %s) ===",
            FW_VERSION, FW_BUILD_DATE, FW_BUILD_TIME);
        log(LOG_LEVEL_INFO, "SYSTEM", "RS485 DE Pin: GPIO %d (OFICIAL StamPLC.pdf)",
            RS485_DE_PIN);
        log(LOG_LEVEL_INFO, "SYSTEM", "RS485 Mode: UART_MODE_RS485_HALF_DUPLEX (hardware)");
        log(LOG_LEVEL_INFO, "SYSTEM", "Log Level: %s", levelName(_currentLevel));
        log(LOG_LEVEL_INFO, "SYSTEM", "SD Card: %s",
            sdAvailable ? "DISPONIBIL" : "LIPSA - log doar serial!");
        logSeparator();
    }

    // ------------------------------------------------------------------------
    // setLevel() - Schimba log level la runtime
    // ------------------------------------------------------------------------
    static void setLevel(uint8_t level) {
        _currentLevel = level;
        log(LOG_LEVEL_INFO, "SYSTEM", "Log level schimbat → %s", levelName(level));
    }

    static uint8_t getLevel() { return _currentLevel; }

    // ------------------------------------------------------------------------
    // log() - Main logging function
    // ------------------------------------------------------------------------
    static void log(uint8_t level, const char* category, const char* fmt, ...) {
        if (level > _currentLevel) return;

        char msg[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        // Construieste linia de log
        char line[640];
        snprintf(line, sizeof(line), "[%8lu] [%s] [%-6s] %s",
                 millis(), levelChar(level), category, msg);

        // Output Serial
        if (LOG_TO_SERIAL) {
            Serial.println(line);
            if (level == LOG_LEVEL_ERROR) {
                Serial.flush();
            }
        }

        // Output SD
        if (LOG_TO_SD && _sdAvailable) {
            // Alege fisierul potrivit
            const char* filename = (level == LOG_LEVEL_ERROR)
                                   ? SD_LOG_FILE_ERRORS
                                   : SD_LOG_FILE_DEBUG;
            writeToSD(filename, line);

            // La erori, scrie si in debug log
            if (level == LOG_LEVEL_ERROR) {
                writeToSD(SD_LOG_FILE_DEBUG, line);
            }
        }

        if (level == LOG_LEVEL_ERROR) {
            _errorCount++;
        }
    }

    // ------------------------------------------------------------------------
    // logHex() - Afiseaza date in format hex pentru RS485
    // ------------------------------------------------------------------------
    static void logHex(const char* direction, const uint8_t* data, size_t len) {
        if (LOG_LEVEL_DEBUG > _currentLevel) return;

        // Construieste hex string
        char hexStr[len * 3 + 8];
        hexStr[0] = '\0';
        for (size_t i = 0; i < len; i++) {
            char byte[4];
            snprintf(byte, sizeof(byte), "%02X ", data[i]);
            strncat(hexStr, byte, sizeof(hexStr) - strlen(hexStr) - 1);
        }

        log(LOG_LEVEL_DEBUG, "RS485",
            "%s [%zu bytes]: %s", direction, len, hexStr);
    }

    // ------------------------------------------------------------------------
    // logTiming() - Log timing measurement
    // ------------------------------------------------------------------------
    static void logTiming(const char* label, uint32_t ms) {
        if (!LOG_TIMING) return;
        if (LOG_LEVEL_DEBUG > _currentLevel) return;
        log(LOG_LEVEL_DEBUG, "TIMING", "%s: %lu ms", label, ms);
    }

    // ------------------------------------------------------------------------
    // logEchoDetected() - Special log pentru echo detection
    // ------------------------------------------------------------------------
    static void logEchoDetected(const uint8_t* tx, const uint8_t* rx, size_t len) {
        log(LOG_LEVEL_ERROR, "RS485",
            "!!! ECHO DETECTAT !!! TX = RX (DE pin greșit sau hardware issue)");

        char txHex[len * 3 + 4];
        char rxHex[len * 3 + 4];
        txHex[0] = rxHex[0] = '\0';

        for (size_t i = 0; i < len && i < 16; i++) {
            char b[4];
            snprintf(b, sizeof(b), "%02X ", tx[i]);
            strncat(txHex, b, sizeof(txHex) - strlen(txHex) - 1);
            snprintf(b, sizeof(b), "%02X ", rx[i]);
            strncat(rxHex, b, sizeof(rxHex) - strlen(rxHex) - 1);
        }

        log(LOG_LEVEL_ERROR, "RS485", "  TX: %s", txHex);
        log(LOG_LEVEL_ERROR, "RS485", "  RX: %s", rxHex);
        log(LOG_LEVEL_ERROR, "RS485", "  FIX: Verifica GPIO 0 si hardware RS485 mode!");
    }

    // ------------------------------------------------------------------------
    // logRS485Stats() - Log statistici RS485
    // ------------------------------------------------------------------------
    static void logRS485Stats(const RS485Stats& s) {
        logSeparator();
        log(LOG_LEVEL_INFO, "RS485", "=== STATISTICI RS485 ===");
        log(LOG_LEVEL_INFO, "RS485", "  DE Pin: GPIO %d", s.dePin);
        log(LOG_LEVEL_INFO, "RS485", "  HW Mode: %s",
            s.hwModeEnabled ? "ACTIV (hardware auto)" : "INACTIV (manual - GREȘIT!)");
        log(LOG_LEVEL_INFO, "RS485", "  TX Count: %lu", s.txCount);
        log(LOG_LEVEL_INFO, "RS485", "  RX Count: %lu", s.rxCount);
        log(LOG_LEVEL_INFO, "RS485", "  RX Valid: %lu", s.rxValidCount);
        log(LOG_LEVEL_INFO, "RS485", "  RX Echo: %lu (%s)",
            s.rxEchoCount, s.rxEchoCount > 0 ? "PROBLEMA!" : "OK");
        log(LOG_LEVEL_INFO, "RS485", "  RX CRC Errors: %lu", s.rxCrcErrors);
        log(LOG_LEVEL_INFO, "RS485", "  RX Timeouts: %lu", s.rxTimeouts);
        logSeparator();
    }

    // ------------------------------------------------------------------------
    // logSystemStats() - Log statistici sistem
    // ------------------------------------------------------------------------
    static void logSystemStats(const SystemStats& s) {
        logSeparator();
        log(LOG_LEVEL_INFO, "SYSTEM", "=== STATISTICI SISTEM ===");
        log(LOG_LEVEL_INFO, "SYSTEM", "  Uptime: %lu sec", s.uptime / 1000);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Total citiri: %lu", s.totalReads);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Succes: %lu (%.1f%%)",
            s.successfulReads, s.successRate);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Erori: %lu", s.failedReads);
        log(LOG_LEVEL_INFO, "SYSTEM", "  CRC Errors: %lu", s.crcErrors);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Timeouts: %lu", s.timeoutErrors);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Echo detectate: %lu", s.echoDetected);
        log(LOG_LEVEL_INFO, "SYSTEM", "  Avg read time: %.1f ms", s.avgReadTimeMs);
        logSeparator();
    }

    // ------------------------------------------------------------------------
    // logHeatPumpData() - Log date pompa caldura
    // ------------------------------------------------------------------------
    static void logHeatPumpData(const HeatPumpData& d) {
        if (!d.valid) {
            log(LOG_LEVEL_WARN, "DATA", "Date invalide - skip log");
            return;
        }

        log(LOG_LEVEL_INFO, "DATA", "=== DATE POMPA (%s) ===", d.dateTime);
        log(LOG_LEVEL_INFO, "DATA", "  Ambient:     %.1f°C", d.tempAmbient);
        log(LOG_LEVEL_INFO, "DATA", "  Water In:    %.1f°C", d.tempWaterInlet);
        log(LOG_LEVEL_INFO, "DATA", "  Water Out:   %.1f°C", d.tempWaterOutlet);
        log(LOG_LEVEL_INFO, "DATA", "  Water Target:%.1f°C", d.tempWaterTarget);
        log(LOG_LEVEL_INFO, "DATA", "  Evap In:     %.1f°C", d.tempEvapIn);
        log(LOG_LEVEL_INFO, "DATA", "  Evap Out:    %.1f°C", d.tempEvapOut);
        log(LOG_LEVEL_INFO, "DATA", "  Comp Disch:  %.1f°C", d.tempCompDisch);
        log(LOG_LEVEL_INFO, "DATA", "  Press High:  %.2f bar", d.pressHigh);
        log(LOG_LEVEL_INFO, "DATA", "  Press Low:   %.2f bar", d.pressLow);
        log(LOG_LEVEL_INFO, "DATA", "  System Status: 0x%04X", d.systemStatus);
        log(LOG_LEVEL_INFO, "DATA", "  Op Mode: 0x%04X", d.operatingMode);
        log(LOG_LEVEL_INFO, "DATA", "  Fault: 0x%04X", d.faultCode);
        log(LOG_LEVEL_INFO, "DATA", "  Quality: %d%%  Time: %lu ms",
            d.dataQuality, d.readDurationMs);
    }

    // ------------------------------------------------------------------------
    // logSeparator() - Linie separator
    // ------------------------------------------------------------------------
    static void logSeparator() {
        if (LOG_TO_SERIAL) {
            Serial.println(F("================================================================================"));
        }
        if (LOG_TO_SD && _sdAvailable) {
            writeToSD(SD_LOG_FILE_DEBUG,
                      "================================================================================");
        }
    }

    // ------------------------------------------------------------------------
    // writeCSV() - Scrie date in fisier CSV
    // ------------------------------------------------------------------------
    static bool writeCSV(const HeatPumpData& d) {
        if (!_sdAvailable) {
            log(LOG_LEVEL_WARN, "SD", "SD card indisponibil - nu pot scrie CSV");
            return false;
        }

        // Creeaza header daca fisierul e nou
        if (!SD.exists(SD_LOG_FILE_DATA)) {
            File f = SD.open(SD_LOG_FILE_DATA, FILE_WRITE);
            if (f) {
                f.println("Timestamp,DateTime,TempAmbient,TempWaterIn,TempWaterOut,"
                          "TempWaterTarget,TempEvapIn,TempEvapOut,TempCompDisch,"
                          "PressHigh,PressLow,SystemStatus,OpMode,FaultCode,"
                          "DataQuality,ReadTimeMs");
                f.close();
                log(LOG_LEVEL_INFO, "SD", "CSV header creat: %s", SD_LOG_FILE_DATA);
            }
        }

        File f = SD.open(SD_LOG_FILE_DATA, FILE_APPEND);
        if (!f) {
            log(LOG_LEVEL_ERROR, "SD", "Nu pot deschide CSV: %s", SD_LOG_FILE_DATA);
            return false;
        }

        char line[256];
        snprintf(line, sizeof(line),
                 "%lu,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,"
                 "0x%04X,0x%04X,0x%04X,%d,%lu",
                 d.timestamp, d.dateTime,
                 d.tempAmbient,
                 d.tempWaterInlet, d.tempWaterOutlet, d.tempWaterTarget,
                 d.tempEvapIn, d.tempEvapOut, d.tempCompDisch,
                 d.pressHigh, d.pressLow,
                 d.systemStatus, d.operatingMode, d.faultCode,
                 d.dataQuality, d.readDurationMs);

        f.println(line);
        f.close();

        log(LOG_LEVEL_DEBUG, "SD", "CSV scris: %s", line);
        return true;
    }

    // ------------------------------------------------------------------------
    // writeBootLog() - Scrie info boot in fisier separat
    // ------------------------------------------------------------------------
    static void writeBootLog(const char* info) {
        if (!_sdAvailable) return;

        File f = SD.open(SD_LOG_FILE_BOOT, FILE_APPEND);
        if (f) {
            char line[256];
            snprintf(line, sizeof(line), "[%8lu] %s", millis(), info);
            f.println(line);
            f.close();
        }
    }

    // ------------------------------------------------------------------------
    // getErrorCount() - Returneaza numarul de erori logare
    // ------------------------------------------------------------------------
    static uint32_t getErrorCount() { return _errorCount; }

    // ------------------------------------------------------------------------
    // sdAvailable() - Verifica SD card
    // ------------------------------------------------------------------------
    static bool sdAvailable() { return _sdAvailable; }

    // ------------------------------------------------------------------------
    // setSDAvailable() - Seteaza stare SD
    // ------------------------------------------------------------------------
    static void setSDAvailable(bool avail) { _sdAvailable = avail; }

private:

    static uint8_t  _currentLevel;
    static bool     _sdAvailable;
    static uint32_t _writeCount;
    static uint32_t _errorCount;

    // ------------------------------------------------------------------------
    // writeToSD() - Scrie o linie in fisier SD
    // ------------------------------------------------------------------------
    static void writeToSD(const char* filename, const char* line) {
        File f = SD.open(filename, FILE_APPEND);
        if (!f) {
            // SD write failed - nu loguiem recursiv, doar la serial
            if (LOG_TO_SERIAL) {
                Serial.print(F("[SD ERROR] Nu pot scrie in: "));
                Serial.println(filename);
            }
            return;
        }

        f.println(line);

        // Flush periodic sau la erori
        _writeCount++;
        if (_writeCount % SD_LOG_FLUSH_EVERY == 0) {
            f.flush();
        }
        f.close();
    }

    // ------------------------------------------------------------------------
    // levelChar() - Caracter prefix nivel
    // ------------------------------------------------------------------------
    static const char* levelChar(uint8_t level) {
        switch (level) {
            case LOG_LEVEL_ERROR:   return "E";
            case LOG_LEVEL_WARN:    return "W";
            case LOG_LEVEL_INFO:    return "I";
            case LOG_LEVEL_DEBUG:   return "D";
            case LOG_LEVEL_VERBOSE: return "V";
            default:                return "?";
        }
    }

    // ------------------------------------------------------------------------
    // levelName() - Nume nivel
    // ------------------------------------------------------------------------
    static const char* levelName(uint8_t level) {
        switch (level) {
            case LOG_LEVEL_NONE:    return "NONE";
            case LOG_LEVEL_ERROR:   return "ERROR";
            case LOG_LEVEL_WARN:    return "WARN";
            case LOG_LEVEL_INFO:    return "INFO";
            case LOG_LEVEL_DEBUG:   return "DEBUG";
            case LOG_LEVEL_VERBOSE: return "VERBOSE";
            default:                return "UNKNOWN";
        }
    }
};

// Static member definitions
uint8_t  DebugLogger::_currentLevel = LOG_LEVEL_INFO;
bool     DebugLogger::_sdAvailable  = false;
uint32_t DebugLogger::_writeCount   = 0;
uint32_t DebugLogger::_errorCount   = 0;
