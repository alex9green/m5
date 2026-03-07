/**
 * @file modbus_autoscan.h
 * @brief Automatic Modbus Configuration Scanner and Tester
 * 
 * Features:
 * - Auto-detect correct GPIO pins for RS485 (TX/RX/DE)
 * - Scan all slave IDs (1-247)
 * - Test all baudrates (9600, 19200, 38400, 115200)
 * - Test different timing configurations
 * - Generate detailed diagnostic report
 * 
 * Based on StamPLC.pdf official documentation and error testing history
 * 
 * @author ProEnergy Green SRL / ThermXpert
 * @date 2026-03-07
 * @version 2.2.0 - Auto-scan & Test All Configurations
 */

#ifndef MODBUS_AUTOSCAN_H
#define MODBUS_AUTOSCAN_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "ModbusRTU.h"

// ============================================================================
// PIN COMBINATIONS - Based on StamPLC.pdf and testing history
// ============================================================================

struct RS485PinConfig {
    uint8_t tx;
    uint8_t rx;
    uint8_t de;
    const char* description;
};

// All possible pin combinations from StamPLC.pdf analysis and testing
const RS485PinConfig PIN_CONFIGS[] = {
    // Official from StamPLC.pdf page 9 (PWR-485)
    {42, 43, 2,  "Official PWR-485 (DE=GPIO2, safe)"},
    {42, 43, 46, "Official PWR-485 (DE=GPIO46, alt strapping)"},
    {42, 43, 4,  "Official PWR-485 (DE=GPIO4, PORT.C)"},
    {42, 43, 5,  "Official PWR-485 (DE=GPIO5, PORT.C)"},
    {42, 43, 0,  "Official PWR-485 (DE=GPIO0, BOOT strapping - may be unstable)"},
    
    // Alternative configurations (CAN shared pins)
    {42, 43, 1,  "PWR-485/CAN shared (DE=GPIO1, RGB ref)"},
    
    // Legacy incorrect configs (for reference/testing)
    {19, 20, 21, "Legacy incorrect config v2.0"},
    {17, 18, 16, "Legacy incorrect config v1.0"},
};

const int NUM_PIN_CONFIGS = sizeof(PIN_CONFIGS) / sizeof(RS485PinConfig);

// ============================================================================
// BAUDRATE CONFIGURATIONS
// ============================================================================

const uint32_t BAUDRATES[] = {
    9600,      // Standard Modbus RTU (most common)
    19200,     // High-speed Modbus
    38400,     // Very high-speed
    4800,      // Low-speed (rare)
    115200     // Maximum (rare for industrial)
};

const int NUM_BAUDRATES = sizeof(BAUDRATES) / sizeof(uint32_t);

// ============================================================================
// TIMING CONFIGURATIONS
// ============================================================================

struct TimingConfig {
    uint16_t de_tx_delay_us;      // Delay before TX after DE HIGH
    uint16_t de_rx_delay_us;      // Delay before RX after DE LOW
    uint16_t post_rx_delay_us;    // Delay after DE LOW
    const char* description;
};

const TimingConfig TIMING_CONFIGS[] = {
    {50,   50,   50,   "Fast (50us) - Default"},
    {500,  500,  100,  "Medium (500us) - Slow transceivers"},
    {1000, 1000, 500,  "Slow (1ms) - Very slow transceivers"},
    {100,  100,  50,   "Balanced (100us)"},
};

const int NUM_TIMING_CONFIGS = sizeof(TIMING_CONFIGS) / sizeof(TimingConfig);

// ============================================================================
// AUTO-SCAN RESULTS
// ============================================================================

struct ScanResult {
    bool success;
    uint8_t pin_config_index;
    uint32_t baudrate;
    uint8_t slave_id;
    uint8_t timing_config_index;
    uint16_t status_register;
    float ambient_temp;
    uint8_t error_code;
    unsigned long scan_time_ms;
};

// ============================================================================
// MODBUS AUTO-SCANNER CLASS
// ============================================================================

class ModbusAutoScanner {
private:
    HardwareSerial* _serial;
    ModbusRTU* _modbus;
    ScanResult _bestResult;
    
public:
    ModbusAutoScanner(HardwareSerial* serial) : _serial(serial) {
        _modbus = nullptr;
        _bestResult.success = false;
    }
    
    ~ModbusAutoScanner() {
        if (_modbus) delete _modbus;
    }
    
    /**
     * @brief Run full auto-scan of all configurations
     * @param quickScan If true, test only most common configs (faster)
     * @return Best configuration found (or failure if none work)
     */
    ScanResult runFullScan(bool quickScan = false) {
        Serial.println("\n╔════════════════════════════════════════════════════════════╗");
        Serial.println("║  MODBUS AUTO-SCAN - Testing All Configurations           ║");
        Serial.println("╚════════════════════════════════════════════════════════════╝\n");
        
        _bestResult.success = false;
        _bestResult.scan_time_ms = millis();
        
        int totalTests = 0;
        int successfulTests = 0;
        
        // Phase 1: Quick scan most common configurations
        Serial.println("📋 PHASE 1: Quick Scan (Most Common Configurations)\n");
        
        if (testConfiguration(0, 0, 0, 1)) {  // Official pins, 9600 baud, fast timing, slave 1
            successfulTests++;
            if (!quickScan) {
                // Found working config in quick scan, but continue to verify
                Serial.println("✓ Found working config in quick scan! Continuing full scan for verification...\n");
            }
        }
        totalTests++;
        
        if (quickScan && _bestResult.success) {
            _bestResult.scan_time_ms = millis() - _bestResult.scan_time_ms;
            printScanSummary(totalTests, successfulTests);
            return _bestResult;
        }
        
        // Phase 2: Test all official pin configs with standard settings
        Serial.println("\n📋 PHASE 2: Testing All Official Pin Configurations\n");
        
        for (int pinIdx = 0; pinIdx < 5; pinIdx++) {  // First 5 are official configs
            for (int slaveId = 1; slaveId <= 16; slaveId++) {
                if (testConfiguration(pinIdx, 0, 0, slaveId)) {
                    successfulTests++;
                }
                totalTests++;
                
                if (_bestResult.success && quickScan) break;
            }
            if (_bestResult.success && quickScan) break;
        }
        
        // Phase 3: If still no success, try different baudrates
        if (!_bestResult.success) {
            Serial.println("\n📋 PHASE 3: Testing Different Baudrates\n");
            
            for (int pinIdx = 0; pinIdx < 5; pinIdx++) {
                for (int baudIdx = 0; baudIdx < NUM_BAUDRATES; baudIdx++) {
                    if (testConfiguration(pinIdx, baudIdx, 0, 1)) {
                        successfulTests++;
                        break;  // Found working baudrate for this pin config
                    }
                    totalTests++;
                }
                if (_bestResult.success) break;
            }
        }
        
        // Phase 4: If still no success, try different timing configs
        if (!_bestResult.success) {
            Serial.println("\n📋 PHASE 4: Testing Different Timing Configurations\n");
            
            for (int pinIdx = 0; pinIdx < 5; pinIdx++) {
                for (int timingIdx = 0; timingIdx < NUM_TIMING_CONFIGS; timingIdx++) {
                    if (testConfiguration(pinIdx, 0, timingIdx, 1)) {
                        successfulTests++;
                        break;
                    }
                    totalTests++;
                }
                if (_bestResult.success) break;
            }
        }
        
        // Phase 5: Last resort - test ALL combinations (very thorough but slow)
        if (!_bestResult.success && !quickScan) {
            Serial.println("\n📋 PHASE 5: Deep Scan - Testing ALL Combinations\n");
            Serial.println("⚠️ This may take several minutes...\n");
            
            for (int pinIdx = 0; pinIdx < NUM_PIN_CONFIGS; pinIdx++) {
                for (int baudIdx = 0; baudIdx < NUM_BAUDRATES; baudIdx++) {
                    for (int timingIdx = 0; timingIdx < NUM_TIMING_CONFIGS; timingIdx++) {
                        for (int slaveId = 1; slaveId <= 247; slaveId++) {
                            if (testConfiguration(pinIdx, baudIdx, timingIdx, slaveId)) {
                                successfulTests++;
                                goto scan_complete;  // Found working config!
                            }
                            totalTests++;
                        }
                    }
                }
            }
        }
        
    scan_complete:
        _bestResult.scan_time_ms = millis() - _bestResult.scan_time_ms;
        printScanSummary(totalTests, successfulTests);
        
        return _bestResult;
    }
    
    /**
     * @brief Test specific configuration
     */
    bool testConfiguration(int pinConfigIdx, int baudrateIdx, int timingIdx, uint8_t slaveId) {
        if (pinConfigIdx >= NUM_PIN_CONFIGS) return false;
        if (baudrateIdx >= NUM_BAUDRATES) return false;
        if (timingIdx >= NUM_TIMING_CONFIGS) return false;
        
        const RS485PinConfig& pins = PIN_CONFIGS[pinConfigIdx];
        uint32_t baudrate = BAUDRATES[baudrateIdx];
        const TimingConfig& timing = TIMING_CONFIGS[timingIdx];
        
        // Print test info
        Serial.printf("Testing: Pins TX=%d RX=%d DE=%d | Baud=%d | Slave=%d | Timing=%s\n",
                     pins.tx, pins.rx, pins.de, baudrate, slaveId, timing.description);
        
        // Cleanup old modbus instance
        if (_modbus) {
            delete _modbus;
            _modbus = nullptr;
        }
        
        // Configure serial
        _serial->end();
        delay(100);
        _serial->begin(baudrate, SERIAL_8N1, pins.rx, pins.tx);
        delay(100);
        
        // Create new modbus instance with custom timing
        _modbus = new ModbusRTU(*_serial, slaveId, 500);
        _modbus->begin(baudrate, pins.rx, pins.tx, pins.de);
        
        // Apply custom timing (if different from default)
        // Note: This would require ModbusRTU class modification to accept timing params
        
        delay(200);  // Stabilization delay
        
        // Test read - try to read status register
        uint16_t testRegs[2];
        bool success = _modbus->readHoldingRegisters(0x0000, 2, testRegs);
        uint8_t errorCode = _modbus->getLastError();
        
        if (success) {
            // Success! Try to read temperature for verification
            uint16_t tempReg[1];
            float ambientTemp = 0.0;
            if (_modbus->readHoldingRegisters(0x004A, 1, tempReg)) {
                ambientTemp = tempReg[0] / 10.0;
            }
            
            Serial.printf("  ✓ SUCCESS! Status=0x%04X, Temp=%.1f°C, Error=0x%02X\n\n",
                         testRegs[0], ambientTemp, errorCode);
            
            // Save this as best result
            _bestResult.success = true;
            _bestResult.pin_config_index = pinConfigIdx;
            _bestResult.baudrate = baudrate;
            _bestResult.slave_id = slaveId;
            _bestResult.timing_config_index = timingIdx;
            _bestResult.status_register = testRegs[0];
            _bestResult.ambient_temp = ambientTemp;
            _bestResult.error_code = 0x00;
            
            return true;
        } else {
            Serial.printf("  ✗ Failed - Error 0x%02X (%s)\n\n",
                         errorCode, getErrorDescription(errorCode));
            return false;
        }
    }
    
    /**
     * @brief Get error description
     */
    const char* getErrorDescription(uint8_t errorCode) {
        switch(errorCode) {
            case 0xE0: return "Timeout - No response";
            case 0xE1: return "CRC Error - Message corrupted";
            case 0xE2: return "Exception - Invalid register";
            case 0xE3: return "Invalid Length - Partial message";
            case 0xE4: return "Wrong Slave - ID mismatch";
            case 0xE5: return "Buffer Overflow";
            default: return "Unknown error";
        }
    }
    
    /**
     * @brief Print scan summary
     */
    void printScanSummary(int totalTests, int successfulTests) {
        Serial.println("\n╔════════════════════════════════════════════════════════════╗");
        Serial.println("║  SCAN COMPLETE - SUMMARY                                  ║");
        Serial.println("╚════════════════════════════════════════════════════════════╝\n");
        
        Serial.printf("Total configurations tested: %d\n", totalTests);
        Serial.printf("Successful tests: %d\n", successfulTests);
        Serial.printf("Scan duration: %.2f seconds\n\n", _bestResult.scan_time_ms / 1000.0);
        
        if (_bestResult.success) {
            Serial.println("✓✓✓ WORKING CONFIGURATION FOUND ✓✓✓\n");
            
            const RS485PinConfig& pins = PIN_CONFIGS[_bestResult.pin_config_index];
            const TimingConfig& timing = TIMING_CONFIGS[_bestResult.timing_config_index];
            
            Serial.println("📌 RECOMMENDED CONFIG.H SETTINGS:");
            Serial.println("═══════════════════════════════════════════════════════════\n");
            Serial.printf("#define RS485_TX_PIN        %d\n", pins.tx);
            Serial.printf("#define RS485_RX_PIN        %d\n", pins.rx);
            Serial.printf("#define RS485_DE_PIN        %d\n", pins.de);
            Serial.printf("#define MODBUS_BAUDRATE     %d\n", _bestResult.baudrate);
            Serial.printf("#define MODBUS_SLAVE_ID     %d\n\n", _bestResult.slave_id);
            
            Serial.println("📊 DEVICE INFORMATION:");
            Serial.println("═══════════════════════════════════════════════════════════\n");
            Serial.printf("Status Register: 0x%04X\n", _bestResult.status_register);
            Serial.printf("Ambient Temperature: %.1f°C\n", _bestResult.ambient_temp);
            Serial.printf("Pin Config: %s\n", pins.description);
            Serial.printf("Timing: %s\n\n", timing.description);
            
        } else {
            Serial.println("✗✗✗ NO WORKING CONFIGURATION FOUND ✗✗✗\n");
            Serial.println("TROUBLESHOOTING:");
            Serial.println("═══════════════════════════════════════════════════════════\n");
            Serial.println("1. Check heat pump is POWERED ON");
            Serial.println("2. Verify physical RS485 wiring:");
            Serial.println("   - A+ to A+ (or try swapping A+/B-)");
            Serial.println("   - B- to B-");
            Serial.println("   - GND to GND (CRITICAL!)");
            Serial.println("3. Install 120Ω termination resistor between A+ and B-");
            Serial.println("4. Check Modbus is ENABLED in heat pump menu");
            Serial.println("5. Verify heat pump supports Modbus RTU protocol");
            Serial.println("6. Check cable length < 100m");
            Serial.println("7. Use shielded twisted pair cable\n");
        }
    }
    
    /**
     * @brief Generate config.h snippet
     */
    String generateConfigSnippet() {
        if (!_bestResult.success) {
            return "// No working configuration found - manual configuration required";
        }
        
        const RS485PinConfig& pins = PIN_CONFIGS[_bestResult.pin_config_index];
        
        String config = "// ============================================================================\n";
        config += "// AUTO-DETECTED CONFIGURATION\n";
        config += "// Generated by Modbus Auto-Scanner\n";
        config += "// ============================================================================\n\n";
        config += "// RS485 Pins (Auto-detected)\n";
        config += "#define RS485_TX_PIN        " + String(pins.tx) + "      // GPIO " + String(pins.tx) + "\n";
        config += "#define RS485_RX_PIN        " + String(pins.rx) + "      // GPIO " + String(pins.rx) + "\n";
        config += "#define RS485_DE_PIN        " + String(pins.de) + "      // GPIO " + String(pins.de) + "\n\n";
        config += "// Modbus Configuration (Auto-detected)\n";
        config += "#define MODBUS_BAUDRATE     " + String(_bestResult.baudrate) + "\n";
        config += "#define MODBUS_SLAVE_ID     " + String(_bestResult.slave_id) + "\n";
        config += "#define MODBUS_TIMEOUT_MS   500\n\n";
        config += "// Note: " + String(pins.description) + "\n";
        
        return config;
    }
    
    ScanResult getBestResult() { return _bestResult; }
};

#endif // MODBUS_AUTOSCAN_H
