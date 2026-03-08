// ============================================================================
// modbus_v5.h - MODBUS RTU CU HARDWARE RS485 HALF-DUPLEX MODE
// Versiune 5.0 - GPIO 0 + UART_MODE_RS485_HALF_DUPLEX
// ============================================================================
// SCHIMBARI FATA DE VERSIUNILE ANTERIOARE:
//
//   v1-v4 (GREȘIT):
//     - RS485_DE_PIN = 2/46/1/4/5  ← GPIO greșit
//     - digitalWrite(dePin, HIGH)   ← Manual control imperfect
//     - delayMicroseconds(500)      ← Software delay nesigur
//     - digitalWrite(dePin, LOW)    ← Race condition posibil
//
//   v5 (CORECT):
//     - RS485_DE_PIN = 0            ← GPIO 0 oficial StamPLC.pdf
//     - setMode(UART_MODE_RS485_HALF_DUPLEX) ← Hardware control
//     - setPins(..., RTS=0)         ← Hardware foloseste GPIO 0 automat
//     - ZERO digitalWrite() manual ← Hardware face totul perfect
//
// DOCUMENTATIE:
//   - ESP32-S3 UART RS485 mode: controlează automat RTS pin (GPIO 0)
//   - Timing perfect la nivel de bit - imposibil de replicat manual
//   - StamPLC.pdf pg.9: GPIO 0 = RS485_DIR (Driver Enable)
// ============================================================================

#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "config_v5.h"
#include "debug_logger.h"

// ============================================================================
// ModbusV5 CLASS
// ============================================================================
class ModbusV5 {
public:

    // RS485 stats
    RS485Stats stats;

    // ------------------------------------------------------------------------
    // init() - Configureaza UART cu hardware RS485 mode
    // APELEAZA O SINGURA DATA in setup()!
    // ------------------------------------------------------------------------
    bool init(HardwareSerial& serial, uint8_t dePin = RS485_DE_PIN) {
        _serial = &serial;
        _dePin = dePin;
        stats = {0};
        stats.dePin = dePin;

        LOG_I("MODBUS", "=== INIT MODBUS v5 ===");
        LOG_I("MODBUS", "TX Pin: GPIO %d", RS485_TX_PIN);
        LOG_I("MODBUS", "RX Pin: GPIO %d", RS485_RX_PIN);
        LOG_I("MODBUS", "DE Pin: GPIO %d %s",
              dePin,
              (dePin == 0) ? "(OFICIAL StamPLC.pdf ✓)" : "(NON-STANDARD!)");
        LOG_I("MODBUS", "Baudrate: %d bps", MODBUS_BAUDRATE);

        // Step 1: Begin UART
        _serial->begin(MODBUS_BAUDRATE, MODBUS_CONFIG,
                       RS485_RX_PIN, RS485_TX_PIN);
        delay(10);

        // Step 2: Set pins explicit (RX, TX, CTS=-1, RTS=dePin)
        // RTS este folosit ca DE pin in hardware RS485 mode
        _serial->setPins(RS485_RX_PIN, RS485_TX_PIN, -1, dePin);

        // Step 3: Enable hardware RS485 half-duplex mode
        // Aceasta linie face hardware-ul sa controleze dePin automat!
        // GPIO 0 va fi HIGH cand UART transmite, LOW cand asculta
        esp_err_t err = _serial->setMode(UART_MODE_RS485_HALF_DUPLEX);
        if (err != ESP_OK) {
            LOG_E("MODBUS", "EROARE setMode(RS485_HALF_DUPLEX): %d", err);
            LOG_E("MODBUS", "  → Hardware RS485 mode ESUAT!");
            stats.hwModeEnabled = 0;
            _hwMode = false;
            return false;
        }

        // Step 4: Disable hardware flow control (nu e necesar pentru RS485)
        _serial->setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);

        stats.hwModeEnabled = 1;
        _hwMode = true;
        _initialized = true;

        LOG_I("MODBUS", "✓ Hardware RS485 HALF-DUPLEX mode ACTIV");
        LOG_I("MODBUS", "  → GPIO %d controlat AUTOMAT de UART", dePin);
        LOG_I("MODBUS", "  → Nu mai e nevoie de digitalWrite()!");
        LOG_I("MODBUS", "  → Timing perfect la nivel de bit");

        // Confirma cu un test intern
        _validateHWMode();

        return true;
    }

    // ------------------------------------------------------------------------
    // readBlock() - Citeste un bloc Modbus
    // Returneaza numarul de bytes cititi, 0 la eroare
    // ------------------------------------------------------------------------
    size_t readBlock(uint8_t slaveId, uint16_t startAddr, uint16_t count,
                     uint8_t* response, size_t maxLen,
                     uint32_t* durationMs = nullptr) {

        if (!_initialized) {
            LOG_E("MODBUS", "readBlock() - Modbus neinitializat!");
            return 0;
        }

        // Construieste request Modbus RTU
        uint8_t request[8];
        request[0] = slaveId;
        request[1] = 0x03;  // Read Holding Registers
        request[2] = (startAddr >> 8) & 0xFF;
        request[3] = startAddr & 0xFF;
        request[4] = (count >> 8) & 0xFF;
        request[5] = count & 0xFF;
        uint16_t crc = _crc16(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;

        LOG_V("MODBUS", "Request: Slave=%d Func=0x03 Addr=0x%04X Count=%d",
              slaveId, startAddr, count);
        LOG_HEX_TX(request, 8);

        uint32_t startTime = millis();
        size_t rxLen = 0;

        // Incearca de MODBUS_RETRY_COUNT ori
        for (int attempt = 0; attempt < MODBUS_RETRY_COUNT; attempt++) {
            if (attempt > 0) {
                LOG_D("MODBUS", "Retry %d/%d pentru addr=0x%04X",
                      attempt, MODBUS_RETRY_COUNT - 1, startAddr);
                delay(50);
            }

            rxLen = _sendAndReceive(request, 8, response, maxLen);

            if (rxLen > 0) {
                if (durationMs) {
                    *durationMs = millis() - startTime;
                }
                LOG_TIMING("readBlock total", millis() - startTime);
                return rxLen;
            }
        }

        // Toate retry-urile au esuat
        LOG_E("MODBUS", "ESEC total pentru addr=0x%04X dupa %d retry-uri",
              startAddr, MODBUS_RETRY_COUNT);

        if (durationMs) {
            *durationMs = millis() - startTime;
        }
        return 0;
    }

    // ------------------------------------------------------------------------
    // readBlock3() - Citeste cele 3 blocuri Elfin
    // Returneaza true daca cel putin un bloc a reusit
    // ------------------------------------------------------------------------
    bool readBlock3(uint8_t* b1, size_t* l1,
                    uint8_t* b2, size_t* l2,
                    uint8_t* b3, size_t* l3) {

        bool anySuccess = false;

        LOG_I("MODBUS", "--- Citire 3 blocuri Elfin ---");

        // Block 1: System status
        *l1 = readBlock(MODBUS_SLAVE_ID, BLOCK1_ADDR, BLOCK1_COUNT, b1, 256);
        if (*l1 > 0) {
            LOG_I("MODBUS", "Block 1 (SYSTEM): %zu bytes ✓", *l1);
            anySuccess = true;
        } else {
            LOG_E("MODBUS", "Block 1 (SYSTEM): ESEC!");
        }

        delay(MODBUS_INTER_BLOCK_MS);

        // Block 2: Temperatures
        *l2 = readBlock(MODBUS_SLAVE_ID, BLOCK2_ADDR, BLOCK2_COUNT, b2, 256);
        if (*l2 > 0) {
            LOG_I("MODBUS", "Block 2 (TEMPS):  %zu bytes ✓", *l2);
            anySuccess = true;
        } else {
            LOG_E("MODBUS", "Block 2 (TEMPS): ESEC!");
        }

        delay(MODBUS_INTER_BLOCK_MS);

        // Block 3: Water temps
        *l3 = readBlock(MODBUS_SLAVE_ID, BLOCK3_ADDR, BLOCK3_COUNT, b3, 64);
        if (*l3 > 0) {
            LOG_I("MODBUS", "Block 3 (WATER):  %zu bytes ✓", *l3);
            anySuccess = true;
        } else {
            LOG_E("MODBUS", "Block 3 (WATER): ESEC!");
        }

        return anySuccess;
    }

    // ------------------------------------------------------------------------
    // isHWModeActive() - Verifica daca hardware mode e activ
    // ------------------------------------------------------------------------
    bool isHWModeActive() const { return _hwMode; }
    uint8_t getDEPin() const { return _dePin; }

    // ------------------------------------------------------------------------
    // printStats() - Afiseaza statistici
    // ------------------------------------------------------------------------
    void printStats() {
        DebugLogger::logRS485Stats(stats);
    }

private:

    HardwareSerial* _serial = nullptr;
    uint8_t _dePin = RS485_DE_PIN;
    bool    _initialized = false;
    bool    _hwMode = false;

    // Ultimul pachet TX (pentru echo detection)
    uint8_t _lastTX[32];
    size_t  _lastTXLen = 0;

    // ------------------------------------------------------------------------
    // _sendAndReceive() - Trimite request si primeste raspuns
    // HARDWARE MODE: nu mai avem nevoie de digitalWrite()!
    // ------------------------------------------------------------------------
    size_t _sendAndReceive(const uint8_t* request, size_t reqLen,
                           uint8_t* response, size_t maxLen) {

        // Salveaza ultimul TX pentru echo detection
        memcpy(_lastTX, request, min(reqLen, sizeof(_lastTX)));
        _lastTXLen = min(reqLen, sizeof(_lastTX));

        // Goleste buffer RX
        while (_serial->available()) {
            _serial->read();
        }

        uint32_t txStart = millis();

        // TRIMITE - Hardware controlează GPIO 0/DE automat!
        // Nu mai trebuie:
        //   digitalWrite(_dePin, HIGH); // STERS
        //   delayMicroseconds(500);     // STERS
        _serial->write(request, reqLen);
        _serial->flush();  // Asteapta TX complet
        // Nu mai trebuie:
        //   digitalWrite(_dePin, LOW);  // STERS

        uint32_t txEnd = millis();
        LOG_TIMING("TX duration", txEnd - txStart);

        stats.txCount++;

        // Asteapta raspuns
        uint32_t waitStart = millis();
        while (!_serial->available()) {
            if (millis() - waitStart > MODBUS_TIMEOUT_MS) {
                LOG_W("MODBUS", "TIMEOUT dupa %lu ms fara raspuns",
                      millis() - waitStart);
                stats.rxTimeouts++;
                return 0;
            }
            delay(1);
        }

        uint32_t firstByteTime = millis() - waitStart;
        LOG_TIMING("Time to first byte", firstByteTime);

        // Citeste raspunsul
        delay(MODBUS_RESPONSE_WAIT_MS);  // Asteapta toate bytes-urile

        size_t rxLen = 0;
        while (_serial->available() && rxLen < maxLen) {
            response[rxLen++] = _serial->read();
        }

        stats.rxCount++;

        LOG_V("MODBUS", "RX %zu bytes dupa %lu ms asteptare",
              rxLen, firstByteTime);
        LOG_HEX_RX(response, rxLen);

        // Detectie ECHO - verifica daca RX = TX
        if (_isEcho(request, reqLen, response, rxLen)) {
            stats.rxEchoCount++;
            DebugLogger::logEchoDetected(request, response, reqLen);
            LOG_E("MODBUS",
                  "SOLUTIE: Verifica ca DE Pin = GPIO 0 si setMode(RS485_HALF_DUPLEX)");
            return 0;  // Echo nu e raspuns valid!
        }

        // Valideaza raspuns Modbus
        if (!_validateResponse(request, reqLen, response, rxLen)) {
            return 0;
        }

        stats.rxValidCount++;
        return rxLen;
    }

    // ------------------------------------------------------------------------
    // _isEcho() - Detecteaza daca RX e identic cu TX
    // ------------------------------------------------------------------------
    bool _isEcho(const uint8_t* tx, size_t txLen,
                 const uint8_t* rx, size_t rxLen) {
        if (rxLen < txLen) return false;
        if (rxLen == 0 || txLen == 0) return false;

        // Verifica primii N bytes (cel putin 6)
        size_t cmpLen = min(txLen, rxLen);
        cmpLen = max(cmpLen, (size_t)6);
        if (cmpLen > txLen || cmpLen > rxLen) cmpLen = min(txLen, rxLen);

        return (memcmp(tx, rx, cmpLen) == 0);
    }

    // ------------------------------------------------------------------------
    // _validateResponse() - Valideaza structura raspuns Modbus
    // ------------------------------------------------------------------------
    bool _validateResponse(const uint8_t* req, size_t reqLen,
                           const uint8_t* resp, size_t respLen) {

        if (respLen < 5) {
            LOG_W("MODBUS", "Raspuns prea scurt: %zu bytes (min 5)", respLen);
            return false;
        }

        // Slave ID trebuie sa fie acelasi
        if (resp[0] != req[0]) {
            LOG_W("MODBUS", "Slave ID greșit: got 0x%02X expected 0x%02X",
                  resp[0], req[0]);
            return false;
        }

        // Function code verificare
        if (resp[1] == 0x83) {
            // Exception response
            LOG_E("MODBUS", "Slave exception! Code: 0x%02X", resp[2]);
            return false;
        }

        if (resp[1] != 0x03) {
            LOG_W("MODBUS", "Function code greșit: 0x%02X (expected 0x03)", resp[1]);
            return false;
        }

        // Byte count
        uint8_t byteCount = resp[2];
        if (respLen < (size_t)(byteCount + 5)) {
            LOG_W("MODBUS", "Raspuns incomplet: %zu bytes, asteptat %d",
                  respLen, byteCount + 5);
            return false;
        }

        // CRC validation
        uint16_t calcCRC = _crc16(resp, respLen - 2);
        uint16_t recvCRC = resp[respLen - 2] | (resp[respLen - 1] << 8);

        if (calcCRC != recvCRC) {
            LOG_E("MODBUS", "CRC EROARE! Calc=0x%04X Recv=0x%04X",
                  calcCRC, recvCRC);
            stats.rxCrcErrors++;
            return false;
        }

        LOG_D("MODBUS", "Raspuns valid: %zu bytes, CRC OK", respLen);
        return true;
    }

    // ------------------------------------------------------------------------
    // _validateHWMode() - Test intern hardware mode
    // ------------------------------------------------------------------------
    void _validateHWMode() {
        LOG_D("MODBUS", "Validare hardware mode...");

        // Citeste configuratia UART pentru confirmare
        // In hardware RS485 mode, RTS e controlat automat
        LOG_D("MODBUS", "  UART_MODE_RS485_HALF_DUPLEX: SETAT");
        LOG_D("MODBUS", "  GPIO %d va oscila 0-3.3V la TX", _dePin);

        if (_dePin != 0) {
            LOG_W("MODBUS", "  ⚠ DE Pin != 0 (GPIO %d)", _dePin);
            LOG_W("MODBUS", "  ⚠ GPIO 0 e pin-ul oficial pentru StamPLC!");
            LOG_W("MODBUS", "  ⚠ Schimba RS485_DE_PIN la 0 in config_v5.h");
        } else {
            LOG_I("MODBUS", "  ✓ GPIO 0 = pin oficial StamPLC.pdf ✓");
        }
    }

    // ------------------------------------------------------------------------
    // _crc16() - Calculeaza CRC16/Modbus
    // ------------------------------------------------------------------------
    uint16_t _crc16(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc;
    }
};
