// ============================================================================
// config_v5.h - VERSIUNE 5.0 DEFINITIVĂ
// M5Stack StampPLC K141 - Heat Pump Monitor
// ============================================================================
// CHANGELOG v5.0:
//   FIX: RS485_DE_PIN = GPIO 0 (pin oficial conform StamPLC.pdf)
//   FIX: Hardware RS485 half-duplex mode (fara digitalWrite manual)
//   NEW: Debug logging complet pe SD card
//   NEW: Log levels (ERROR, WARN, INFO, DEBUG, VERBOSE)
//   NEW: Pin test complet inclusiv GPIO 0
//   NEW: SD card logging cu timestamp si categorii
// ============================================================================

#pragma once

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FW_VERSION              "5.0.0"
#define FW_BUILD_DATE           __DATE__
#define FW_BUILD_TIME           __TIME__
#define FW_DESCRIPTION          "Hardware RS485 + SD Debug Logging"

// ============================================================================
// RS485 PINS - OFICIAL M5Stack StamPLC.pdf pagina 9
// ============================================================================
// PWR-485 Interface:
//   GPIO 42 → RS485_TX/BOOT  ← CORECT (neschimbat)
//   GPIO 43 → RS485_RX       ← CORECT (neschimbat)
//   GPIO 0  → RS485_DIR (DE/RE) ← FIX! Was: 2, 46, 1, 4, 5 (GREȘIT!)
// NOTE: GPIO 0 este OK după boot! Pull-up intern în hardware StamPLC.
// NOTE: Hardware UART_MODE_RS485_HALF_DUPLEX controlează GPIO 0 automat.
#define RS485_TX_PIN            42      // GPIO 42 - TX (CORECT)
#define RS485_RX_PIN            43      // GPIO 43 - RX (CORECT)
#define RS485_DE_PIN            0       // GPIO 0  - DIR/DE (FIX! Oficial!)
#define RS485_UART_NUM          UART_NUM_1

// ============================================================================
// MODBUS SETTINGS
// ============================================================================
#define MODBUS_BAUDRATE         9600
#define MODBUS_CONFIG           SERIAL_8N1
#define MODBUS_SLAVE_ID         0x01
#define MODBUS_TIMEOUT_MS       1000
#define MODBUS_RETRY_COUNT      3
#define MODBUS_INTER_BLOCK_MS   110   // Delay intre blocuri (>100ms cerut de protocol!)
#define MODBUS_RESPONSE_WAIT_MS 100   // Timp asteptare raspuns

// ============================================================================
// ELFIN PROTOCOL - 3 BLOCURI
// ============================================================================
#define BLOCK1_ADDR             0x0000  // System status
#define BLOCK1_COUNT            0x0029  // 41 registre
#define BLOCK2_ADDR             0x0040  // Temperatures
#define BLOCK2_COUNT            0x003E  // 62 registre
#define BLOCK3_ADDR             0x00F8  // Water temps
#define BLOCK3_COUNT            0x0008  // 8 registre

// ============================================================================
// SD CARD PINS - M5Stack StamPLC oficial
// ============================================================================
#define SD_MISO_PIN             9
#define SD_CS_PIN               10
#define SD_SCK_PIN              7
#define SD_MOSI_PIN             8
#define SD_SPI_FREQ             4000000 // 4MHz - stabil si sigur

// SD Log files
#define SD_LOG_FILE_DEBUG       "/debug_v5.log"   // Debug complet
#define SD_LOG_FILE_DATA        "/heatpump_v5.csv" // Date temperatura
#define SD_LOG_FILE_ERRORS      "/errors_v5.log"   // Erori critice
#define SD_LOG_FILE_BOOT        "/boot_v5.log"     // Boot/startup info
#define SD_LOG_MAX_SIZE_MB      10                  // Max size per file
#define SD_LOG_FLUSH_EVERY      5                   // Flush la N scrieri

// ============================================================================
// DEBUG LOGGING CONFIGURATION
// ============================================================================
#define LOG_LEVEL_NONE          0
#define LOG_LEVEL_ERROR         1
#define LOG_LEVEL_WARN          2
#define LOG_LEVEL_INFO          3
#define LOG_LEVEL_DEBUG         4
#define LOG_LEVEL_VERBOSE       5

// Set log level - schimba pentru debugging
#define CURRENT_LOG_LEVEL       LOG_LEVEL_DEBUG

// Where to log
#define LOG_TO_SERIAL           true
#define LOG_TO_SD               true
#define LOG_SERIAL_BAUD         115200

// RS485 raw hex dump in logs
#define LOG_RS485_TX_HEX        true    // Log fiecare TX packet
#define LOG_RS485_RX_HEX        true    // Log fiecare RX packet
#define LOG_TIMING              true    // Log timings (ms per operatie)

// ============================================================================
// PIN SCANNER v5 - Testeaza TOATE pinele inclusiv GPIO 0
// ============================================================================
// Candidati DE pin in ordinea prioritatii
// GPIO 0 = PRIMUL (pin oficial StamPLC!)
#define PIN_SCAN_CANDIDATES     {0, 46, 2, 1, 4, 5}
#define PIN_SCAN_COUNT          6
#define PIN_SCAN_TESTS_PER_PIN  5       // Teste per pin
#define PIN_SCAN_SUCCESS_RATE   60      // % minim pentru "PASS"
#define PIN_SCAN_TIMEOUT_MS     500     // Timeout per test

// ============================================================================
// WiFi SETTINGS
// ============================================================================
#define WIFI_SSID               "TP-LINK_FF30"
#define WIFI_PASSWORD           "22587732"
#define WIFI_HOSTNAME           "heatpump-v5"
#define WIFI_TIMEOUT_MS         15000

// ============================================================================
// NTP TIME
// ============================================================================
#define NTP_SERVER1             "pool.ntp.org"
#define NTP_SERVER2             "time.nist.gov"
#define NTP_TIMEZONE_OFFSET     7200    // UTC+2 Romania
#define NTP_DAYLIGHT_OFFSET     3600    // DST

// ============================================================================
// WEB SERVER
// ============================================================================
#define WEB_SERVER_PORT         80
#define API_UPDATE_INTERVAL_MS  2000

// ============================================================================
// OTA UPDATE
// ============================================================================
#define OTA_ENABLED             true
#define OTA_PASSWORD            "thermxpert2026"
#define OTA_PORT                3232
#define OTA_HOSTNAME            "heatpump-v5-ota"

// ============================================================================
// SYSTEM TIMINGS
// ============================================================================
#define READ_INTERVAL_MS        15000   // Citire pompa la 15 sec
#define CSV_LOG_INTERVAL_SEC    60      // Scriere CSV la 60 sec
#define WATCHDOG_TIMEOUT_SEC    30
#define STATS_PRINT_INTERVAL_MS 60000  // Print statistici la 1 minut

// ============================================================================
// BUTTON PINS - M5Stack StamPLC
// ============================================================================
#define BUTTON_A_PIN            39      // KEYA - Reset stats
#define BUTTON_B_PIN            40      // KEYB - Force read
#define BUTTON_C_PIN            41      // KEYC - Toggle debug level

// ============================================================================
// NVS STORAGE
// ============================================================================
#define NVS_NAMESPACE           "heatpump_v5"
#define NVS_KEY_DE_PIN          "de_pin"
#define NVS_KEY_BAUD            "baudrate"
#define NVS_MAGIC               0xDEAD0005  // v5 magic number

// ============================================================================
// HARDWARE VALIDATION
// ============================================================================
// La boot, verifica ca GPIO 0 e accesibil si nu e in boot mode
#define VALIDATE_GPIO0_ON_BOOT  true
#define GPIO0_SETTLE_MS         100     // Timp de stabilizare dupa boot

// ============================================================================
// DATA STRUCTURES
// ============================================================================
struct HeatPumpData {
    // Timestamp
    uint32_t timestamp;
    char     dateTime[32];

    // Block 1: System (41 registre)
    uint16_t systemStatus;
    uint16_t operatingMode;
    uint16_t faultCode;
    uint16_t runningHours;
    uint16_t block1Raw[41];

    // Block 2: Temperatures (62 registre)
    float    tempAmbient;
    float    tempEvapIn;
    float    tempEvapOut;
    float    tempCompDisch;
    float    pressHigh;
    float    pressLow;
    float    block2Temps[16];
    uint16_t block2Raw[62];

    // Block 3: Water (8 registre)
    float    tempWaterInlet;
    float    tempWaterOutlet;
    float    tempWaterTarget;
    uint16_t block3Raw[8];

    bool     valid;
    uint8_t  dataQuality;      // 0-100%
    uint32_t lastReadTime;
    uint32_t readDurationMs;
};

struct SystemStats {
    uint32_t uptime;
    uint32_t totalReads;
    uint32_t successfulReads;
    uint32_t failedReads;
    uint32_t crcErrors;
    uint32_t timeoutErrors;
    uint32_t echoDetected;     // NOU: numara echo-uri detectate
    float    successRate;
    float    avgReadTimeMs;
};

struct RS485Stats {
    uint32_t txCount;
    uint32_t rxCount;
    uint32_t rxEchoCount;      // NOU: echo detectat (TX = RX)
    uint32_t rxValidCount;
    uint32_t rxCrcErrors;
    uint32_t rxTimeouts;
    uint32_t hwModeEnabled;    // 1 = hardware mode activ
    uint8_t  dePin;            // Pin DE folosit
};

struct ConfigStorage {
    uint8_t  dePin;
    uint32_t baudrate;
    uint8_t  slaveId;
    uint16_t timeout;
    bool     hwModeActive;     // NOU: hardware mode flag
    uint32_t magicNumber;
};
