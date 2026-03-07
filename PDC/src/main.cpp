/**
 * @file quick_diagnostic.cpp
 * @brief Fast RS485 Hardware Diagnostic Tool
 * 
 * STANDALONE - No WiFi, no web server, no external dependencies
 * Just Serial + RS485 testing
 * 
 * Rapid validation:
 * 1. GPIO DE control test
 * 2. RS485 echo test  
 * 3. Modbus simple ping (slave ID scan 1-10)
 * 4. Auto pin detection
 * 
 * Upload this, run, report results in < 2 minutes
 * 
 * @version 1.0.1
 * @date 2026-03-07
 */

#include <Arduino.h>

// ============================================================================
// CONFIGURATION
// ============================================================================
#define RS485_TX_PIN    42
#define RS485_RX_PIN    43

// Test ALL these DE pins
uint8_t DE_PINS[] = {46, 1, 4, 5, 2, 21};  // Added GPIO 21 as alternative
#define DE_PIN_COUNT 6

#define MODBUS_BAUD     9600
#define TEST_TIMEOUT    200  // Fast timeout for quick testing

HardwareSerial RS485Serial(1);

// ============================================================================
// CRC16 MODBUS
// ============================================================================
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

// ============================================================================
// TEST 1: GPIO DE CONTROL
// ============================================================================
void testGPIOControl(uint8_t dePin) {
    Serial.printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Serial.printf("TEST 1: GPIO %d Control\n", dePin);
    Serial.printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    pinMode(dePin, OUTPUT);
    
    // Test HIGH
    digitalWrite(dePin, HIGH);
    delay(100);
    Serial.printf("  GPIO %d HIGH → Should measure 3.3V on DE pin\n", dePin);
    delay(500);
    
    // Test LOW
    digitalWrite(dePin, LOW);
    delay(100);
    Serial.printf("  GPIO %d LOW  → Should measure 0V on DE pin\n", dePin);
    delay(500);
    
    Serial.println("  ✓ GPIO toggling works (software level)");
}

// ============================================================================
// TEST 2: RS485 ECHO TEST
// ============================================================================
bool testRS485Echo(uint8_t dePin) {
    Serial.printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    Serial.printf("TEST 2: RS485 Echo Test (GPIO %d)\n", dePin);
    Serial.printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    
    RS485Serial.end();
    delay(100);
    
    pinMode(dePin, OUTPUT);
    digitalWrite(dePin, LOW);
    
    RS485Serial.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    delay(100);
    
    // Clear buffer
    while (RS485Serial.available()) RS485Serial.read();
    
    // Send test pattern
    uint8_t testPattern[] = {0xAA, 0x55, 0x01, 0x02, 0x03};
    
    digitalWrite(dePin, HIGH);
    delay(1);
    RS485Serial.write(testPattern, 5);
    RS485Serial.flush();
    delay(5);
    digitalWrite(dePin, LOW);
    delay(1);
    
    // Check for echo
    delay(50);
    uint8_t bytesReceived = 0;
    uint8_t received[10];
    
    while (RS485Serial.available() && bytesReceived < 10) {
        received[bytesReceived++] = RS485Serial.read();
    }
    
    Serial.printf("  Sent:     ");
    for (int i = 0; i < 5; i++) Serial.printf("%02X ", testPattern[i]);
    Serial.printf("\n  Received: ");
    for (int i = 0; i < bytesReceived; i++) Serial.printf("%02X ", received[i]);
    Serial.println();
    
    if (bytesReceived == 5 && memcmp(testPattern, received, 5) == 0) {
        Serial.println("  ✗ ECHO DETECTED! RS485 transceiver NOT switching to RX");
        Serial.println("    → Problem: DE pin not controlling transceiver correctly");
        return false;
    } else if (bytesReceived == 0) {
        Serial.println("  ✓ No echo (good!)");
        Serial.println("    → DE pin appears to work, but pump not responding");
        return true;
    } else {
        Serial.println("  ? Partial data received");
        return false;
    }
}

// ============================================================================
// TEST 3: MODBUS SIMPLE PING
// ============================================================================
bool testModbusPing(uint8_t dePin, uint8_t slaveId) {
    RS485Serial.end();
    delay(100);
    
    pinMode(dePin, OUTPUT);
    digitalWrite(dePin, LOW);
    
    RS485Serial.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    delay(100);
    
    // Clear buffer
    while (RS485Serial.available()) RS485Serial.read();
    
    // Simplest Modbus request: Read 1 register from address 0
    uint8_t request[8];
    request[0] = slaveId;    // Slave ID
    request[1] = 0x03;       // Function code
    request[2] = 0x00;       // Address high
    request[3] = 0x00;       // Address low
    request[4] = 0x00;       // Count high
    request[5] = 0x01;       // Count low (read 1 register)
    
    uint16_t crc = calculateCRC16(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;
    
    // Send with extreme delays
    digitalWrite(dePin, HIGH);
    delay(5);  // 5ms before TX
    RS485Serial.write(request, 8);
    RS485Serial.flush();
    delay(10);  // 10ms after TX
    digitalWrite(dePin, LOW);
    delay(5);  // 5ms stabilization
    
    // Wait for response
    uint32_t start = millis();
    uint8_t response[64];
    uint8_t bytesReceived = 0;
    
    while (millis() - start < TEST_TIMEOUT && bytesReceived < 64) {
        if (RS485Serial.available()) {
            response[bytesReceived++] = RS485Serial.read();
        }
    }
    
    // Analyze response
    if (bytesReceived == 0) {
        return false;  // No response
    }
    
    // Check if it's an echo
    if (bytesReceived >= 5 && 
        response[0] == slaveId && 
        response[1] == 0x03 && 
        response[2] == 0x00 && 
        response[3] == 0x00) {
        Serial.printf("    ECHO from slave %d (DE pin problem!)\n", slaveId);
        return false;
    }
    
    // Check for valid Modbus response
    if (bytesReceived >= 5 && 
        response[0] == slaveId && 
        response[1] == 0x03) {
        Serial.printf("    ✓ VALID response from slave %d (%d bytes)!\n", slaveId, bytesReceived);
        Serial.printf("      Data: ");
        for (int i = 0; i < bytesReceived && i < 16; i++) {
            Serial.printf("%02X ", response[i]);
        }
        Serial.println();
        return true;
    }
    
    // Unknown response
    Serial.printf("    ? Unknown response from slave %d: ", slaveId);
    for (int i = 0; i < bytesReceived && i < 8; i++) {
        Serial.printf("%02X ", response[i]);
    }
    Serial.println();
    return false;
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n\n");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║  RS485 HARDWARE DIAGNOSTIC TOOL                          ║");
    Serial.println("║  M5Stamp PLC K141 - Quick Validation                     ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝\n");
    
    Serial.println("This tool will:");
    Serial.println("  1. Test GPIO DE control");
    Serial.println("  2. Test RS485 echo (hardware loopback)");
    Serial.println("  3. Scan Modbus slave IDs 1-10");
    Serial.println("  4. Test all candidate DE pins\n");
    
    Serial.println("Starting in 3 seconds...\n");
    delay(3000);
    
    // ========================================================================
    // RUN TESTS FOR EACH DE PIN
    // ========================================================================
    
    bool foundWorkingPin = false;
    uint8_t workingPin = 0;
    uint8_t workingSlaveId = 0;
    
    for (uint8_t pinIdx = 0; pinIdx < DE_PIN_COUNT; pinIdx++) {
        uint8_t dePin = DE_PINS[pinIdx];
        
        Serial.println("\n\n");
        Serial.println("════════════════════════════════════════════════════════════");
        Serial.printf("TESTING DE PIN: GPIO %d\n", dePin);
        Serial.println("════════════════════════════════════════════════════════════");
        
        // Test 1: GPIO control
        testGPIOControl(dePin);
        
        // Test 2: Echo test
        bool noEcho = testRS485Echo(dePin);
        
        if (!noEcho) {
            Serial.printf("\n✗ GPIO %d has ECHO problem - skipping Modbus test\n", dePin);
            continue;
        }
        
        // Test 3: Modbus ping for slave IDs 1-10
        Serial.printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        Serial.printf("TEST 3: Modbus Scan (GPIO %d, Slave 1-10)\n", dePin);
        Serial.printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        
        for (uint8_t slaveId = 1; slaveId <= 10; slaveId++) {
            Serial.printf("  Testing slave %d... ", slaveId);
            
            if (testModbusPing(dePin, slaveId)) {
                foundWorkingPin = true;
                workingPin = dePin;
                workingSlaveId = slaveId;
                break;
            } else {
                Serial.println("no response");
            }
            
            delay(100);
        }
        
        if (foundWorkingPin) {
            break;  // Found working config!
        }
    }
    
    // ========================================================================
    // FINAL RESULTS
    // ========================================================================
    
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║  DIAGNOSTIC RESULTS                                       ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝\n");
    
    if (foundWorkingPin) {
        Serial.println("✓✓✓ SUCCESS! ✓✓✓\n");
        Serial.printf("Working Configuration Found:\n");
        Serial.printf("  DE Pin:    GPIO %d\n", workingPin);
        Serial.printf("  Slave ID:  %d\n", workingSlaveId);
        Serial.printf("  Baudrate:  9600\n");
        Serial.printf("  TX Pin:    GPIO %d\n", RS485_TX_PIN);
        Serial.printf("  RX Pin:    GPIO %d\n\n", RS485_RX_PIN);
        
        Serial.println("Update your config.h:");
        Serial.printf("#define RS485_DE_PIN    %d\n", workingPin);
        Serial.printf("#define MODBUS_SLAVE_ID %d\n\n", workingSlaveId);
    } else {
        Serial.println("✗✗✗ NO WORKING CONFIGURATION FOUND ✗✗✗\n");
        Serial.println("Possible causes:");
        Serial.println("  1. RS485 wiring incorrect:");
        Serial.println("     → Check A+ connected to pump A+ (or D+)");
        Serial.println("     → Check B- connected to pump B- (or D-)");
        Serial.println("     → Check GND connected");
        Serial.println("  2. Try SWAPPING A+ and B-");
        Serial.println("  3. Pump is OFF or Modbus disabled in menu");
        Serial.println("  4. RS485 transceiver hardware fault");
        Serial.println("  5. Baudrate mismatch (try 19200 baud)\n");
        
        Serial.println("NEXT STEPS:");
        Serial.println("  → Physically swap A+ with B- wires");
        Serial.println("  → Re-upload this diagnostic");
        Serial.println("  → Report results\n");
    }
    
    Serial.println("Diagnostic complete. Press RESET to run again.\n");
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
    // Diagnostic runs once in setup()
    delay(1000);
}
