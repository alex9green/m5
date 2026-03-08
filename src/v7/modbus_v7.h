// ============================================================================
// modbus_v7.h - MODBUS RTU v7 - PINI CONFIRMATI + BAUD SCAN + INTER-BYTE GAP
// ============================================================================
// SCHIMBARI FATA DE v5:
//
//   FIX: delay(100ms) inlocuit cu inter-byte gap (20ms liniste pe linie)
//        La 9600 baud, 87 bytes = 91ms → delay(100ms) era "marginal"
//        Inter-byte gap captureaza frame-ul complet indiferent de baud rate.
//
//   FIX: init() accepta txPin, rxPin explicit (nu mai foloseste constante globale)
//
//   NOU: scanBaudRate() - testeaza {9600, 4800, 19200, 38400}
//        Returneaza primul baud care da raspuns valid.
//
//   NOU: Log hex dump la CRC error pentru diagnosticare
//
//   NOU: RS485Stats include txPin, rxPin, baudrate
//
//   PASTRAT: Hardware UART_MODE_RS485_HALF_DUPLEX (functioneaza pe StamPLC K141)
//   PASTRAT: Fallback manual DE control daca setMode() esueaza
//   PASTRAT: Echo detection, CRC16 Modbus, _validateResponse()
// ============================================================================

#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "config_v7.h"
#include "debug_logger.h"

// ============================================================================
// ModbusV7 CLASS
// ============================================================================
class ModbusV7 {
public:

    RS485Stats stats;

    // ------------------------------------------------------------------------
    // init() - Configureaza UART cu hardware RS485 mode
    // txPin, rxPin, dePin = pini confirmati din hardware
    // ------------------------------------------------------------------------
    bool init(HardwareSerial& serial,
              uint8_t txPin = RS485_TX_PIN,
              uint8_t rxPin = RS485_RX_PIN,
              uint8_t dePin = RS485_DE_PIN,
              uint32_t baud = MODBUS_BAUDRATE) {
        _serial  = &serial;
        _txPin   = txPin;
        _rxPin   = rxPin;
        _dePin   = dePin;
        _baud    = baud;
        stats    = {0};
        stats.dePin   = dePin;
        stats.txPin   = txPin;
        stats.rxPin   = rxPin;
        stats.baudrate = baud;

        LOG_I("MODBUS", "=== INIT MODBUS v7 ===");
        LOG_I("MODBUS", "TX=GPIO%d  RX=GPIO%d  DE=GPIO%d  Baud=%lu",
              txPin, rxPin, dePin, baud);

        // Step 1: setPins INAINTE de begin() - CRITIC pentru setMode()
        _serial->setPins(rxPin, txPin, -1, dePin);

        // Step 2: begin()
        _serial->begin(baud, MODBUS_CONFIG);
        delay(10);

        // Step 3: Hardware RS485 half-duplex mode
        bool hwOK = _serial->setMode(UART_MODE_RS485_HALF_DUPLEX);
        if (!hwOK) {
            LOG_W("MODBUS", "setMode(RS485_HALF_DUPLEX) esuat → manual DE control");
            pinMode(dePin, OUTPUT);
            digitalWrite(dePin, LOW);
            stats.hwModeEnabled = 0;
            _hwMode = false;
        } else {
            _serial->setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
            stats.hwModeEnabled = 1;
            _hwMode = true;
            LOG_I("MODBUS", "✓ Hardware RS485 HALF-DUPLEX mode ACTIV");
        }

        _initialized = true;
        LOG_I("MODBUS", "✓ Init OK: TX=%d RX=%d DE=%d Baud=%lu mode=%s",
              txPin, rxPin, dePin, baud, _hwMode ? "HARDWARE" : "MANUAL");
        return true;
    }

    // ------------------------------------------------------------------------
    // scanBaudRate() - Testeaza baud rates si returneaza cel corect
    // APELEAZA DUPA init() cu baud default!
    // Returneaza baud rate valid sau 0 daca niciun baud nu functioneaza.
    // ------------------------------------------------------------------------
    uint32_t scanBaudRate(HardwareSerial& serial) {
        static const uint32_t bauds[] = BAUD_SCAN_CANDIDATES;

        LOG_SEPARATOR();
        LOG_I("BAUD", "=== SCAN BAUD RATE ===");
        LOG_I("BAUD", "Testez: 9600, 4800, 19200, 38400 bps");
        LOG_I("BAUD", "TX=%d RX=%d DE=%d", _txPin, _rxPin, _dePin);
        LOG_SEPARATOR();

        for (int b = 0; b < BAUD_SCAN_COUNT; b++) {
            uint32_t baud = bauds[b];
            LOG_I("BAUD", "--- Test %lu bps ---", baud);

            // Reconfigureaza UART cu noul baud
            serial.end();
            delay(20);
            serial.setPins(_rxPin, _txPin, -1, _dePin);
            serial.begin(baud, MODBUS_CONFIG);
            delay(20);
            serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
            serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);

            int successCount = 0;
            for (int t = 0; t < BAUD_SCAN_TESTS; t++) {
                uint8_t resp[256];
                size_t rxLen = _sendAndReceive(
                    nullptr, 0, resp, sizeof(resp), true /* probe */);

                if (rxLen > 0) {
                    successCount++;
                    LOG_I("BAUD", "  Test %d/%d: ✓ SUCCESS %zu bytes",
                          t+1, BAUD_SCAN_TESTS, rxLen);
                } else {
                    // Log raw bytes daca am primit ceva (chiar si cu CRC error)
                    LOG_D("BAUD", "  Test %d/%d: esuat", t+1, BAUD_SCAN_TESTS);
                }
                delay(150);
            }

            if (successCount >= 1) {
                LOG_I("BAUD", "✓ BAUD RATE CONFIRMAT: %lu bps (%d/%d succese)",
                      baud, successCount, BAUD_SCAN_TESTS);
                _baud = baud;
                stats.baudrate = baud;
                return baud;
            }

            LOG_I("BAUD", "  %lu bps: 0/%d succese", baud, BAUD_SCAN_TESTS);
        }

        LOG_E("BAUD", "Niciun baud rate nu functioneaza!");
        LOG_E("BAUD", "  → Verifica conexiunile A/B la pompa");
        LOG_E("BAUD", "  → Verifica Slave ID (curent: 0x%02X)", MODBUS_SLAVE_ID);

        // Revenim la baud default
        serial.end();
        delay(20);
        serial.setPins(_rxPin, _txPin, -1, _dePin);
        serial.begin(MODBUS_BAUDRATE, MODBUS_CONFIG);
        serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
        serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
        return 0;
    }

    // ------------------------------------------------------------------------
    // readBlock() - Citeste un bloc Modbus (cu retry)
    // ------------------------------------------------------------------------
    size_t readBlock(uint8_t slaveId, uint16_t startAddr, uint16_t count,
                     uint8_t* response, size_t maxLen,
                     uint32_t* durationMs = nullptr) {

        if (!_initialized) return 0;

        uint8_t request[8];
        request[0] = slaveId;
        request[1] = 0x03;
        request[2] = (startAddr >> 8) & 0xFF;
        request[3] = startAddr & 0xFF;
        request[4] = (count >> 8) & 0xFF;
        request[5] = count & 0xFF;
        uint16_t crc = _crc16(request, 6);
        request[6] = crc & 0xFF;
        request[7] = (crc >> 8) & 0xFF;

        LOG_HEX_TX(request, 8);

        uint32_t startTime = millis();

        for (int attempt = 0; attempt <= MODBUS_RETRY_COUNT; attempt++) {
            if (attempt > 0) {
                LOG_D("MODBUS", "Retry %d/%d addr=0x%04X", attempt, MODBUS_RETRY_COUNT, startAddr);
                delay(100);
            }

            size_t rxLen = _sendAndReceive(request, 8, response, maxLen);

            if (rxLen > 0) {
                if (durationMs) *durationMs = millis() - startTime;
                return rxLen;
            }
        }

        LOG_E("MODBUS", "ESEC addr=0x%04X dupa %d retry-uri", startAddr, MODBUS_RETRY_COUNT);
        if (durationMs) *durationMs = millis() - startTime;
        return 0;
    }

    // ------------------------------------------------------------------------
    // readBlock3() - Citeste cele 3 blocuri Elfin
    // ------------------------------------------------------------------------
    bool readBlock3(uint8_t* b1, size_t* l1,
                    uint8_t* b2, size_t* l2,
                    uint8_t* b3, size_t* l3) {
        bool any = false;

        LOG_I("MODBUS", "--- Citire 3 blocuri Elfin @ %lu bps ---", _baud);

        *l1 = readBlock(MODBUS_SLAVE_ID, BLOCK1_ADDR, BLOCK1_COUNT, b1, 256);
        if (*l1 > 0) { LOG_I("MODBUS", "Block1 SYSTEM: %zu bytes ✓", *l1); any = true; }
        else            LOG_E("MODBUS", "Block1 SYSTEM: ESEC");

        delay(MODBUS_INTER_BLOCK_MS);

        *l2 = readBlock(MODBUS_SLAVE_ID, BLOCK2_ADDR, BLOCK2_COUNT, b2, 256);
        if (*l2 > 0) { LOG_I("MODBUS", "Block2 TEMPS:  %zu bytes ✓", *l2); any = true; }
        else            LOG_E("MODBUS", "Block2 TEMPS:  ESEC");

        delay(MODBUS_INTER_BLOCK_MS);

        *l3 = readBlock(MODBUS_SLAVE_ID, BLOCK3_ADDR, BLOCK3_COUNT, b3, 64);
        if (*l3 > 0) { LOG_I("MODBUS", "Block3 WATER:  %zu bytes ✓", *l3); any = true; }
        else            LOG_E("MODBUS", "Block3 WATER:  ESEC");

        return any;
    }

    bool     isHWModeActive() const { return _hwMode; }
    uint8_t  getDEPin()        const { return _dePin; }
    uint8_t  getTXPin()        const { return _txPin; }
    uint8_t  getRXPin()        const { return _rxPin; }
    uint32_t getBaud()         const { return _baud; }

    void printStats() { DebugLogger::logRS485Stats(stats); }

private:

    HardwareSerial* _serial      = nullptr;
    uint8_t  _txPin              = RS485_TX_PIN;
    uint8_t  _rxPin              = RS485_RX_PIN;
    uint8_t  _dePin              = RS485_DE_PIN;
    uint32_t _baud               = MODBUS_BAUDRATE;
    bool     _initialized        = false;
    bool     _hwMode             = false;

    // ------------------------------------------------------------------------
    // _sendAndReceive() - Trimite request, asteapta raspuns cu inter-byte gap
    //
    // probe=true: folosit de baud scan, trimite Block1 request hardcodat.
    // Inter-byte gap: asteptam 20ms de liniste dupa ultimul byte primit.
    // Aceasta metoda captureaza frame-ul complet indiferent de marime/baud.
    // ------------------------------------------------------------------------
    size_t _sendAndReceive(const uint8_t* request, size_t reqLen,
                           uint8_t* response, size_t maxLen,
                           bool probe = false) {
        // Request hardcodat pentru probe (baud scan)
        static const uint8_t PROBE_REQ[8] = {
            MODBUS_SLAVE_ID, 0x03,
            (BLOCK1_ADDR >> 8) & 0xFF, BLOCK1_ADDR & 0xFF,
            (BLOCK1_COUNT >> 8) & 0xFF, BLOCK1_COUNT & 0xFF,
            0x84, 0x14  // CRC pentru Slave=1, Addr=0, Count=0x29
        };
        if (probe) { request = PROBE_REQ; reqLen = 8; }

        // Goleste buffer RX
        while (_serial->available()) _serial->read();

        // Transmisie
        uint32_t txStart = millis();
        if (!_hwMode) {
            digitalWrite(_dePin, HIGH);
            delayMicroseconds(150);
        }
        _serial->write(request, reqLen);
        _serial->flush();
        if (!_hwMode) {
            digitalWrite(_dePin, LOW);
        }
        LOG_TIMING("TX", millis() - txStart);
        stats.txCount++;

        // Asteapta primul byte
        uint32_t waitStart = millis();
        while (!_serial->available()) {
            if (millis() - waitStart > MODBUS_TIMEOUT_MS) {
                stats.rxTimeouts++;
                LOG_W("MODBUS", "TIMEOUT %lu ms", MODBUS_TIMEOUT_MS);
                return 0;
            }
            delay(1);
        }

        // *** INTER-BYTE GAP: citeste pana la 20ms de liniste pe linie ***
        // Asta captureaza frame-ul complet indiferent de baud rate sau marime.
        // La 9600 baud: 87 bytes = 91ms. La 19200: 87 bytes = 46ms.
        // Cu gap 20ms, vom astepta sfarsitul frame-ului in ambele cazuri.
        size_t   rxLen      = 0;
        uint32_t lastRxTime = millis();
        while (millis() - lastRxTime < 20 && rxLen < maxLen) {
            if (_serial->available()) {
                response[rxLen++] = _serial->read();
                lastRxTime = millis();
            }
        }

        stats.rxCount++;
        LOG_HEX_RX(response, rxLen);

        if (rxLen < 5) {
            LOG_W("MODBUS", "Raspuns prea scurt: %zu bytes", rxLen);
            return 0;
        }

        // Echo detection
        if (rxLen >= reqLen && memcmp(request, response, min(reqLen, rxLen)) == 0) {
            stats.rxEchoCount++;
            LOG_E("MODBUS", "ECHO detectat! DE pin nu comuta TX→RX");
            return 0;
        }

        // Validare (cu log raw bytes la CRC error)
        if (!_validateResponse(request, reqLen, response, rxLen)) {
            return 0;
        }

        stats.rxValidCount++;
        return rxLen;
    }

    // ------------------------------------------------------------------------
    // _validateResponse() - Valideaza Modbus RTU response
    // Log hex dump la CRC error pentru diagnosticare baud rate
    // ------------------------------------------------------------------------
    bool _validateResponse(const uint8_t* req, size_t reqLen,
                           const uint8_t* resp, size_t respLen) {
        if (respLen < 5) return false;

        if (resp[0] != req[0]) {
            LOG_W("MODBUS", "Slave ID gresit: got=0x%02X expected=0x%02X",
                  resp[0], req[0]);
            _logRawBytes(resp, respLen);
            return false;
        }

        if (resp[1] == (req[1] | 0x80)) {
            LOG_E("MODBUS", "Exception Modbus! Code=0x%02X", resp[2]);
            _logRawBytes(resp, respLen);
            return false;
        }

        if (resp[1] != 0x03) {
            LOG_W("MODBUS", "Function code gresit: 0x%02X", resp[1]);
            _logRawBytes(resp, respLen);
            return false;
        }

        uint8_t byteCount = resp[2];
        if (respLen < (size_t)(byteCount + 5)) {
            LOG_W("MODBUS", "Raspuns incomplet: %zu bytes, asteptat %d",
                  respLen, byteCount + 5);
            _logRawBytes(resp, respLen);
            return false;
        }

        uint16_t calcCRC = _crc16(resp, respLen - 2);
        uint16_t recvCRC = resp[respLen - 2] | (resp[respLen - 1] << 8);

        if (calcCRC != recvCRC) {
            LOG_E("MODBUS", "CRC ERROR! Calc=0x%04X Recv=0x%04X [%zu bytes @ %lu bps]",
                  calcCRC, recvCRC, respLen, _baud);
            // *** LOG RAW BYTES la CRC error - CRITIC pentru diagnosticare ***
            _logRawBytes(resp, respLen);
            stats.rxCrcErrors++;
            return false;
        }

        return true;
    }

    // ------------------------------------------------------------------------
    // _logRawBytes() - Afiseaza hex dump pentru diagnosticare
    // Ajuta la determinarea baud rate-ului corect si a structurii frame-ului
    // ------------------------------------------------------------------------
    void _logRawBytes(const uint8_t* data, size_t len) {
        char buf[256];
        buf[0] = '\0';
        size_t show = min(len, (size_t)32);
        for (size_t i = 0; i < show; i++) {
            char tmp[4];
            snprintf(tmp, sizeof(tmp), "%02X ", data[i]);
            strncat(buf, tmp, sizeof(buf) - strlen(buf) - 1);
        }
        if (len > 32) strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
        LOG_D("MODBUS", "  RAW [%zu bytes]: %s", len, buf);

        // Daca primii 2 bytes sunt 01 03 → frame e corect la nivel baud
        if (len >= 2 && data[0] == MODBUS_SLAVE_ID && data[1] == 0x03) {
            LOG_D("MODBUS", "  → Slave ID si FC corecte! CRC error → frame trunchiat?");
        } else if (len >= 1 && data[0] != MODBUS_SLAVE_ID) {
            LOG_D("MODBUS", "  → Byte[0]=0x%02X != SlaveID=0x%02X → baud rate gresit?",
                  data[0], MODBUS_SLAVE_ID);
        }
    }

    bool _isEcho(const uint8_t* tx, size_t txLen, const uint8_t* rx, size_t rxLen) {
        if (rxLen == 0 || txLen == 0 || rxLen < txLen) return false;
        return (memcmp(tx, rx, txLen) == 0);
    }

    uint16_t _crc16(const uint8_t* data, size_t len) {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
            }
        }
        return crc;
    }
};
