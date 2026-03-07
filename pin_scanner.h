/**
 * @file pin_scanner.h
 * @brief Automatic RS485 DE pin scanner for M5Stamp PLC K141
 * 
 * Tests candidate GPIO pins to find working DE (Driver Enable) pin
 * Priority order based on hardware compatibility:
 * 1. GPIO 46 - Official alternative, strapping but stable
 * 2. GPIO 1  - TXD0, can be repurposed
 * 3. GPIO 4  - PORT.C, safe GPIO
 * 4. GPIO 5  - PORT.C, safe GPIO
 * 5. GPIO 2  - Known problematic (strapping), test last
 * 
 * @version 1.0.0
 * @date 2026-03-07
 */

#ifndef PIN_SCANNER_H
#define PIN_SCANNER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "elfin_protocol.h"

// ============================================================================
// PIN CANDIDATES
// ============================================================================

struct DEPinCandidate {
    uint8_t pin;
    const char* name;
    const char* notes;
    bool tested;
    bool works;
    uint16_t successCount;
    uint16_t failCount;
};

// Priority-ordered list of DE pin candidates
DEPinCandidate DE_PIN_CANDIDATES[] = {
    {46, "GPIO 46", "Official alt, strapping but stable", false, false, 0, 0},
    {1,  "GPIO 1",  "TXD0, can repurpose", false, false, 0, 0},
    {4,  "GPIO 4",  "PORT.C safe GPIO", false, false, 0, 0},
    {5,  "GPIO 5",  "PORT.C safe GPIO", false, false, 0, 0},
    {2,  "GPIO 2",  "Strapping pin - PROBLEMATIC", false, false, 0, 0}
};

#define DE_PIN_CANDIDATE_COUNT 5

// ============================================================================
// SCANNER CLASS
// ============================================================================

class PinScanner {
private:
    HardwareSerial* modbusSerial;
    uint8_t txPin;
    uint8_t rxPin;
    uint32_t baudrate;
    uint8_t currentDePin;
    
    // Test statistics
    struct TestStats {
        uint32_t totalTests;
        uint32_t successfulPins;
        uint32_t failedPins;
        uint32_t totalDuration;
        uint8_t bestPin;
        float bestSuccessRate;
    } stats;
    
public:
    PinScanner(HardwareSerial* serial, uint8_t tx, uint8_t rx, uint32_t baud) 
        : modbusSerial(serial), txPin(tx), rxPin(rx), baudrate(baud), currentDePin(0) {
        memset(&stats, 0, sizeof(stats));
    }
    
    /**
     * @brief Initialize serial port with new DE pin
     * @param dePin DE/RE pin to test
     * @return true if initialization successful
     */
    bool initWithPin(uint8_t dePin) {
        // End previous serial if active
        modbusSerial->end();
        delay(100);
        
        // Configure DE pin as output
        pinMode(dePin, OUTPUT);
        digitalWrite(dePin, LOW);  // Start in receive mode
        
        currentDePin = dePin;
        
        // Initialize UART with new configuration
        modbusSerial->begin(baudrate, SERIAL_8N1, rxPin, txPin);
        modbusSerial->setRxBufferSize(256);
        modbusSerial->setTxBufferSize(256);
        
        // Small delay for hardware to stabilize
        delay(50);
        
        return true;
    }
    
    /**
     * @brief Test a single DE pin with Elfin Block 1
     * @param dePin Pin to test
     * @param testCount Number of read attempts
     * @return Success count
     */
    uint16_t testPin(uint8_t dePin, uint8_t testCount = 3) {
        Serial.printf("\n[PIN TEST] Testing GPIO %d...\n", dePin);
        
        if (!initWithPin(dePin)) {
            Serial.println("  ✗ Init failed");
            return 0;
        }
        
        uint16_t successCount = 0;
        
        // Test with Block 1 (smallest block for quick test)
        const ElfinBlock& block = ELFIN_BLOCKS[0];
        
        for (uint8_t i = 0; i < testCount; i++) {
            // Clear RX buffer
            while (modbusSerial->available()) modbusSerial->read();
            
            // Build Modbus request
            uint8_t request[8];
            request[0] = 0x01;  // Slave ID
            request[1] = 0x03;  // Function code
            request[2] = (block.startAddr >> 8) & 0xFF;
            request[3] = block.startAddr & 0xFF;
            request[4] = (block.count >> 8) & 0xFF;
            request[5] = block.count & 0xFF;
            
            // Calculate CRC16
            uint16_t crc = calculateCRC16(request, 6);
            request[6] = crc & 0xFF;
            request[7] = (crc >> 8) & 0xFF;
            
            // Send request
            digitalWrite(dePin, HIGH);  // TX mode
            delayMicroseconds(100);
            modbusSerial->write(request, 8);
            modbusSerial->flush();
            delayMicroseconds(100);
            digitalWrite(dePin, LOW);   // RX mode
            
            // Wait for response
            uint32_t startTime = millis();
            uint16_t bytesReceived = 0;
            uint8_t response[256];
            
            while (millis() - startTime < 500) {
                if (modbusSerial->available()) {
                    response[bytesReceived++] = modbusSerial->read();
                    if (bytesReceived >= 5) {
                        // Check if we have complete response
                        uint8_t expectedBytes = response[2] + 5;  // header + data + CRC
                        if (bytesReceived >= expectedBytes) {
                            break;
                        }
                    }
                    if (bytesReceived >= 256) break;
                }
            }
            
            // Validate response
            if (bytesReceived >= 7 && response[0] == 0x01 && response[1] == 0x03) {
                // Check CRC
                uint16_t responseCrc = response[bytesReceived - 2] | (response[bytesReceived - 1] << 8);
                uint16_t calculatedCrc = calculateCRC16(response, bytesReceived - 2);
                
                if (responseCrc == calculatedCrc) {
                    successCount++;
                    Serial.printf("  ✓ Test %d/%d: %d bytes, CRC OK\n", i+1, testCount, bytesReceived);
                } else {
                    Serial.printf("  ✗ Test %d/%d: CRC mismatch\n", i+1, testCount);
                }
            } else {
                Serial.printf("  ✗ Test %d/%d: Invalid response (%d bytes)\n", i+1, testCount, bytesReceived);
            }
            
            delay(100);
        }
        
        Serial.printf("  Result: %d/%d successful\n", successCount, testCount);
        return successCount;
    }
    
    /**
     * @brief Scan all candidate pins and find best one
     * @return Best pin number, or 0 if none work
     */
    uint8_t scanAllPins() {
        Serial.println("\n╔════════════════════════════════════════════════════════════╗");
        Serial.println("║  RS485 DE PIN SCANNER - Testing All Candidates           ║");
        Serial.println("╚════════════════════════════════════════════════════════════╝\n");
        
        stats.totalTests = 0;
        stats.successfulPins = 0;
        stats.failedPins = 0;
        stats.bestPin = 0;
        stats.bestSuccessRate = 0.0f;
        
        uint32_t scanStart = millis();
        
        for (uint8_t i = 0; i < DE_PIN_CANDIDATE_COUNT; i++) {
            DEPinCandidate& candidate = DE_PIN_CANDIDATES[i];
            
            Serial.printf("Testing %d/%d: GPIO %d (%s)\n", 
                         i+1, DE_PIN_CANDIDATE_COUNT, candidate.pin, candidate.name);
            Serial.printf("  Notes: %s\n", candidate.notes);
            
            candidate.tested = true;
            uint16_t successCount = testPin(candidate.pin, 5);
            
            candidate.successCount = successCount;
            candidate.failCount = 5 - successCount;
            candidate.works = (successCount >= 3);  // 60% success rate minimum
            
            stats.totalTests++;
            
            if (candidate.works) {
                stats.successfulPins++;
                float successRate = (successCount / 5.0f) * 100.0f;
                
                if (successRate > stats.bestSuccessRate) {
                    stats.bestSuccessRate = successRate;
                    stats.bestPin = candidate.pin;
                }
                
                Serial.printf("  ✓ GPIO %d WORKS! Success rate: %.1f%%\n\n", 
                             candidate.pin, successRate);
            } else {
                stats.failedPins++;
                Serial.printf("  ✗ GPIO %d failed\n\n", candidate.pin);
            }
            
            delay(500);  // Pause between tests
        }
        
        stats.totalDuration = millis() - scanStart;
        
        printScanResults();
        
        return stats.bestPin;
    }
    
    /**
     * @brief Print scan results summary
     */
    void printScanResults() {
        Serial.println("\n╔════════════════════════════════════════════════════════════╗");
        Serial.println("║  SCAN RESULTS                                             ║");
        Serial.println("╚════════════════════════════════════════════════════════════╝\n");
        
        Serial.printf("Total pins tested: %d\n", stats.totalTests);
        Serial.printf("Working pins: %d\n", stats.successfulPins);
        Serial.printf("Failed pins: %d\n", stats.failedPins);
        Serial.printf("Scan duration: %d seconds\n\n", stats.totalDuration / 1000);
        
        Serial.println("PIN TEST RESULTS:");
        Serial.println("─────────────────────────────────────────────────────────────");
        
        for (uint8_t i = 0; i < DE_PIN_CANDIDATE_COUNT; i++) {
            const DEPinCandidate& candidate = DE_PIN_CANDIDATES[i];
            if (candidate.tested) {
                float successRate = (candidate.successCount / 5.0f) * 100.0f;
                Serial.printf("GPIO %2d: %s [%d/5 success, %.1f%%] %s\n",
                             candidate.pin,
                             candidate.works ? "✓ WORKS" : "✗ FAILED",
                             candidate.successCount,
                             successRate,
                             candidate.pin == stats.bestPin ? "← BEST" : "");
            }
        }
        
        Serial.println("─────────────────────────────────────────────────────────────\n");
        
        if (stats.bestPin > 0) {
            Serial.printf("🎯 RECOMMENDED PIN: GPIO %d (%.1f%% success rate)\n\n", 
                         stats.bestPin, stats.bestSuccessRate);
            
            Serial.println("Update config.h with:");
            Serial.printf("#define RS485_DE_PIN    %d\n\n", stats.bestPin);
        } else {
            Serial.println("❌ NO WORKING PIN FOUND!\n");
            Serial.println("TROUBLESHOOTING:");
            Serial.println("1. Check RS485 wiring (A+, B-, GND)");
            Serial.println("2. Verify pump is powered ON");
            Serial.println("3. Check Modbus enabled in pump menu");
            Serial.println("4. Add 120Ω termination resistor\n");
        }
    }
    
    /**
     * @brief Get scan statistics
     */
    TestStats getStats() const {
        return stats;
    }
    
private:
    /**
     * @brief Calculate Modbus RTU CRC16
     */
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
};

#endif // PIN_SCANNER_H
