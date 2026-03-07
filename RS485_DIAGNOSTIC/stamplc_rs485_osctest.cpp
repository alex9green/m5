/**
 * @file stamplc_rs485_osctest.cpp
 * @brief M5Stack StampPLC — RS485 Oscilloscope Test
 *
 * PINOUT CONFIRMAT (StampPLC hardware):
 *   RS485 TX  = GPIO 0   (DI → transceiver)
 *   RS485 RX  = GPIO 39  (RO ← transceiver)
 *   RS485 DE  = GPIO 46  (Driver Enable) ← testam cu osciloscopul
 *
 * CONSOLA: USB-CDC (Serial) — NU foloseste GPIO 43/44
 *
 * COMPILARE — platformio.ini:
 *   [env:m5stack-stamps3]
 *   platform   = espressif32
 *   board      = m5stack-stamps3
 *   framework  = arduino
 *   build_flags =
 *       -DARDUINO_USB_CDC_ON_BOOT=1
 *       -DARDUINO_USB_MODE=1
 *
 * MENIU SERIAL (115200 baud):
 *   1 → Test DE pin: puls manual pe GPIO 46
 *   2 → Test TX: trimite 0xAA 0x55 continuu (pattern osciloscop)
 *   3 → Test Modbus RTU ping slave 1 (FC03 1 registru)
 *   4 → Scan DE candidati: GPIO 0,1,2,3,4,5,46
 *   5 → Raw loopback test (TX→RX intern)
 *   6 → Dump: stare GPIO 0, 39, 46 la fiecare 500ms
 *   0 → STOP toate testele
 *
 * @version 1.0.0
 */

#include <Arduino.h>
#include <HardwareSerial.h>

// ── Pin definitions ────────────────────────────────────────────────────────
#define RS485_TX_PIN   0    // GPIO 0  — confirmat osciloscop
#define RS485_RX_PIN   39   // GPIO 39 — oficial M5Stack
#define RS485_DE_PIN   46   // GPIO 46 — de verificat cu osciloscopul!

// Candidati DE alternativi de testat
const int DE_CANDIDATES[] = { 46, 1, 2, 3, 4, 5 };
const int DE_CANDIDATE_COUNT = 6;

// ── Serial ─────────────────────────────────────────────────────────────────
HardwareSerial RS485Serial(2);   // UART2 → GPIO 0 TX, GPIO 39 RX

#define CONSOLE  Serial          // USB-CDC (nu GPIO 43/44!)
#define BAUD_CON 115200
#define BAUD_485 9600

// ── State ──────────────────────────────────────────────────────────────────
static int  activeTest    = 0;
static int  currentDePin  = RS485_DE_PIN;
static bool rs485Ready    = false;

// ── Forward declarations ───────────────────────────────────────────────────
void initRS485(int dePin);
void testDePulse(int dePin);
void testTxPattern();
void testModbusPing();
void testDeScan();
void testLoopback();
void testGpioDump();
void sendModbusFrame(const uint8_t* frame, uint8_t len, int dePin);
uint16_t crc16(const uint8_t* data, uint8_t len);
void printMenu();
void printSeparator();

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    CONSOLE.begin(BAUD_CON);
    delay(500);

    CONSOLE.println("\n\n");
    printSeparator();
    CONSOLE.println("  M5Stack StampPLC — RS485 Oscilloscope Test v1.0");
    printSeparator();
    CONSOLE.printf("  RS485 TX  = GPIO %-2d\n", RS485_TX_PIN);
    CONSOLE.printf("  RS485 RX  = GPIO %-2d\n", RS485_RX_PIN);
    CONSOLE.printf("  RS485 DE  = GPIO %-2d  ← verifica cu osciloscopul!\n", RS485_DE_PIN);
    printSeparator();
    CONSOLE.println("  CONSOLA: USB-CDC (nu GPIO 43/44)");
    printSeparator();

    initRS485(RS485_DE_PIN);
    printMenu();
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {
    // Citeste comanda din consola
    if (CONSOLE.available()) {
        char c = CONSOLE.read();
        while (CONSOLE.available()) CONSOLE.read();  // flush

        if (c == '0') {
            activeTest = 0;
            CONSOLE.println("\n[STOP] Toate testele oprite.");
            printMenu();
        } else if (c >= '1' && c <= '6') {
            activeTest = c - '0';
            CONSOLE.printf("\n[TEST %c] Pornit.\n", c);
        }
    }

    // Executa testul activ
    switch (activeTest) {
        case 1: testDePulse(currentDePin);  delay(500);  break;
        case 2: testTxPattern();            delay(100);  break;
        case 3: testModbusPing();           delay(1000); break;
        case 4: testDeScan();  activeTest = 0; printMenu(); break;
        case 5: testLoopback(); delay(200); break;
        case 6: testGpioDump(); delay(500); break;
        default: delay(50); break;
    }
}

// =============================================================================
// INIT RS485
// =============================================================================
void initRS485(int dePin) {
    currentDePin = dePin;

    // DE pin
    pinMode(dePin, OUTPUT);
    digitalWrite(dePin, LOW);  // receive mode implicit

    // UART2: RX=39, TX=0
    RS485Serial.begin(BAUD_485, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    RS485Serial.setRxBufferSize(256);
    RS485Serial.setTxBufferSize(256);

    rs485Ready = true;
    CONSOLE.printf("[INIT] RS485 OK  TX=GPIO%d  RX=GPIO%d  DE=GPIO%d\n",
                   RS485_TX_PIN, RS485_RX_PIN, dePin);
}

// =============================================================================
// TEST 1 — DE Pulse (osciloscop pe GPIO 46)
// =============================================================================
void testDePulse(int dePin) {
    // Puls 100ms HIGH pe DE
    CONSOLE.printf("[DE] GPIO%d → HIGH (100ms)\n", dePin);
    digitalWrite(dePin, HIGH);
    delay(100);
    digitalWrite(dePin, LOW);
    CONSOLE.printf("[DE] GPIO%d → LOW\n", dePin);
    // ↑ Pe osciloscop trebuie sa apara un dreptunghi curat 100ms
}

// =============================================================================
// TEST 2 — TX Pattern (0xAA 0x55) pentru osciloscop
// =============================================================================
void testTxPattern() {
    static uint8_t pattern[] = { 0xAA, 0x55, 0xAA, 0x55 };

    digitalWrite(currentDePin, HIGH);   // transmit mode
    delayMicroseconds(100);

    RS485Serial.write(pattern, sizeof(pattern));
    RS485Serial.flush();

    delayMicroseconds(100);
    digitalWrite(currentDePin, LOW);    // receive mode

    CONSOLE.println("[TX] 0xAA 0x55 0xAA 0x55  ← verifica pe osciloscop GPIO 0");
}

// =============================================================================
// TEST 3 — Modbus RTU Ping (FC03, slave 1, 1 registru la adresa 0x0000)
// =============================================================================
void testModbusPing() {
    // Frame: 01 03 00 00 00 01 + CRC
    uint8_t frame[6] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01 };
    uint16_t crc = crc16(frame, 6);

    uint8_t fullFrame[8];
    memcpy(fullFrame, frame, 6);
    fullFrame[6] = crc & 0xFF;
    fullFrame[7] = (crc >> 8) & 0xFF;

    CONSOLE.print("[MODBUS] TX → ");
    for (int i = 0; i < 8; i++) CONSOLE.printf("%02X ", fullFrame[i]);
    CONSOLE.println();

    sendModbusFrame(fullFrame, 8, currentDePin);

    // Asteapta raspuns max 500ms
    unsigned long t0 = millis();
    uint8_t resp[32];
    int respLen = 0;

    while (millis() - t0 < 500 && respLen < 32) {
        if (RS485Serial.available()) {
            resp[respLen++] = RS485Serial.read();
        }
    }

    if (respLen == 0) {
        CONSOLE.println("[MODBUS] ✗ Timeout — fara raspuns");
        CONSOLE.println("         Verifica: cablaj A/B, baud rate, slave ID");
    } else {
        CONSOLE.printf("[MODBUS] ✓ Raspuns (%d bytes): ", respLen);
        for (int i = 0; i < respLen; i++) CONSOLE.printf("%02X ", resp[i]);
        CONSOLE.println();

        // Verifica CRC
        if (respLen >= 4) {
            uint16_t rxCrc = (resp[respLen-1] << 8) | resp[respLen-2];
            uint16_t calcCrc = crc16(resp, respLen - 2);
            CONSOLE.printf("[MODBUS] CRC: calc=%04X recv=%04X %s\n",
                           calcCrc, rxCrc, calcCrc == rxCrc ? "✓ OK" : "✗ EROARE");
        }
    }
}

// =============================================================================
// TEST 4 — Scan candidati DE
// =============================================================================
void testDeScan() {
    CONSOLE.println("\n[SCAN] Testez candidati DE...");
    printSeparator();

    // Frame Modbus ping
    uint8_t frame[6] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01 };
    uint16_t crc = crc16(frame, 6);
    uint8_t fullFrame[8];
    memcpy(fullFrame, frame, 6);
    fullFrame[6] = crc & 0xFF;
    fullFrame[7] = (crc >> 8) & 0xFF;

    int bestPin = -1;

    for (int i = 0; i < DE_CANDIDATE_COUNT; i++) {
        int dePin = DE_CANDIDATES[i];

        // Reinit cu noul DE pin
        if (currentDePin != dePin) {
            pinMode(currentDePin, INPUT);  // elibereaza pinul anterior
            pinMode(dePin, OUTPUT);
            digitalWrite(dePin, LOW);
            currentDePin = dePin;
            delay(50);
        }

        int ok = 0;
        for (int t = 0; t < 3; t++) {
            // Flush RX
            while (RS485Serial.available()) RS485Serial.read();

            sendModbusFrame(fullFrame, 8, dePin);

            unsigned long t0 = millis();
            int respLen = 0;
            uint8_t resp[32];
            while (millis() - t0 < 300 && respLen < 32) {
                if (RS485Serial.available()) resp[respLen++] = RS485Serial.read();
            }
            if (respLen >= 5) ok++;
            delay(100);
        }

        CONSOLE.printf("  GPIO %-2d DE: %d/3 raspunsuri %s\n",
                       dePin, ok, ok >= 2 ? "✓ FUNCTIONEAZA" : "✗");
        if (ok >= 2 && bestPin < 0) bestPin = dePin;
    }

    printSeparator();
    if (bestPin > 0) {
        CONSOLE.printf("[SCAN] ✓ Best DE pin: GPIO %d\n", bestPin);
        initRS485(bestPin);
    } else {
        CONSOLE.println("[SCAN] ✗ Niciun pin DE functional");
        CONSOLE.println("       → Verifica cablaj RS485 A/B si slave device");
    }
}

// =============================================================================
// TEST 5 — Loopback (conecteaza GPIO 0 la GPIO 39 fizic cu un fir!)
// =============================================================================
void testLoopback() {
    static uint8_t cnt = 0;
    uint8_t txByte = 0x30 + (cnt % 10);  // '0'..'9'
    cnt++;

    // Flush
    while (RS485Serial.available()) RS485Serial.read();

    // Trimite fara DE (loopback direct)
    RS485Serial.write(txByte);
    RS485Serial.flush();
    delay(5);

    if (RS485Serial.available()) {
        uint8_t rxByte = RS485Serial.read();
        if (rxByte == txByte) {
            CONSOLE.printf("[LOOPBACK] ✓ TX=0x%02X RX=0x%02X  OK\n", txByte, rxByte);
        } else {
            CONSOLE.printf("[LOOPBACK] ✗ TX=0x%02X RX=0x%02X  MISMATCH\n", txByte, rxByte);
        }
    } else {
        CONSOLE.printf("[LOOPBACK] ✗ TX=0x%02X — nicio data primita\n", txByte);
        CONSOLE.println("           → Conecteaza GPIO 0 la GPIO 39 cu un fir");
    }
}

// =============================================================================
// TEST 6 — GPIO Dump
// =============================================================================
void testGpioDump() {
    int g0  = digitalRead(0);
    int g39 = digitalRead(39);
    int g46 = digitalRead(46);
    CONSOLE.printf("[GPIO] G0(TX)=%d  G39(RX)=%d  G46(DE)=%d  DE_current=GPIO%d\n",
                   g0, g39, g46, currentDePin);
}

// =============================================================================
// HELPERS
// =============================================================================
void sendModbusFrame(const uint8_t* frame, uint8_t len, int dePin) {
    digitalWrite(dePin, HIGH);
    delayMicroseconds(200);

    RS485Serial.write(frame, len);
    RS485Serial.flush();

    delayMicroseconds(200);
    digitalWrite(dePin, LOW);
}

uint16_t crc16(const uint8_t* data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else              crc >>= 1;
        }
    }
    return crc;
}

void printSeparator() {
    CONSOLE.println("──────────────────────────────────────────────────────");
}

void printMenu() {
    CONSOLE.println();
    printSeparator();
    CONSOLE.println("  MENIU:");
    CONSOLE.println("  1 → Puls DE (osciloscop GPIO 46)");
    CONSOLE.println("  2 → Pattern 0xAA 0x55 pe TX (osciloscop GPIO 0)");
    CONSOLE.println("  3 → Modbus ping FC03 slave 1");
    CONSOLE.println("  4 → Scan automat pini DE");
    CONSOLE.println("  5 → Loopback test (GPIO 0 → GPIO 39 cu fir)");
    CONSOLE.println("  6 → Dump GPIO 0/39/46");
    CONSOLE.println("  0 → STOP");
    printSeparator();
    CONSOLE.printf("  DE activ: GPIO %d   TX: GPIO %d   RX: GPIO %d\n",
                   currentDePin, RS485_TX_PIN, RS485_RX_PIN);
    printSeparator();
}
