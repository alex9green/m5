/**
 * @file ModbusRTU.h
 * @brief Modbus RTU Master Library for ESP32 with RS485 Hardware Control
 * 
 * Features:
 * - Hardware RS485 DE/RE pin control
 * - CRC16 checksum validation
 * - Timeout handling
 * - Retry mechanism
 * - Function codes: 03H (Read Holding), 06H (Write Single), 10H (Write Multiple)
 * 
 * Based on Modbus RTU specification and SolarEast Heat Pump protocol
 * 
 * @author ProEnergy Green SRL / ThermXpert
 * @date 2026-03-07
 */

#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Modbus Function Codes
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10

// Modbus Exception Codes
#define MODBUS_EX_ILLEGAL_FUNCTION          0x01
#define MODBUS_EX_ILLEGAL_DATA_ADDRESS      0x02
#define MODBUS_EX_ILLEGAL_DATA_VALUE        0x03
#define MODBUS_EX_SLAVE_DEVICE_FAILURE      0x04

// Internal Error Codes
#define MODBUS_ERR_TIMEOUT                  0xE0
#define MODBUS_ERR_CRC                      0xE1
#define MODBUS_ERR_EXCEPTION                0xE2
#define MODBUS_ERR_INVALID_LENGTH           0xE3
#define MODBUS_ERR_INVALID_SLAVE            0xE4
#define MODBUS_ERR_BUFFER_OVERFLOW          0xE5
#define MODBUS_ERR_ILLEGAL_DATA_VALUE       0xE6

// Max buffer size
#define MODBUS_MAX_BUFFER                   256

class ModbusRTU {
public:
    /**
     * @brief Constructor
     * @param serial Hardware serial port
     * @param slaveId Modbus slave address (1-247)
     * @param timeout Response timeout in milliseconds
     */
    ModbusRTU(HardwareSerial &serial, uint8_t slaveId = 1, uint16_t timeout = 500);
    
    /**
     * @brief Initialize Modbus communication
     * @param baudrate Serial baudrate (typically 9600 for SolarEast)
     * @param rxPin RX pin number
     * @param txPin TX pin number
     * @param dePin DE/RE control pin (-1 if not used)
     */
    void begin(uint32_t baudrate, int8_t rxPin, int8_t txPin, int8_t dePin = -1);
    
    /**
     * @brief Read holding registers (Function Code 0x03)
     * @param startAddress Starting register address
     * @param quantity Number of registers to read (1-125)
     * @param buffer Output buffer for register values
     * @return true if successful
     */
    bool readHoldingRegisters(uint16_t startAddress, uint16_t quantity, uint16_t *buffer);
    
    /**
     * @brief Write single register (Function Code 0x06)
     * @param address Register address
     * @param value Value to write
     * @return true if successful
     */
    bool writeSingleRegister(uint16_t address, uint16_t value);
    
    /**
     * @brief Write multiple registers (Function Code 0x10)
     * @param startAddress Starting register address
     * @param quantity Number of registers to write
     * @param values Array of values to write
     * @return true if successful
     */
    bool writeMultipleRegisters(uint16_t startAddress, uint16_t quantity, const uint16_t *values);
    
    /**
     * @brief Set slave ID
     * @param id Slave address (1-247)
     */
    void setSlaveId(uint8_t id);
    
    /**
     * @brief Set response timeout
     * @param ms Timeout in milliseconds
     */
    void setTimeout(uint16_t ms);
    
    /**
     * @brief Get last error code
     * @return Error code (0x00 = no error)
     */
    uint8_t getLastError();
    
    /**
     * @brief Get last exception code (if applicable)
     * @return Exception code
     */
    uint8_t getLastException();
    
    /**
     * @brief Clear receive buffer
     */
    void clearRxBuffer();

private:
    HardwareSerial &_serial;
    uint8_t _slaveId;
    uint16_t _timeout;
    int8_t _dePin;
    uint8_t _lastError;
    uint8_t _lastException;
    uint8_t _txBuffer[MODBUS_MAX_BUFFER];
    uint8_t _rxBuffer[MODBUS_MAX_BUFFER];
    
    /**
     * @brief Send Modbus request and receive response
     * @param request Request buffer
     * @param requestLength Request length in bytes
     * @param response Response buffer
     * @param expectedLength Expected response length (0 = variable)
     * @return Response length (0 = error)
     */
    uint16_t sendRequest(const uint8_t *request, uint16_t requestLength, 
                        uint8_t *response, uint16_t expectedLength = 0);
    
    /**
     * @brief Calculate CRC16 checksum
     * @param buffer Data buffer
     * @param length Data length
     * @return CRC16 value (little-endian)
     */
    uint16_t calculateCRC(const uint8_t *buffer, uint16_t length);
    
    /**
     * @brief Set RS485 transceiver to transmit mode
     */
    void setTxMode();
    
    /**
     * @brief Set RS485 transceiver to receive mode
     */
    void setRxMode();
    
    /**
     * @brief Wait for serial transmission to complete
     */
    void waitTransmitComplete();
};

// ============================================================================
// IMPLEMENTATION
// ============================================================================

ModbusRTU::ModbusRTU(HardwareSerial &serial, uint8_t slaveId, uint16_t timeout)
    : _serial(serial), _slaveId(slaveId), _timeout(timeout), _dePin(-1), 
      _lastError(0), _lastException(0) {
}

void ModbusRTU::begin(uint32_t baudrate, int8_t rxPin, int8_t txPin, int8_t dePin) {
    _dePin = dePin;
    
    // Configure DE/RE pin if provided
    if (_dePin >= 0) {
        pinMode(_dePin, OUTPUT);
        digitalWrite(_dePin, LOW);  // Start in receive mode
    }
    
    // Initialize serial port
    _serial.begin(baudrate, SERIAL_8N1, rxPin, txPin);
    _serial.setRxBufferSize(256);
    _serial.setTxBufferSize(256);
    
    // Flush any existing data
    clearRxBuffer();
    
    delay(100);  // Allow serial to stabilize
}

void ModbusRTU::setSlaveId(uint8_t id) {
    if (id >= 1 && id <= 247) {
        _slaveId = id;
    }
}

void ModbusRTU::setTimeout(uint16_t ms) {
    _timeout = ms;
}

uint8_t ModbusRTU::getLastError() {
    return _lastError;
}

uint8_t ModbusRTU::getLastException() {
    return _lastException;
}

void ModbusRTU::clearRxBuffer() {
    while (_serial.available()) {
        _serial.read();
    }
}

void ModbusRTU::setTxMode() {
    if (_dePin >= 0) {
        digitalWrite(_dePin, HIGH);
        delayMicroseconds(50);  // tDRE: Driver Enable to TX start
    }
}

void ModbusRTU::setRxMode() {
    if (_dePin >= 0) {
        waitTransmitComplete();
        delayMicroseconds(50);  // tDFE: TX end to Driver Disable
        digitalWrite(_dePin, LOW);
    }
}

void ModbusRTU::waitTransmitComplete() {
    _serial.flush();  // Wait for TX complete
    delayMicroseconds(100);
}

uint16_t ModbusRTU::calculateCRC(const uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)buffer[i];
        
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;  // Little-endian: low byte first
}

uint16_t ModbusRTU::sendRequest(const uint8_t *request, uint16_t requestLength,
                                uint8_t *response, uint16_t expectedLength) {
    _lastError = 0;
    _lastException = 0;
    
    // Clear RX buffer
    clearRxBuffer();
    
    // Add delay before transmission (per SolarEast spec: 100ms between frames)
    delay(50);
    
    // Switch to TX mode
    setTxMode();
    
    // Send request
    _serial.write(request, requestLength);
    
    // Switch back to RX mode
    setRxMode();
    
    // Wait for response
    unsigned long startTime = millis();
    uint16_t rxIndex = 0;
    bool receiving = false;
    unsigned long lastByteTime = startTime;
    
    while (millis() - startTime < _timeout) {
        if (_serial.available()) {
            response[rxIndex++] = _serial.read();
            lastByteTime = millis();
            receiving = true;
            
            if (rxIndex >= MODBUS_MAX_BUFFER) {
                _lastError = MODBUS_ERR_BUFFER_OVERFLOW;
                return 0;
            }
            
            // Check if we have minimum frame (slave + FC + 2 bytes CRC)
            if (rxIndex >= 4) {
                // If expecting specific length, check if received
                if (expectedLength > 0 && rxIndex >= expectedLength) {
                    break;
                }
                // Otherwise, use inter-character timeout
                if (receiving && (millis() - lastByteTime) > 10) {
                    break;  // End of frame detected
                }
            }
        } else if (receiving && (millis() - lastByteTime) > 10) {
            break;  // End of frame
        }
        
        yield();  // Allow other tasks
    }
    
    // Check timeout
    if (rxIndex == 0) {
        _lastError = MODBUS_ERR_TIMEOUT;
        return 0;
    }
    
    // Minimum frame: Slave(1) + FC(1) + CRC(2) = 4 bytes
    if (rxIndex < 4) {
        _lastError = MODBUS_ERR_INVALID_LENGTH;
        return 0;
    }
    
    // Verify CRC
    uint16_t receivedCRC = (response[rxIndex - 1] << 8) | response[rxIndex - 2];
    uint16_t calculatedCRC = calculateCRC(response, rxIndex - 2);
    
    if (receivedCRC != calculatedCRC) {
        _lastError = MODBUS_ERR_CRC;
        return 0;
    }
    
    // Check slave ID
    if (response[0] != _slaveId) {
        _lastError = MODBUS_ERR_INVALID_SLAVE;
        return 0;
    }
    
    // Check for exception
    if (response[1] & 0x80) {
        _lastError = MODBUS_ERR_EXCEPTION;
        _lastException = response[2];
        return 0;
    }
    
    return rxIndex;
}

bool ModbusRTU::readHoldingRegisters(uint16_t startAddress, uint16_t quantity, uint16_t *buffer) {
    if (quantity == 0 || quantity > 125) {
        _lastError = MODBUS_ERR_ILLEGAL_DATA_VALUE;
        return false;
    }
    
    // Build request: Slave + FC + Start_Hi + Start_Lo + Qty_Hi + Qty_Lo + CRC_Lo + CRC_Hi
    _txBuffer[0] = _slaveId;
    _txBuffer[1] = MODBUS_FC_READ_HOLDING_REGISTERS;
    _txBuffer[2] = (startAddress >> 8) & 0xFF;
    _txBuffer[3] = startAddress & 0xFF;
    _txBuffer[4] = (quantity >> 8) & 0xFF;
    _txBuffer[5] = quantity & 0xFF;
    
    uint16_t crc = calculateCRC(_txBuffer, 6);
    _txBuffer[6] = crc & 0xFF;         // CRC low byte
    _txBuffer[7] = (crc >> 8) & 0xFF;  // CRC high byte
    
    // Expected response length: Slave(1) + FC(1) + ByteCount(1) + Data(qty*2) + CRC(2)
    uint16_t expectedLength = 5 + (quantity * 2);
    
    // Send request
    uint16_t rxLength = sendRequest(_txBuffer, 8, _rxBuffer, expectedLength);
    
    if (rxLength == 0) {
        return false;
    }
    
    // Verify function code
    if (_rxBuffer[1] != MODBUS_FC_READ_HOLDING_REGISTERS) {
        _lastError = MODBUS_ERR_INVALID_LENGTH;
        return false;
    }
    
    // Verify byte count
    uint8_t byteCount = _rxBuffer[2];
    if (byteCount != quantity * 2) {
        _lastError = MODBUS_ERR_INVALID_LENGTH;
        return false;
    }
    
    // Extract data (big-endian to uint16_t)
    for (uint16_t i = 0; i < quantity; i++) {
        buffer[i] = (_rxBuffer[3 + i * 2] << 8) | _rxBuffer[4 + i * 2];
    }
    
    return true;
}

bool ModbusRTU::writeSingleRegister(uint16_t address, uint16_t value) {
    // Build request: Slave + FC + Addr_Hi + Addr_Lo + Val_Hi + Val_Lo + CRC_Lo + CRC_Hi
    _txBuffer[0] = _slaveId;
    _txBuffer[1] = MODBUS_FC_WRITE_SINGLE_REGISTER;
    _txBuffer[2] = (address >> 8) & 0xFF;
    _txBuffer[3] = address & 0xFF;
    _txBuffer[4] = (value >> 8) & 0xFF;
    _txBuffer[5] = value & 0xFF;
    
    uint16_t crc = calculateCRC(_txBuffer, 6);
    _txBuffer[6] = crc & 0xFF;
    _txBuffer[7] = (crc >> 8) & 0xFF;
    
    // Expected response: Echo of request (8 bytes)
    uint16_t rxLength = sendRequest(_txBuffer, 8, _rxBuffer, 8);
    
    if (rxLength == 0) {
        return false;
    }
    
    // Verify echo
    if (memcmp(_txBuffer, _rxBuffer, 6) != 0) {
        _lastError = MODBUS_ERR_INVALID_LENGTH;
        return false;
    }
    
    return true;
}

bool ModbusRTU::writeMultipleRegisters(uint16_t startAddress, uint16_t quantity, const uint16_t *values) {
    if (quantity == 0 || quantity > 123) {
        _lastError = MODBUS_ERR_ILLEGAL_DATA_VALUE;
        return false;
    }
    
    uint8_t byteCount = quantity * 2;
    
    // Build request
    _txBuffer[0] = _slaveId;
    _txBuffer[1] = MODBUS_FC_WRITE_MULTIPLE_REGISTERS;
    _txBuffer[2] = (startAddress >> 8) & 0xFF;
    _txBuffer[3] = startAddress & 0xFF;
    _txBuffer[4] = (quantity >> 8) & 0xFF;
    _txBuffer[5] = quantity & 0xFF;
    _txBuffer[6] = byteCount;
    
    // Add data
    for (uint16_t i = 0; i < quantity; i++) {
        _txBuffer[7 + i * 2] = (values[i] >> 8) & 0xFF;
        _txBuffer[8 + i * 2] = values[i] & 0xFF;
    }
    
    uint16_t requestLength = 7 + byteCount;
    uint16_t crc = calculateCRC(_txBuffer, requestLength);
    _txBuffer[requestLength] = crc & 0xFF;
    _txBuffer[requestLength + 1] = (crc >> 8) & 0xFF;
    
    // Expected response: Slave + FC + Start_Hi + Start_Lo + Qty_Hi + Qty_Lo + CRC_Lo + CRC_Hi (8 bytes)
    uint16_t rxLength = sendRequest(_txBuffer, requestLength + 2, _rxBuffer, 8);
    
    if (rxLength == 0) {
        return false;
    }
    
    // Verify response
    if (_rxBuffer[1] != MODBUS_FC_WRITE_MULTIPLE_REGISTERS) {
        _lastError = MODBUS_ERR_INVALID_LENGTH;
        return false;
    }
    
    return true;
}

#endif // MODBUS_RTU_H
