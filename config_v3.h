/**
 * @file config_v3.h
 * @brief Configuration for M5Stamp PLC K141 - Elfin EW11 Clone Mode
 * 
 * Version 3.0.0 - Elfin Protocol Replication
 * - Auto DE pin scanner (GPIO 46, 1, 4, 5, 2)
 * - Exact Elfin EW11 Modbus blocks
 * - Live hex dump & temperature graphs
 * - CSV export functionality
 * 
 * @version 3.0.0
 * @date 2026-03-07
 */

#ifndef CONFIG_V3_H
#define CONFIG_V3_H

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FIRMWARE_VERSION        "3.0.0"
#define BUILD_DATE              __DATE__
#define BUILD_TIME              __TIME__

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================
#define WIFI_SSID               "TP-LINK_FF30"
#define WIFI_PASSWORD           "22587732"
#define WIFI_HOSTNAME           "heatpump-elfin"
#define WIFI_TIMEOUT_MS         15000

// ============================================================================
// RS485 CONFIGURATION (Official M5Stack StamPLC K141 Pins)
// ============================================================================
#define RS485_TX_PIN            42      // GPIO 42 → PWR-485 TX (FIXED)
#define RS485_RX_PIN            43      // GPIO 43 → PWR-485 RX (FIXED)
#define RS485_DE_PIN_DEFAULT    46      // GPIO 46 → Default DE pin (will be auto-detected)

// MODBUS RTU Settings
#define MODBUS_BAUDRATE         9600    // Confirmed from Elfin EW11
#define MODBUS_SLAVE_ID         0x01    // Slave ID = 1 (confirmed)
#define MODBUS_TIMEOUT_MS       1000    // Response timeout
#define MODBUS_RETRY_COUNT      3       // Retry failed reads

// ============================================================================
// ELFIN PROTOCOL MODE
// ============================================================================
#define ELFIN_CLONE_MODE        true    // Use exact Elfin EW11 protocol
#define USE_3_BLOCK_READ        true    // Read in 3 blocks like Elfin
#define BLOCK_READ_DELAY_MS     50      // Delay between block reads

// ============================================================================
// AUTO PIN SCANNER
// ============================================================================
#define AUTO_SCAN_ON_BOOT       true    // Auto-scan DE pins on first boot
#define SAVE_WORKING_PIN        true    // Auto-save working pin to NVS
#define PIN_SCAN_TEST_COUNT     5       // Tests per pin (60% success = pass)

// ============================================================================
// SD CARD CONFIGURATION (Official Pins)
// ============================================================================
#define SD_MISO_PIN             9       // GPIO 9  (Official)
#define SD_CS_PIN               10      // GPIO 10 (Official)
#define SD_SCK_PIN              7       // GPIO 7  (Official)
#define SD_MOSI_PIN             8       // GPIO 8  (Official)
#define SD_ENABLED              true

// ============================================================================
// WEB SERVER CONFIGURATION
// ============================================================================
#define WEB_SERVER_PORT         80
#define WEB_UPDATE_INTERVAL_MS  2000    // Update interval for live data
#define ENABLE_HEX_DUMP         true    // Live Modbus hex dump
#define ENABLE_TEMP_GRAPH       true    // Real-time temperature graph
#define ENABLE_CSV_EXPORT       true    // CSV export functionality

// ============================================================================
// OTA CONFIGURATION
// ============================================================================
#define OTA_ENABLED             true
#define OTA_HOSTNAME            "heatpump-elfin"
#define OTA_PASSWORD            "thermxpert2026"
#define OTA_PORT                3232

// ============================================================================
// NTP CONFIGURATION
// ============================================================================
#define NTP_SERVER              "pool.ntp.org"
#define NTP_TIMEZONE_OFFSET     7200    // UTC+2 (Romania)
#define NTP_DAYLIGHT_OFFSET     3600    // DST offset

// ============================================================================
// LOGGING CONFIGURATION
// ============================================================================
#define LOG_TO_SERIAL           true
#define LOG_TO_SD               true
#define LOG_LEVEL_DEBUG         true
#define CSV_LOG_INTERVAL_SEC    60      // Log to CSV every 60 seconds

// ============================================================================
// TEMPERATURE DECODING
// ============================================================================
#define TEMP_SCALE_FACTOR       10.0f   // Divide raw value by 10
#define TEMP_MIN_VALID          -50.0f  // Minimum valid temperature
#define TEMP_MAX_VALID          150.0f  // Maximum valid temperature
#define TEMP_ERROR_VALUE        -999.0f // Error indicator

// ============================================================================
// SYSTEM MONITORING
// ============================================================================
#define READ_INTERVAL_MS        15000   // Read heat pump every 15 seconds
#define WATCHDOG_TIMEOUT_SEC    30      // Watchdog timeout
#define STATS_UPDATE_INTERVAL   10      // Update stats every 10 reads

// ============================================================================
// BUTTON PINS (Official M5Stack)
// ============================================================================
#define BUTTON_A_PIN            39      // KEYA
#define BUTTON_B_PIN            40      // KEYB
#define BUTTON_C_PIN            41      // KEYC

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
#define DEBUG_SERIAL            Serial
#define DEBUG_BAUD_RATE         115200
#define DEBUG_PRINT_RAW_PACKETS true    // Print raw Modbus packets
#define DEBUG_PRINT_DECODED     true    // Print decoded temperatures

// ============================================================================
// FEATURE FLAGS
// ============================================================================
#define FEATURE_WEB_UI          true
#define FEATURE_API             true
#define FEATURE_OTA             true
#define FEATURE_NTP             true
#define FEATURE_STATS           true
#define FEATURE_AUTO_SCAN       true

// ============================================================================
// DISPLAY CONFIGURATION (if LCD connected)
// ============================================================================
#define LCD_ENABLED             false   // Set true if using StamPLC LCD
#define LCD_MOSI_PIN            8
#define LCD_SCK_PIN             7
#define LCD_RS_PIN              6
#define LCD_CS_PIN              12
#define LCD_RST_PIN             3

// ============================================================================
// MACRO HELPERS
// ============================================================================
#define ARRAY_SIZE(x)           (sizeof(x) / sizeof((x)[0]))

#if DEBUG_SERIAL && LOG_LEVEL_DEBUG
    #define DEBUG_PRINTF(...)   DEBUG_SERIAL.printf(__VA_ARGS__)
    #define DEBUG_PRINTLN(x)    DEBUG_SERIAL.println(x)
#else
    #define DEBUG_PRINTF(...)
    #define DEBUG_PRINTLN(x)
#endif

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief Heat pump data structure (expanded for Elfin blocks)
 */
struct HeatPumpData {
    // Timestamps
    uint32_t timestamp;
    char dateTime[32];
    
    // Block 1: System Status (41 registers)
    uint16_t systemStatus;
    uint16_t operatingMode;
    uint32_t faultCode;
    uint16_t runningHours;
    
    // Block 2: Temperatures (from 62 registers)
    float tempAmbient;          // T1 - Outdoor ambient
    float tempEvaporatorIn;     // T2
    float tempEvaporatorOut;    // T3
    float tempCompressorDischarge; // T4
    float tempCondenserIn;      // T5
    float tempCondenserOut;     // T6
    float tempDefrost;          // T7
    float tempSuction;          // T8
    
    // Block 3: Water temperatures (8 registers)
    float tempWaterInlet;
    float tempWaterOutlet;
    float tempWaterTarget;
    
    // Pressures (from Block 2)
    float pressureHigh;         // Bar
    float pressureLow;          // Bar
    
    // Computed values
    float cop;                  // Coefficient of Performance
    float powerEstimated;       // kW (estimated)
    
    // Data quality
    bool valid;
    uint8_t dataQuality;        // 0-100%
    uint32_t lastReadTime;
};

/**
 * @brief System statistics
 */
struct SystemStats {
    uint32_t uptime;
    uint32_t totalReads;
    uint32_t successfulReads;
    uint32_t failedReads;
    uint32_t crcErrors;
    uint32_t timeoutErrors;
    float successRate;
    uint32_t lastResetTime;
};

/**
 * @brief Configuration storage (NVS)
 */
struct ConfigStorage {
    uint8_t dePin;              // Working DE pin
    uint32_t baudrate;
    uint8_t slaveId;
    uint16_t timeout;
    bool calibrated;
    uint32_t magicNumber;       // 0xDEADBEEF = valid config
};

#define CONFIG_MAGIC_NUMBER     0xDEADBEEF

#endif // CONFIG_V3_H
