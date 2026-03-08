// ============================================================================
// pin_test_v5.h - TEST COMPLET GPIO PENTRU RS485 DE PIN
// Versiune 5.0 - Include GPIO 0 (PRIMUL in lista!)
// ============================================================================
// DIFERENTE FATA DE VERSIUNILE ANTERIOARE:
//
//   pin_scanner.h (v3): {46, 1, 4, 5, 2}     ← GPIO 0 LIPSESTE!
//   quick_diagnostic.cpp (v4): {46,1,4,5,2,21} ← GPIO 0 LIPSESTE!
//
//   pin_test_v5.h (v5): {0, 46, 2, 1, 4, 5}  ← GPIO 0 PRIMUL! ✓
//
// METODOLOGIE TEST:
//   1. Pentru fiecare pin candidat:
//      a. Configureaza UART cu hardware RS485 mode (setMode)
//      b. Trimite Modbus Block 1 request
//      c. Verifica raspuns: valid / echo / timeout
//      d. Repeta PIN_SCAN_TESTS_PER_PIN ori
//   2. Calculeaza success rate
//   3. Returneaza cel mai bun pin (GPIO 0 ar trebui sa fie 100%)
//   4. Salveaza in NVS si in SD log
// ============================================================================

#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <Preferences.h>
#include "config_v5.h"
#include "debug_logger.h"

// ============================================================================
// Structura rezultat test
// ============================================================================
struct PinTestResult {
    uint8_t  pin;
    int      successCount;
    int      echoCount;
    int      timeoutCount;
    int      crcErrorCount;
    int      totalTests;
    float    successRate;
    bool     hwModeWorking;
    uint32_t avgResponseMs;
    char     verdict[32];  // "BEST", "PASS", "FAIL", "ECHO", "TIMEOUT"
};

// ============================================================================
// PinTesterV5 CLASS
// ============================================================================
class PinTesterV5 {
public:

    static const int MAX_CANDIDATES = 8;
    PinTestResult results[MAX_CANDIDATES];
    int           resultCount = 0;
    int           bestPinIndex = -1;

    // Pinii candidati in ordinea prioritatii
    // GPIO 0 = PRIMUL (pin oficial StamPLC.pdf!)
    uint8_t candidates[PIN_SCAN_COUNT] = PIN_SCAN_CANDIDATES;

    // ------------------------------------------------------------------------
    // scanAll() - Testeaza TOTI pinii candidati
    // Returneaza GPIO-ul recomandat (0 daca niciunul nu functioneaza)
    // ------------------------------------------------------------------------
    uint8_t scanAll(HardwareSerial& serial) {

        LOG_SEPARATOR();
        LOG_I("PINSCAN", "=== PIN SCAN v5 - Test DE Pin RS485 ===");
        LOG_I("PINSCAN", "Candidati: {0, 46, 2, 1, 4, 5} ← GPIO 0 PRIMUL!");
        LOG_I("PINSCAN", "Metoda: hardware UART_MODE_RS485_HALF_DUPLEX");
        LOG_I("PINSCAN", "Teste per pin: %d", PIN_SCAN_TESTS_PER_PIN);
        LOG_I("PINSCAN", "Success threshold: %d%%", PIN_SCAN_SUCCESS_RATE);
        LOG_SEPARATOR();

        resultCount = 0;
        bestPinIndex = -1;
        float bestRate = -1.0f;

        for (int i = 0; i < PIN_SCAN_COUNT && resultCount < MAX_CANDIDATES; i++) {
            uint8_t pin = candidates[i];

            LOG_I("PINSCAN", "--- Test GPIO %d ---", pin);

            PinTestResult r = testPin(serial, pin);
            results[resultCount] = r;

            // Actualizeaza best
            if (r.successRate > bestRate) {
                bestRate = r.successRate;
                bestPinIndex = resultCount;
            }

            resultCount++;

            // Daca gasim GPIO 0 cu 100% success, nu mai continuam
            if (pin == 0 && r.successRate >= 100.0f) {
                LOG_I("PINSCAN", "✓ GPIO 0 = 100%% success! Scan complet.");
                break;
            }

            delay(200);  // Pauza intre teste
        }

        printResults();

        if (bestPinIndex >= 0 && results[bestPinIndex].successRate >= PIN_SCAN_SUCCESS_RATE) {
            uint8_t bestPin = results[bestPinIndex].pin;
            LOG_I("PINSCAN", "✓ BEST PIN: GPIO %d (%.0f%% success)",
                  bestPin, results[bestPinIndex].successRate);

            if (bestPin != 0) {
                LOG_W("PINSCAN", "⚠ Cel mai bun pin NU e GPIO 0 (oficial)!");
                LOG_W("PINSCAN", "⚠ Verifica hardware-ul StamPLC!");
            }

            saveToNVS(bestPin);
            return bestPin;
        }

        LOG_E("PINSCAN", "NICI UN PIN NU FUNCTIONEAZA!");
        LOG_E("PINSCAN", "  → Verifica conexiunile hardware");
        LOG_E("PINSCAN", "  → Verifica alimentarea dispozitivului RS485");
        LOG_E("PINSCAN", "  → Verifica baudrate (curent: %d)", MODBUS_BAUDRATE);
        return 0xFF;  // Eroare - niciun pin valid
    }

    // ------------------------------------------------------------------------
    // testPin() - Testeaza un singur pin
    // ------------------------------------------------------------------------
    PinTestResult testPin(HardwareSerial& serial, uint8_t pin) {
        PinTestResult r;
        r.pin          = pin;
        r.successCount = 0;
        r.echoCount    = 0;
        r.timeoutCount = 0;
        r.crcErrorCount = 0;
        r.totalTests   = PIN_SCAN_TESTS_PER_PIN;
        r.hwModeWorking = false;
        r.avgResponseMs = 0;
        uint32_t totalMs = 0;

        // Configureaza UART cu noul pin (hardware sau manual fallback)
        if (!_initUARTWithPin(serial, pin)) {
            LOG_E("PINSCAN", "GPIO %d: Nu pot initializa UART!", pin);
            r.successRate = 0;
            snprintf(r.verdict, sizeof(r.verdict), "INIT_FAIL");
            return r;
        }
        r.hwModeWorking = !_useManualDE;

        // Modbus Block 1 request (hardcodat din Elfin protocol)
        uint8_t request[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x29, 0x84, 0x14};

        LOG_D("PINSCAN", "GPIO %d: Rulare %d teste...", pin, PIN_SCAN_TESTS_PER_PIN);

        for (int t = 0; t < PIN_SCAN_TESTS_PER_PIN; t++) {
            uint32_t startMs = millis();
            TestOutcome outcome = _runOneTest(serial, request, 8);
            uint32_t duration = millis() - startMs;

            totalMs += duration;

            switch (outcome) {
                case OUTCOME_SUCCESS:
                    r.successCount++;
                    LOG_D("PINSCAN", "  Test %d: ✓ SUCCESS (%lu ms)", t+1, duration);
                    break;
                case OUTCOME_ECHO:
                    r.echoCount++;
                    LOG_D("PINSCAN", "  Test %d: ↩ ECHO (DE pin nu comuta TX/RX)");
                    break;
                case OUTCOME_TIMEOUT:
                    r.timeoutCount++;
                    LOG_D("PINSCAN", "  Test %d: ⏱ TIMEOUT (%lu ms)", t+1, duration);
                    break;
                case OUTCOME_CRC_ERROR:
                    r.crcErrorCount++;
                    LOG_D("PINSCAN", "  Test %d: ✗ CRC ERROR (%lu ms)", t+1, duration);
                    break;
            }

            delay(100);
        }

        // Calculeaza rata
        r.successRate  = (float)r.successCount / PIN_SCAN_TESTS_PER_PIN * 100.0f;
        r.avgResponseMs = totalMs / PIN_SCAN_TESTS_PER_PIN;

        // Verdict
        if (r.successRate >= PIN_SCAN_SUCCESS_RATE) {
            snprintf(r.verdict, sizeof(r.verdict), "PASS");
        } else if (r.echoCount > r.successCount) {
            snprintf(r.verdict, sizeof(r.verdict), "ECHO (DE greșit!)");
        } else if (r.timeoutCount >= PIN_SCAN_TESTS_PER_PIN) {
            snprintf(r.verdict, sizeof(r.verdict), "TIMEOUT");
        } else {
            snprintf(r.verdict, sizeof(r.verdict), "FAIL");
        }

        LOG_I("PINSCAN", "GPIO %d: %s [%d/%d, %.0f%%, avg %lu ms]",
              pin, r.verdict, r.successCount, PIN_SCAN_TESTS_PER_PIN,
              r.successRate, r.avgResponseMs);

        return r;
    }

    // ------------------------------------------------------------------------
    // printResults() - Afiseaza tabel complet
    // ------------------------------------------------------------------------
    void printResults() {
        LOG_SEPARATOR();
        LOG_I("PINSCAN", "=== REZULTATE PIN SCAN v5 ===");
        LOG_I("PINSCAN", "%-6s  %-5s  %-5s  %-5s  %-5s  %-8s  %-6s  %s",
              "GPIO", "SUCC", "ECHO", "TOUT", "CRC", "SuccRate", "AvgMs", "Verdict");
        LOG_I("PINSCAN", "------  -----  -----  -----  -----  --------  ------  -------");

        for (int i = 0; i < resultCount; i++) {
            PinTestResult& r = results[i];
            bool isBest = (i == bestPinIndex &&
                           r.successRate >= PIN_SCAN_SUCCESS_RATE);

            LOG_I("PINSCAN", "GPIO %-2d  %-5d  %-5d  %-5d  %-5d  %6.0f%%  %5lu ms  %s%s",
                  r.pin, r.successCount, r.echoCount,
                  r.timeoutCount, r.crcErrorCount,
                  r.successRate, r.avgResponseMs,
                  r.verdict,
                  isBest ? " ← BEST" : "");
        }

        if (bestPinIndex >= 0) {
            LOG_SEPARATOR();
            LOG_I("PINSCAN", "✓ RECOMANDAT: GPIO %d (%.0f%%)",
                  results[bestPinIndex].pin,
                  results[bestPinIndex].successRate);

            if (results[bestPinIndex].pin == 0) {
                LOG_I("PINSCAN", "  → GPIO 0 = pin oficial StamPLC.pdf ✓ PERFECT!");
            } else {
                LOG_W("PINSCAN", "  → Atentie: GPIO %d != GPIO 0 (oficial)",
                      results[bestPinIndex].pin);
            }
        }
        LOG_SEPARATOR();
    }

    // ------------------------------------------------------------------------
    // saveToNVS() - Salveaza pinul in flash
    // ------------------------------------------------------------------------
    void saveToNVS(uint8_t pin) {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putUChar(NVS_KEY_DE_PIN, pin);
        prefs.putUInt("magic", NVS_MAGIC);
        prefs.end();
        LOG_I("PINSCAN", "Pinul GPIO %d salvat in NVS", pin);
    }

    // ------------------------------------------------------------------------
    // loadFromNVS() - Citeste pinul din flash
    // Returneaza 0xFF daca nu e salvat valid
    // ------------------------------------------------------------------------
    static uint8_t loadFromNVS() {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        uint32_t magic = prefs.getUInt("magic", 0);
        uint8_t pin = prefs.getUChar(NVS_KEY_DE_PIN, 0xFF);
        prefs.end();

        if (magic != NVS_MAGIC) {
            LOG_I("PINSCAN", "NVS: magic invalid (0x%08X) → scan necesar", magic);
            return 0xFF;
        }

        LOG_I("PINSCAN", "NVS: pin GPIO %d gasit (magic OK)", pin);
        return pin;
    }

private:

    uint8_t _currentDePin = 0xFF;  // Pin DE curent (pentru manual control)
    bool    _useManualDE  = false;  // true = manual digitalWrite, false = hardware UART

    enum TestOutcome {
        OUTCOME_SUCCESS,
        OUTCOME_ECHO,
        OUTCOME_TIMEOUT,
        OUTCOME_CRC_ERROR
    };

    // ------------------------------------------------------------------------
    // _initUARTWithPin() - Reinitializeaza UART cu noul pin DE
    // Daca hardware RS485 mode esueaza, foloseste manual DE control.
    // ------------------------------------------------------------------------
    bool _initUARTWithPin(HardwareSerial& serial, uint8_t dePin) {
        // End previous session
        serial.end();
        delay(50);

        // Reinitializeaza cu noul pin
        serial.begin(MODBUS_BAUDRATE, MODBUS_CONFIG,
                     RS485_RX_PIN, RS485_TX_PIN);

        // Set pins: RX, TX, CTS=-1, RTS=dePin
        serial.setPins(RS485_RX_PIN, RS485_TX_PIN, -1, dePin);

        // Incearca hardware RS485 mode; daca esueaza, fallback la manual DE
        esp_err_t err = serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
        if (err != ESP_OK) {
            LOG_W("PINSCAN", "setMode GPIO %d err=%d → manual DE control", dePin, err);
            // Configureaza pin DE ca output pentru control manual
            pinMode(dePin, OUTPUT);
            digitalWrite(dePin, LOW);
            _currentDePin = dePin;
            _useManualDE  = true;
        } else {
            serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
            _currentDePin = dePin;
            _useManualDE  = false;
        }

        delay(20);  // Stabilizare
        return true;  // Intotdeauna continuam (hardware sau manual)
    }

    // ------------------------------------------------------------------------
    // _runOneTest() - Un singur test Modbus
    // ------------------------------------------------------------------------
    TestOutcome _runOneTest(HardwareSerial& serial, const uint8_t* request, size_t reqLen) {
        // Goleste buffer RX
        while (serial.available()) serial.read();

        // Trimite - hardware sau manual DE control
        if (_useManualDE && _currentDePin != 0xFF) {
            digitalWrite(_currentDePin, HIGH);
            delayMicroseconds(100);
        }
        serial.write(request, reqLen);
        serial.flush();  // Asteapta TX complet (CRITIC pentru manual mode!)
        if (_useManualDE && _currentDePin != 0xFF) {
            digitalWrite(_currentDePin, LOW);
        }

        // Asteapta raspuns
        uint32_t waitStart = millis();
        while (!serial.available()) {
            if (millis() - waitStart > PIN_SCAN_TIMEOUT_MS) {
                return OUTCOME_TIMEOUT;
            }
            delay(1);
        }

        delay(50);  // Asteapta toate bytes-urile

        // Citeste raspunsul
        uint8_t response[128];
        size_t rxLen = 0;
        while (serial.available() && rxLen < sizeof(response)) {
            response[rxLen++] = serial.read();
        }

        if (rxLen < 5) return OUTCOME_TIMEOUT;

        // Echo detection
        size_t cmpLen = min(reqLen, rxLen);
        if (memcmp(request, response, cmpLen) == 0) {
            return OUTCOME_ECHO;
        }

        // CRC check
        if (!_checkCRC(response, rxLen)) {
            return OUTCOME_CRC_ERROR;
        }

        // Verifica slave ID si function code
        if (response[0] != 0x01 || response[1] != 0x03) {
            return OUTCOME_CRC_ERROR;  // Invalid response
        }

        return OUTCOME_SUCCESS;
    }

    // ------------------------------------------------------------------------
    // _checkCRC() - Verifica CRC Modbus
    // ------------------------------------------------------------------------
    bool _checkCRC(const uint8_t* data, size_t len) {
        if (len < 2) return false;

        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len - 2; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
            }
        }

        uint16_t recvCRC = data[len-2] | (data[len-1] << 8);
        return (crc == recvCRC);
    }
};
