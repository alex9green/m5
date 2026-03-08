// ============================================================================
// config_v7.h - VERSIUNE 7.0 - PINI CONFIRMATI DIN HARDWARE + LOG
// M5Stack StampPLC K141 (ESP32-S3) - Heat Pump Monitor
// ============================================================================
// CHANGELOG v7.0 - Ce stim SIGUR din teste reale:
//
//   [CONFIRMAT] SD card:
//     SCK=7, MOSI=8, MISO=9, CS=10  (K141-v1.0, log: "OK")
//
//   [CONFIRMAT] RS485 pini hardware (SIT3088 pe StamPLC):
//     TX=GPIO0   → DI  al SIT3088 (data in spre transceiver)
//     RX=GPIO39  → RO  al SIT3088 (data out spre ESP)
//     DE=GPIO46  → DE/RE al SIT3088 (direction enable)
//     Confirmat prin CRC_ERROR (59-86ms) = raspuns real de la pompa!
//
//   [CONFIRMAT] GPIO39 = RS485 RX = conflict cu BUTTON_A
//     SIT3088 RO tine GPIO39 LOW → BUTTON_A spam la infinite loop.
//     BUTTON_A dezactivat in v7. Butoane active: KEYB(40), KEYC(41).
//
//   [FIX] delay(50ms) era insuficient pentru 87 bytes la 9600 baud (~91ms)
//     → inlocuit cu inter-byte gap 20ms in modbus_v7.h si pin_test_v7.h
//
//   [NOU] Scan baud rate: 9600, 4800, 19200, 38400
//     Baud-ul corect al pompei e necunoscut → scanam la primul boot
//
//   [NOU] NVS salveaza: TX + RX + DE + baudrate (nu doar DE)
//
// STATUS POMPA: Semnalul RS485 EXISTA pe A/B (verificat cu osciloscop/USB).
//   Pompa genereaza semnal Modbus RTU. Dupa fix baud rate, citirea va merge.
// ============================================================================

#pragma once

// ============================================================================
// FIRMWARE VERSION
// ============================================================================
#define FW_VERSION              "7.0.0"
#define FW_BUILD_DATE           __DATE__
#define FW_BUILD_TIME           __TIME__
#define FW_DESCRIPTION          "Pini confirmati + Baud scan + Inter-byte gap fix"

// ============================================================================
// RS485 PINS - CONFIRMATI DIN HARDWARE (SIT3088 pe StamPLC K141)
// ============================================================================
//   SIT3088 pin → ESP32-S3 GPIO
//   DI  (data in)       → GPIO 0   = TX
//   RO  (data out)      → GPIO 39  = RX   ← si KEYA, dezactivat ca buton!
//   DE/RE (direction)   → GPIO 46  = DE
//
//   PWR-485 conector: doar A si B conectate la pompa. +5V nefolosit.
//   Hardware RS485 HALF-DUPLEX mode (setMode) functioneaza pe acest board.
#define RS485_TX_PIN            0       // GPIO 0  - DI al SIT3088
#define RS485_RX_PIN            39      // GPIO 39 - RO al SIT3088 (≠ BUTTON_A!)
#define RS485_DE_PIN            46      // GPIO 46 - DE/RE al SIT3088
#define RS485_UART_NUM          UART_NUM_1

// Perechi candidat pentru scan (TX=0/RX=39 PRIMA - e confirmata!)
#define RS485_UART_CANDIDATES   {{0,39,46},{42,43,0},{0,1,46}}
#define RS485_UART_PAIR_COUNT   3

// ============================================================================
// MODBUS / BAUD RATE
// ============================================================================
#define MODBUS_BAUDRATE         9600    // Default; scanul poate gasi alt baud
#define MODBUS_CONFIG           SERIAL_8N1
#define MODBUS_SLAVE_ID         0x01
#define MODBUS_TIMEOUT_MS       1000
#define MODBUS_RETRY_COUNT      2       // 2 retry-uri (3 total) per bloc
#define MODBUS_INTER_BLOCK_MS   150     // Pauza intre blocuri
// NOTA: NU mai avem MODBUS_RESPONSE_WAIT_MS fix - folosim inter-byte gap!

// Baud rate scan - ordinea e importanta (cel mai probabil primul)
#define BAUD_SCAN_CANDIDATES    {9600, 4800, 19200, 38400}
#define BAUD_SCAN_COUNT         4
#define BAUD_SCAN_TESTS         3       // Teste per baud rate

// ============================================================================
// ELFIN PROTOCOL - 3 BLOCURI MODBUS
// ============================================================================
#define BLOCK1_ADDR             0x0000  // System status
#define BLOCK1_COUNT            0x0029  // 41 registre → 87 bytes raspuns
#define BLOCK2_ADDR             0x0040  // Temperatures
#define BLOCK2_COUNT            0x003E  // 62 registre → 129 bytes raspuns
#define BLOCK3_ADDR             0x00F8  // Water temps
#define BLOCK3_COUNT            0x0008  // 8 registre  → 21 bytes raspuns

// ============================================================================
// SD CARD PINS - CONFIRMATE (K141-v1.0, log: "OK")
// ============================================================================
#define SD_SCK_PIN              7       // Confirmat din log (K141-v1.0)
#define SD_MOSI_PIN             8       // Confirmat din log
#define SD_MISO_PIN             9       // Comun tuturor variantelor StamPLC
#define SD_CS_PIN               10      // Comun tuturor variantelor StamPLC
#define SD_SPI_FREQ             4000000 // 4MHz - stabil pe K141

// SD Log files
#define SD_LOG_FILE_DEBUG       "/debug_v7.log"
#define SD_LOG_FILE_DATA        "/heatpump_v7.csv"
#define SD_LOG_FILE_ERRORS      "/errors_v7.log"
#define SD_LOG_FILE_BOOT        "/boot_v7.log"
#define SD_LOG_MAX_SIZE_MB      10
#define SD_LOG_FLUSH_EVERY      5

// ============================================================================
// DEBUG LOGGING
// ============================================================================
#define LOG_LEVEL_NONE          0
#define LOG_LEVEL_ERROR         1
#define LOG_LEVEL_WARN          2
#define LOG_LEVEL_INFO          3
#define LOG_LEVEL_DEBUG         4
#define LOG_LEVEL_VERBOSE       5

#define CURRENT_LOG_LEVEL       LOG_LEVEL_DEBUG
#define LOG_TO_SERIAL           true
#define LOG_TO_SD               true
#define LOG_SERIAL_BAUD         115200
#define LOG_RS485_TX_HEX        true
#define LOG_RS485_RX_HEX        true
#define LOG_TIMING              true

// ============================================================================
// PIN SCAN - DE pin candidati (TX/RX sunt confirmate, scanam doar DE daca e nevoie)
// ============================================================================
#define PIN_SCAN_CANDIDATES     {46, 0, 2, 1, 4, 5}  // 46 PRIMUL (confirmat!)
#define PIN_SCAN_COUNT          6
#define PIN_SCAN_TESTS_PER_PIN  3
#define PIN_SCAN_SUCCESS_RATE   60
#define PIN_SCAN_TIMEOUT_MS     500

// ============================================================================
// WiFi
// ============================================================================
#define WIFI_SSID               "TP-LINK_FF30"
#define WIFI_PASSWORD           "22587732"
#define WIFI_HOSTNAME           "heatpump-v7"
#define WIFI_TIMEOUT_MS         15000

// ============================================================================
// NTP
// ============================================================================
#define NTP_SERVER1             "pool.ntp.org"
#define NTP_SERVER2             "time.nist.gov"
#define NTP_TIMEZONE_OFFSET     7200    // UTC+2 Romania
#define NTP_DAYLIGHT_OFFSET     3600    // DST

// ============================================================================
// WEB SERVER
// ============================================================================
#define WEB_SERVER_PORT         80

// ============================================================================
// OTA
// ============================================================================
#define OTA_ENABLED             true
#define OTA_PASSWORD            "thermxpert2026"
#define OTA_PORT                3232
#define OTA_HOSTNAME            "heatpump-v7-ota"

// ============================================================================
// SYSTEM TIMINGS
// ============================================================================
#define READ_INTERVAL_MS        15000
#define CSV_LOG_INTERVAL_SEC    60
#define WATCHDOG_TIMEOUT_SEC    30
#define STATS_PRINT_INTERVAL_MS 60000

// ============================================================================
// BUTTON PINS
// ATENTIE: BUTTON_A (GPIO 39) = RS485 RX! Nu poate fi buton.
//          SIT3088 RO tine GPIO39 LOW permanent → spam KEYA.
//          BUTTON_A_PIN setat la 0xFF = dezactivat in cod.
// ============================================================================
#define BUTTON_A_PIN            0xFF    // DEZACTIVAT - GPIO39 = RS485 RX!
#define BUTTON_B_PIN            40      // KEYB - Citire imediata
#define BUTTON_C_PIN            41      // KEYC - Cicleaza log level

// ============================================================================
// NVS STORAGE - v7 namespace (separat de v5 pentru a evita date vechi)
// ============================================================================
#define NVS_NAMESPACE           "heatpump_v7"
#define NVS_KEY_DE_PIN          "de_pin"
#define NVS_KEY_BAUD            "baudrate"
#define NVS_MAGIC               0xDEAD0007  // v7 magic - diferit de v5!

// ============================================================================
// DATA STRUCTURES
// ============================================================================
struct HeatPumpData {
    uint32_t timestamp;
    char     dateTime[32];

    // Block 1: System
    uint16_t systemStatus;
    uint16_t operatingMode;
    uint16_t faultCode;
    uint16_t runningHours;
    uint16_t block1Raw[41];

    // Block 2: Temperatures
    float    tempAmbient;
    float    tempEvapIn;
    float    tempEvapOut;
    float    tempCompDisch;
    float    pressHigh;
    float    pressLow;
    float    block2Temps[16];
    uint16_t block2Raw[62];

    // Block 3: Water
    float    tempWaterInlet;
    float    tempWaterOutlet;
    float    tempWaterTarget;
    uint16_t block3Raw[8];

    bool     valid;
    uint8_t  dataQuality;
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
    uint32_t echoDetected;
    float    successRate;
    float    avgReadTimeMs;
};

struct RS485Stats {
    uint32_t txCount;
    uint32_t rxCount;
    uint32_t rxEchoCount;
    uint32_t rxValidCount;
    uint32_t rxCrcErrors;
    uint32_t rxTimeouts;
    uint32_t hwModeEnabled;
    uint8_t  dePin;
    uint8_t  txPin;
    uint8_t  rxPin;
    uint32_t baudrate;
};
