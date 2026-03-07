/**
 * @file elfin_protocol.h
 * @brief Exact replication of Elfin EW11 Modbus RTU protocol for SolarEast Heat Pump
 * 
 * Based on captured packets from Elfin EW11:
 * - Block 1: 01 03 00 00 00 29 84 14 → System Status & Faults (41 registers)
 * - Block 2: 01 03 00 40 00 3E C5 CE → Temperatures & Pressures (62 registers)
 * - Block 3: 01 03 00 F8 00 08 C5 FD → Water Inlet/Outlet (8 registers)
 * 
 * @version 1.0.0
 * @date 2026-03-07
 */

#ifndef ELFIN_PROTOCOL_H
#define ELFIN_PROTOCOL_H

#include <Arduino.h>

// ============================================================================
// ELFIN EW11 PROTOCOL BLOCKS
// ============================================================================

struct ElfinBlock {
    uint16_t startAddr;     // Starting register address
    uint16_t count;         // Number of registers to read
    const char* name;       // Block name
    const char* description;// Detailed description
    uint8_t expectedBytes;  // Expected response bytes (count * 2)
};

// Exact blocks from Elfin EW11 capture
const ElfinBlock ELFIN_BLOCKS[] = {
    // Block 1: System Status & Faults
    // Request: 01 03 00 00 00 29 84 14
    // Response: 82 bytes (41 registers * 2)
    {
        .startAddr = 0x0000,
        .count = 0x0029,  // 41 registers
        .name = "SYSTEM",
        .description = "System Status, Faults, Operating Mode",
        .expectedBytes = 82
    },
    
    // Block 2: Temperatures & Pressures
    // Request: 01 03 00 40 00 3E C5 CE
    // Response: 124 bytes (62 registers * 2)
    {
        .startAddr = 0x0040,
        .count = 0x003E,  // 62 registers
        .name = "TEMPS",
        .description = "T1-T8 Temperatures, Pressures, Sensors",
        .expectedBytes = 124
    },
    
    // Block 3: Water Inlet/Outlet Temperatures
    // Request: 01 03 00 F8 00 08 C5 FD
    // Response: 16 bytes (8 registers * 2)
    {
        .startAddr = 0x00F8,
        .count = 0x0008,  // 8 registers
        .name = "WATER",
        .description = "Water Inlet/Outlet Temperatures",
        .expectedBytes = 16
    }
};

#define ELFIN_BLOCK_COUNT 3

// ============================================================================
// TEMPERATURE DECODING
// ============================================================================

/**
 * @brief Decode temperature from Modbus register
 * @param raw Raw 16-bit value from Modbus register
 * @return Temperature in °C (raw / 10.0)
 * 
 * Example: 0x006E (110 decimal) → 11.0°C
 */
inline float decodeTemperature(uint16_t raw) {
    // Handle negative temperatures (signed 16-bit)
    int16_t signedRaw = (int16_t)raw;
    return signedRaw / 10.0f;
}

/**
 * @brief Check if temperature value is valid
 * @param temp Temperature value
 * @return true if valid, false if error/missing
 * 
 * 0xFFD8 (-40 decimal / 10 = -4.0°C) often means "sensor not connected"
 */
inline bool isValidTemperature(float temp) {
    return (temp > -50.0f && temp < 150.0f);
}

// ============================================================================
// KNOWN TEMPERATURE REGISTER OFFSETS (within Block 2, 0x0040 base)
// ============================================================================

// Example from Elfin log: byte position in Block 2 response
// Offset 0x002E (46 bytes into response) = 0x006E = 11.0°C

struct TempRegister {
    uint8_t blockIndex;     // Which block (0=SYSTEM, 1=TEMPS, 2=WATER)
    uint16_t offsetInBlock; // Register offset within block
    const char* name;       // Temperature name
};

// Known temperature registers (expand as you identify more)
const TempRegister KNOWN_TEMPS[] = {
    {1, 0x0E, "T1_AMBIENT"},      // Ambient outdoor temp (seen in log: 0x006E = 11.0°C)
    {1, 0x00, "TEMP_1"},          // First temp in Block 2
    {1, 0x01, "TEMP_2"},
    {1, 0x02, "TEMP_3"},
    {1, 0x03, "TEMP_4"},
    {1, 0x04, "TEMP_5"},
    {1, 0x05, "TEMP_6"},
    {1, 0x06, "TEMP_7"},
    {1, 0x07, "TEMP_8"},
    {2, 0x00, "WATER_INLET"},     // Water inlet temp
    {2, 0x01, "WATER_OUTLET"},    // Water outlet temp
};

#define KNOWN_TEMP_COUNT 11

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Get human-readable block name
 */
inline const char* getBlockName(uint8_t blockIndex) {
    if (blockIndex < ELFIN_BLOCK_COUNT) {
        return ELFIN_BLOCKS[blockIndex].name;
    }
    return "UNKNOWN";
}

/**
 * @brief Calculate expected response size for a block
 * @param blockIndex Block index (0-2)
 * @return Total bytes expected: 3 (header) + data + 2 (CRC)
 */
inline uint16_t getExpectedResponseSize(uint8_t blockIndex) {
    if (blockIndex < ELFIN_BLOCK_COUNT) {
        // Modbus response: [SlaveID][Function][ByteCount][Data...][CRC16]
        // ByteCount = count * 2
        return 3 + ELFIN_BLOCKS[blockIndex].expectedBytes + 2;
    }
    return 0;
}

/**
 * @brief Format Modbus request as hex string for debugging
 */
inline String formatModbusRequest(uint8_t slaveId, uint16_t startAddr, uint16_t count) {
    char buf[64];
    snprintf(buf, sizeof(buf), "01 03 %02X %02X %02X %02X [CRC]", 
             (startAddr >> 8) & 0xFF, startAddr & 0xFF,
             (count >> 8) & 0xFF, count & 0xFF);
    return String(buf);
}

/**
 * @brief Format raw bytes as hex string
 */
inline String bytesToHex(const uint8_t* data, size_t len) {
    String result = "";
    for (size_t i = 0; i < len; i++) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        result += buf;
        if ((i + 1) % 16 == 0 && i < len - 1) {
            result += "\n";
        }
    }
    return result;
}

#endif // ELFIN_PROTOCOL_H
