// ============================================================================
// pin_test_v7.h - SCAN PIN RS485 v7 - TX=0/RX=39/DE=46 CONFIRMATE
// ============================================================================
// SCHIMBARI FATA DE v5:
//
//   TX=0, RX=39, DE=46 ESTE PRIMA COMBINATIE TESTATA (nu TX=42/RX=43)
//   Confirmat din log v5: CRC errors (semnal real!) vs TIMEOUT pe TX=42/RX=43.
//
//   CRC_ERROR scoring: effectiveScore = crcErrors/total * 30%
//   Garanteaza ca CRC_ERROR (semnal real) > TIMEOUT (fara semnal)
//
//   Inter-byte gap 20ms inlocuieste delay(50ms) fix.
//   La 9600 baud: 87 bytes = 91ms → delay(50ms) trunchia raspunsul!
//
//   Log hex dump la CRC error → vedem exact ce trimite pompa.
//
//   Scan separat de baud rate e in modbus_v7.h (scanBaudRate()).
//   Acest fisier scaneaza doar TX/RX/DE combinations.
// ============================================================================

#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "config_v7.h"
#include "debug_logger.h"

// ============================================================================
// Test outcome enum
// ============================================================================
enum TestOutcome {
    OUTCOME_SUCCESS,
    OUTCOME_ECHO,
    OUTCOME_TIMEOUT,
    OUTCOME_CRC_ERROR
};

// ============================================================================
// PinTestResult - Rezultat test un singur pin/pereche
// ============================================================================
struct PinTestResult {
    uint8_t  pin;            // DE pin testat
    int      successCount;
    int      echoCount;
    int      timeoutCount;
    int      crcErrorCount;
    int      totalTests;
    float    successRate;    // % SUCCESS pur
    float    effectiveScore; // SUCCESS=100%, CRC_ERROR=30%, TIMEOUT=0%
    bool     hwModeWorking;
    uint32_t avgResponseMs;
    char     verdict[32];
};

// ============================================================================
// UARTPair - Pereche TX/RX candidata
// ============================================================================
struct UARTPair {
    uint8_t tx;
    uint8_t rx;
    uint8_t de;
};

// ============================================================================
// PinTesterV7
// ============================================================================
class PinTesterV7 {
public:
    static const int MAX_RESULTS = 6;

    PinTestResult results[MAX_RESULTS];
    int           resultCount  = 0;
    int           bestIndex    = -1;
    uint8_t       bestTxPin    = RS485_TX_PIN;
    uint8_t       bestRxPin    = RS485_RX_PIN;
    uint8_t       bestDePin    = RS485_DE_PIN;

    // ------------------------------------------------------------------------
    // scanAll() - Testeaza combinatii TX/RX/DE
    // TX=0/RX=39/DE=46 ESTE PRIMA (confirmata din log!).
    // Returneaza DE pinul cel mai bun, sau 0xFF daca nimic nu merge.
    // ------------------------------------------------------------------------
    uint8_t scanAll(HardwareSerial& serial) {
        static const UARTPair pairs[] = RS485_UART_CANDIDATES;

        LOG_SEPARATOR();
        LOG_I("PINSCAN", "=== PIN SCAN v7 - TX/RX/DE ===");
        LOG_I("PINSCAN", "  [0] TX=0  RX=39 DE=46 ← CONFIRMAT din log! (CRC errors=semnal real)");
        LOG_I("PINSCAN", "  [1] TX=42 RX=43 DE=0  ← oficial StamPLC.pdf (TIMEOUT in v5 log)");
        LOG_I("PINSCAN", "  [2] TX=0  RX=1  DE=46 ← alternativa");
        LOG_I("PINSCAN", "Threshold: %d%% SUCCESS sau CRC_ERROR > 0", PIN_SCAN_SUCCESS_RATE);
        LOG_SEPARATOR();

        resultCount = 0;
        bestIndex   = -1;
        float bestScore = -1.0f;

        for (int p = 0; p < RS485_UART_PAIR_COUNT && resultCount < MAX_RESULTS; p++) {
            uint8_t tx = pairs[p].tx;
            uint8_t rx = pairs[p].rx;
            uint8_t de = pairs[p].de;

            LOG_I("PINSCAN", "--- Pereche %d: TX=%d RX=%d DE=%d ---", p, tx, rx, de);

            if (rx == RS485_RX_PIN) {
                LOG_I("PINSCAN", "  → RX=GPIO%d = RS485 RX confirmat (nu buton!)", rx);
            }

            // Reconfigureaza UART
            serial.end();
            delay(50);
            serial.setPins(rx, tx, -1, de);
            serial.begin(MODBUS_BAUDRATE, MODBUS_CONFIG);
            delay(20);

            PinTestResult r = _testPair(serial, tx, rx, de);
            results[resultCount] = r;

            if (r.effectiveScore > bestScore) {
                bestScore  = r.effectiveScore;
                bestIndex  = resultCount;
                bestTxPin  = tx;
                bestRxPin  = rx;
                bestDePin  = de;
            }
            resultCount++;

            // Oprire timpurie la succes 100%
            if (r.successRate >= 100.0f) {
                LOG_I("PINSCAN", "✓ 100%% succes! Scan oprit.");
                break;
            }

            delay(200);
        }

        _printResults();

        if (bestIndex >= 0) {
            PinTestResult& best = results[bestIndex];

            if (best.successRate >= PIN_SCAN_SUCCESS_RATE) {
                LOG_I("PINSCAN", "✓ OPTIM: TX=%d RX=%d DE=%d (%.0f%% succes)",
                      bestTxPin, bestRxPin, bestDePin, best.successRate);
                _saveToNVS(bestDePin, bestTxPin, bestRxPin, MODBUS_BAUDRATE);
                return bestDePin;
            }

            if (best.crcErrorCount > 0) {
                LOG_W("PINSCAN", "~ BEST cu CRC: TX=%d RX=%d DE=%d → semnal real!",
                      bestTxPin, bestRxPin, bestDePin);
                LOG_W("PINSCAN", "  Baud rate posibil gresit → va rula scanBaudRate()");
                _saveToNVS(bestDePin, bestTxPin, bestRxPin, 0);  // baud=0 = necunoscut
                return bestDePin;
            }
        }

        LOG_E("PINSCAN", "NICIO combinatie valida! Verifica conexiunile A/B.");
        return 0xFF;
    }

    // ------------------------------------------------------------------------
    // loadFromNVS() - Incarca TX/RX/DE/baud din flash
    // Returneaza DE, seteaza *txOut/*rxOut/*baudOut daca nu-s null
    // Returneaza 0xFF daca nu exista date valide
    // ------------------------------------------------------------------------
    static uint8_t loadFromNVS(uint8_t* txOut   = nullptr,
                                uint8_t* rxOut   = nullptr,
                                uint32_t* baudOut = nullptr) {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        uint32_t magic   = prefs.getUInt("magic", 0);
        uint8_t  dePin   = prefs.getUChar("de_pin",  0xFF);
        uint8_t  txPin   = prefs.getUChar("tx_pin",  RS485_TX_PIN);
        uint8_t  rxPin   = prefs.getUChar("rx_pin",  RS485_RX_PIN);
        uint32_t baud    = prefs.getUInt("baudrate", 0);
        prefs.end();

        if (magic != NVS_MAGIC) {
            LOG_I("NVS", "Magic invalid (0x%08X) → scan necesar", magic);
            return 0xFF;
        }

        if (txOut)   *txOut   = txPin;
        if (rxOut)   *rxOut   = rxPin;
        if (baudOut) *baudOut = baud;

        LOG_I("NVS", "Loaded: TX=%d RX=%d DE=%d Baud=%lu", txPin, rxPin, dePin, baud);
        return dePin;
    }

    // ------------------------------------------------------------------------
    // saveToNVS() - Salveaza configuratia completa
    // ------------------------------------------------------------------------
    static void saveToNVS(uint8_t dePin, uint8_t txPin, uint8_t rxPin, uint32_t baud) {
        _saveToNVS(dePin, txPin, rxPin, baud);
    }

private:

    bool _useManualDE  = false;
    uint8_t _currentDE = 0xFF;

    // ------------------------------------------------------------------------
    // _testPair() - Testeaza o pereche TX/RX/DE
    // UART trebuie reconfigurat INAINTE de apel.
    // ------------------------------------------------------------------------
    PinTestResult _testPair(HardwareSerial& serial,
                            uint8_t txPin, uint8_t rxPin, uint8_t dePin) {
        PinTestResult r;
        r.pin           = dePin;
        r.successCount  = 0;
        r.echoCount     = 0;
        r.timeoutCount  = 0;
        r.crcErrorCount = 0;
        r.totalTests    = PIN_SCAN_TESTS_PER_PIN;
        r.hwModeWorking = false;
        r.avgResponseMs = 0;
        r.effectiveScore = 0.0f;

        bool hwOK = serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
        if (!hwOK) {
            serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
            pinMode(dePin, OUTPUT);
            digitalWrite(dePin, LOW);
            _useManualDE = true;
        } else {
            serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
            _useManualDE = false;
        }
        r.hwModeWorking = !_useManualDE;
        _currentDE = dePin;

        // Block1 request: Slave=1, FC=03, Addr=0, Count=0x29, CRC=0x1484
        static const uint8_t REQ[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x29, 0x84, 0x14};

        LOG_D("PINSCAN", "TX=%d RX=%d DE=%d: %d teste @ %d bps...",
              txPin, rxPin, dePin, PIN_SCAN_TESTS_PER_PIN, MODBUS_BAUDRATE);

        uint32_t totalMs = 0;
        for (int t = 0; t < PIN_SCAN_TESTS_PER_PIN; t++) {
            uint32_t t0 = millis();
            TestOutcome outcome = _runOneTest(serial, REQ, 8, txPin, rxPin, dePin);
            uint32_t dur = millis() - t0;
            totalMs += dur;

            switch (outcome) {
                case OUTCOME_SUCCESS:
                    r.successCount++;
                    LOG_D("PINSCAN", "  [%d] ✓ SUCCESS (%lu ms)", t+1, dur);
                    break;
                case OUTCOME_ECHO:
                    r.echoCount++;
                    LOG_D("PINSCAN", "  [%d] ↩ ECHO", t+1);
                    break;
                case OUTCOME_TIMEOUT:
                    r.timeoutCount++;
                    LOG_D("PINSCAN", "  [%d] ⏱ TIMEOUT (%lu ms)", t+1, dur);
                    break;
                case OUTCOME_CRC_ERROR:
                    r.crcErrorCount++;
                    LOG_D("PINSCAN", "  [%d] ~ CRC_ERROR (%lu ms) = semnal detectat!", t+1, dur);
                    break;
            }
            delay(100);
        }

        r.successRate    = (float)r.successCount / PIN_SCAN_TESTS_PER_PIN * 100.0f;
        r.avgResponseMs  = totalMs / PIN_SCAN_TESTS_PER_PIN;
        r.effectiveScore = r.successRate;
        if (r.effectiveScore == 0.0f && r.crcErrorCount > 0) {
            r.effectiveScore = (float)r.crcErrorCount / PIN_SCAN_TESTS_PER_PIN * 30.0f;
        }

        if      (r.successRate >= PIN_SCAN_SUCCESS_RATE) snprintf(r.verdict, sizeof(r.verdict), "PASS");
        else if (r.crcErrorCount > 0 && r.timeoutCount == 0) snprintf(r.verdict, sizeof(r.verdict), "CRC-SEMNAL!");
        else if (r.echoCount > 0)     snprintf(r.verdict, sizeof(r.verdict), "ECHO");
        else if (r.timeoutCount >= PIN_SCAN_TESTS_PER_PIN) snprintf(r.verdict, sizeof(r.verdict), "TIMEOUT");
        else                          snprintf(r.verdict, sizeof(r.verdict), "PARTIAL");

        LOG_I("PINSCAN", "TX=%d RX=%d DE=%d → %s [ok=%d crc=%d tout=%d score=%.0f%% avg=%lums]",
              txPin, rxPin, dePin, r.verdict,
              r.successCount, r.crcErrorCount, r.timeoutCount,
              r.effectiveScore, r.avgResponseMs);

        return r;
    }

    // ------------------------------------------------------------------------
    // _runOneTest() - Un singur test Modbus cu inter-byte gap
    // ------------------------------------------------------------------------
    TestOutcome _runOneTest(HardwareSerial& serial,
                            const uint8_t* request, size_t reqLen,
                            uint8_t txPin, uint8_t rxPin, uint8_t dePin) {
        while (serial.available()) serial.read();

        if (_useManualDE) { digitalWrite(dePin, HIGH); delayMicroseconds(150); }
        serial.write(request, reqLen);
        serial.flush();
        if (_useManualDE) { digitalWrite(dePin, LOW); }

        // Asteapta primul byte
        uint32_t t0 = millis();
        while (!serial.available()) {
            if (millis() - t0 > PIN_SCAN_TIMEOUT_MS) return OUTCOME_TIMEOUT;
            delay(1);
        }

        // Inter-byte gap: citeste pana la 20ms de liniste
        uint8_t  resp[128];
        size_t   rxLen    = 0;
        uint32_t lastTime = millis();
        while (millis() - lastTime < 20 && rxLen < sizeof(resp)) {
            if (serial.available()) {
                resp[rxLen++] = serial.read();
                lastTime = millis();
            }
        }

        if (rxLen < 5) return OUTCOME_TIMEOUT;

        // Echo check
        if (rxLen >= reqLen && memcmp(request, resp, reqLen) == 0) return OUTCOME_ECHO;

        // Log raw bytes (ajuta la diagnosticare baud)
        char hexBuf[100]; hexBuf[0] = '\0';
        for (size_t i = 0; i < rxLen && i < 20; i++) {
            char tmp[4]; snprintf(tmp, sizeof(tmp), "%02X ", resp[i]);
            strncat(hexBuf, tmp, sizeof(hexBuf) - strlen(hexBuf) - 1);
        }
        if (rxLen > 20) strncat(hexBuf, "...", sizeof(hexBuf) - strlen(hexBuf) - 1);
        LOG_D("PINSCAN", "  RX [%zu bytes]: %s", rxLen, hexBuf);

        // CRC check
        if (!_checkCRC(resp, rxLen)) return OUTCOME_CRC_ERROR;

        if (resp[0] != 0x01 || resp[1] != 0x03) return OUTCOME_CRC_ERROR;

        return OUTCOME_SUCCESS;
    }

    bool _checkCRC(const uint8_t* data, size_t len) {
        if (len < 2) return false;
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len - 2; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
        uint16_t recv = data[len-2] | (data[len-1] << 8);
        return crc == recv;
    }

    void _printResults() {
        LOG_SEPARATOR();
        LOG_I("PINSCAN", "=== REZULTATE SCAN v7 ===");
        for (int i = 0; i < resultCount; i++) {
            PinTestResult& r = results[i];
            LOG_I("PINSCAN", "DE=%-2d  ok=%-2d crc=%-2d tout=%-2d  score=%5.0f%%  %4lu ms  %s%s",
                  r.pin, r.successCount, r.crcErrorCount, r.timeoutCount,
                  r.effectiveScore, r.avgResponseMs, r.verdict,
                  i == bestIndex ? " ← BEST" : "");
        }
        if (bestIndex >= 0) {
            LOG_SEPARATOR();
            PinTestResult& b = results[bestIndex];
            if (b.successRate >= PIN_SCAN_SUCCESS_RATE)
                LOG_I("PINSCAN", "✓ BEST: TX=%d RX=%d DE=%d → %.0f%% succes",
                      bestTxPin, bestRxPin, bestDePin, b.successRate);
            else if (b.crcErrorCount > 0)
                LOG_W("PINSCAN", "~ BEST: TX=%d RX=%d DE=%d → CRC errors (semnal real!)",
                      bestTxPin, bestRxPin, bestDePin);
            else
                LOG_E("PINSCAN", "✗ Niciun candidat cu semnal detectat");
        }
        LOG_SEPARATOR();
    }

    static void _saveToNVS(uint8_t dePin, uint8_t txPin, uint8_t rxPin, uint32_t baud) {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putUChar("de_pin",  dePin);
        prefs.putUChar("tx_pin",  txPin);
        prefs.putUChar("rx_pin",  rxPin);
        prefs.putUInt("baudrate", baud);
        prefs.putUInt("magic",    NVS_MAGIC);
        prefs.end();
        LOG_I("NVS", "Salvat: TX=%d RX=%d DE=%d Baud=%lu", txPin, rxPin, dePin, baud);
    }
};
