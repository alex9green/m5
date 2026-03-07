/**
 * @file rs485_pin_tester.cpp
 * @brief M5Stamp PLC K141 - RS485 GPIO Pin Compatibility Tester
 *
 * Tests ALL compatible GPIO pins by generating a 1kHz square wave.
 * Use an oscilloscope to verify which pins output a clean signal.
 *
 * USAGE:
 *   1. Upload to M5Stack Stamp S3 via Arduino IDE
 *   2. Open Serial Monitor @ 115200 baud
 *   3. Type 's' + Enter to start test
 *   4. For each pin: connect oscilloscope probe
 *      - See 1kHz square wave? → type 'g' (good)
 *      - No signal?            → type 'b' (bad)
 *   5. Results shown at end
 *
 * @version 1.0.0
 * @date 2026-03-07
 */

#include <Arduino.h>

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

struct PinEntry {
    uint8_t  gpio;
    const char* name;
    const char* function;
    uint8_t  priority;   // 1=Critical, 2=High, 3=Medium, 4=Reserved
    const char* location;
};

// ALL testable pins on M5Stamp PLC K141 / Stamp S3
static const PinEntry PIN_LIST[] = {
    // Priority 1 - CRITICAL (RS485 key pins)
    { 0,  "GPIO0",  "DE - Driver Enable",   1, "Main board" },
    { 39, "GPIO39", "RE - Receive Enable",  1, "Main board" },
    { 42, "GPIO42", "TX - Transmit",        1, "Main board" },
    { 43, "GPIO43", "RX - Receive",         1, "Main board" },
    { 46, "GPIO46", "DE Backup",            1, "Main board (strapping)" },

    // Priority 2 - HIGH (Alternative RS485 pins)
    { 1,  "GPIO1",  "General Purpose",      2, "Main board" },
    { 2,  "GPIO2",  "General Purpose",      2, "Main board" },
    { 3,  "GPIO3",  "General Purpose",      2, "Main board" },
    { 4,  "GPIO4",  "General Purpose",      2, "Main board" },
    { 5,  "GPIO5",  "General Purpose",      2, "Main board" },
    { 15, "GPIO15", "General Purpose",      2, "Main board" },
    { 16, "GPIO16", "General Purpose",      2, "Main board" },
    { 17, "GPIO17", "General Purpose",      2, "Main board" },
    { 18, "GPIO18", "General Purpose",      2, "Main board" },
    { 21, "GPIO21", "Grove Port A SDA",     2, "Grove A" },
    { 22, "GPIO22", "Grove Port A SCL",     2, "Grove A" },
    { 23, "GPIO23", "General Purpose",      2, "Expansion" },
    { 24, "GPIO24", "General Purpose",      2, "Expansion" },
    { 25, "GPIO25", "General Purpose",      2, "Expansion" },
    { 26, "GPIO26", "General Purpose",      2, "Expansion" },

    // Priority 3 - MEDIUM (Grove B / Expansion)
    { 8,  "GPIO8",  "Grove Port B",         3, "Grove B" },
    { 9,  "GPIO9",  "Grove Port B / SD_MISO",3,"Grove B / SD" },
    { 10, "GPIO10", "SD_CS",                3, "SD Card" },
    { 11, "GPIO11", "Grove Port B",         3, "Grove B" },
    { 29, "GPIO29", "Expansion",            3, "Expansion" },
    { 30, "GPIO30", "Expansion",            3, "Expansion" },
    { 31, "GPIO31", "Expansion",            3, "Expansion" },
    { 32, "GPIO32", "Expansion",            3, "Expansion" },
    { 33, "GPIO33", "Expansion",            3, "Expansion" },
    { 34, "GPIO34", "Expansion (input only)",3,"Expansion" },
    { 35, "GPIO35", "Expansion (input only)",3,"Expansion" },
    { 36, "GPIO36", "Expansion (input only)",3,"Expansion" },
    { 37, "GPIO37", "Expansion",            3, "Expansion" },
    { 38, "GPIO38", "Expansion",            3, "Expansion" },
    { 40, "GPIO40", "KEYB Button",          3, "Main board" },
    { 41, "GPIO41", "KEYC Button",          3, "Main board" },
    { 44, "GPIO44", "UART0 TX",             3, "Debug UART" },
    { 45, "GPIO45", "Expansion",            3, "Expansion" },
};

#define PIN_COUNT  (sizeof(PIN_LIST) / sizeof(PIN_LIST[0]))

// ============================================================================
// RESULT STORAGE
// ============================================================================

enum PinResult { UNTESTED, GOOD, BAD, SKIPPED };

PinResult results[PIN_COUNT];

// ============================================================================
// GLOBALS
// ============================================================================

bool testRunning = false;
uint8_t currentPinIndex = 0;
uint32_t pinStartTime = 0;
#define PIN_TEST_DURATION_MS  5000   // 5 seconds per pin
#define WAVE_HALF_PERIOD_US   500    // 1kHz = 500us high, 500us low

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
void printHeader();
void printPinInfo(uint8_t idx);
void printProgress(uint32_t elapsed);
void printResults();
void printSeparator();
void waitForInput();

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    // Init all results
    for (uint8_t i = 0; i < PIN_COUNT; i++) {
        results[i] = UNTESTED;
    }

    printHeader();
    Serial.println("Type 's' + Enter to START the pin test.");
    Serial.println("Type 'r' + Enter to see RESULTS at any time.");
}

// ============================================================================
// MAIN LOOP
// ============================================================================
void loop() {
    // Check serial input
    if (Serial.available()) {
        char c = Serial.read();
        while (Serial.available()) Serial.read();  // flush

        if (!testRunning && (c == 's' || c == 'S')) {
            testRunning = true;
            currentPinIndex = 0;
            pinStartTime = millis();
            Serial.println("\n\n╔══════════════════════════════════════════════════════╗");
            Serial.println("║  RS485 GPIO PIN TESTER - Starting...                ║");
            Serial.println("╚══════════════════════════════════════════════════════╝\n");
            delay(500);
        }

        if (testRunning) {
            if (c == 'g' || c == 'G') {
                results[currentPinIndex] = GOOD;
                Serial.printf("  → Marked GPIO%d as ✅ GOOD\n\n", PIN_LIST[currentPinIndex].gpio);
                currentPinIndex++;
                if (currentPinIndex >= PIN_COUNT) {
                    testRunning = false;
                    printResults();
                } else {
                    pinStartTime = millis();
                    printPinInfo(currentPinIndex);
                }
            } else if (c == 'b' || c == 'B') {
                results[currentPinIndex] = BAD;
                Serial.printf("  → Marked GPIO%d as ❌ BAD\n\n", PIN_LIST[currentPinIndex].gpio);
                currentPinIndex++;
                if (currentPinIndex >= PIN_COUNT) {
                    testRunning = false;
                    printResults();
                } else {
                    pinStartTime = millis();
                    printPinInfo(currentPinIndex);
                }
            } else if (c == 'n' || c == 'N') {
                results[currentPinIndex] = SKIPPED;
                Serial.printf("  → Skipped GPIO%d\n\n", PIN_LIST[currentPinIndex].gpio);
                currentPinIndex++;
                if (currentPinIndex >= PIN_COUNT) {
                    testRunning = false;
                    printResults();
                } else {
                    pinStartTime = millis();
                    printPinInfo(currentPinIndex);
                }
            }
        }

        if (c == 'r' || c == 'R') {
            printResults();
        }
    }

    if (testRunning && currentPinIndex < PIN_COUNT) {
        const PinEntry& pin = PIN_LIST[currentPinIndex];
        uint32_t elapsed = millis() - pinStartTime;

        // Print pin info once at start
        static uint8_t lastPrinted = 255;
        if (lastPrinted != currentPinIndex) {
            lastPrinted = currentPinIndex;
            printPinInfo(currentPinIndex);
        }

        // Generate 1kHz square wave on current pin
        if (pin.priority != 4) {
            pinMode(pin.gpio, OUTPUT);
            digitalWrite(pin.gpio, HIGH);
            delayMicroseconds(WAVE_HALF_PERIOD_US);
            digitalWrite(pin.gpio, LOW);
            delayMicroseconds(WAVE_HALF_PERIOD_US);
        }

        // Print progress every second
        static uint32_t lastProgressPrint = 0;
        if (millis() - lastProgressPrint >= 1000) {
            lastProgressPrint = millis();
            printProgress(elapsed);
        }

        // Auto-advance after timeout
        if (elapsed >= PIN_TEST_DURATION_MS) {
            if (results[currentPinIndex] == UNTESTED) {
                results[currentPinIndex] = SKIPPED;
                Serial.println("  → Timeout, auto-skipped\n");
            }
            // Reset pin to input
            pinMode(pin.gpio, INPUT);
            currentPinIndex++;
            if (currentPinIndex >= PIN_COUNT) {
                testRunning = false;
                printResults();
            } else {
                pinStartTime = millis();
            }
        }
    }
}

// ============================================================================
// PRINT FUNCTIONS
// ============================================================================

void printHeader() {
    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║  M5Stamp PLC K141 - RS485 GPIO PIN COMPATIBILITY TESTER    ║");
    Serial.println("║  Version 1.0.0  │  2026-03-07                              ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");
    Serial.println("");
    Serial.printf("  Total pins to test: %d\n", PIN_COUNT);
    Serial.println("  Signal: 1kHz square wave (0V → 3.3V)");
    Serial.println("  Duration: 5 seconds per pin");
    Serial.println("");
    Serial.println("  Commands during test:");
    Serial.println("    g = GPIO is GOOD (signal visible on oscilloscope)");
    Serial.println("    b = GPIO is BAD  (no signal)");
    Serial.println("    n = SKIP this pin");
    Serial.println("    r = Show results so far");
    Serial.println("");
    printSeparator();
}

void printPinInfo(uint8_t idx) {
    const PinEntry& pin = PIN_LIST[idx];
    const char* priStr = (pin.priority == 1) ? "CRITICAL" :
                         (pin.priority == 2) ? "HIGH" :
                         (pin.priority == 3) ? "MEDIUM" : "RESERVED";

    Serial.println("");
    Serial.println("┌─────────────────────────────────────────────────────┐");
    Serial.printf("│  TESTING PIN %d / %d\n", idx + 1, (int)PIN_COUNT);
    Serial.printf("│  Pin Name:    %s\n", pin.name);
    Serial.printf("│  GPIO Number: %d\n", pin.gpio);
    Serial.printf("│  Function:    %s\n", pin.function);
    Serial.printf("│  Location:    %s\n", pin.location);
    Serial.printf("│  Priority:    %d (%s)\n", pin.priority, priStr);
    Serial.println("│");
    Serial.println("│  📍 Connect oscilloscope probe to this GPIO pin");
    Serial.println("│     → Look for 1kHz square wave (500us high, 500us low)");
    Serial.println("│     → Voltage swing: 0V to 3.3V");
    Serial.println("│");
    Serial.println("│  g = GOOD  │  b = BAD  │  n = SKIP");
    Serial.println("└─────────────────────────────────────────────────────┘");
}

void printProgress(uint32_t elapsed) {
    uint8_t secs = elapsed / 1000;
    uint8_t pct = (elapsed * 100) / PIN_TEST_DURATION_MS;
    if (pct > 100) pct = 100;

    // Build progress bar
    char bar[21];
    uint8_t filled = pct / 5;
    for (uint8_t i = 0; i < 20; i++) {
        bar[i] = (i < filled) ? '#' : '.';
    }
    bar[20] = '\0';

    Serial.printf("  TIME: %d / 5 sec  [%s] %d%%\n", secs, bar, pct);
}

void printResults() {
    Serial.println("\n\n");
    Serial.println("╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║  TEST RESULTS                                               ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");

    uint8_t good = 0, bad = 0, skip = 0;

    // Print by priority
    for (uint8_t p = 1; p <= 3; p++) {
        const char* priLabel = (p==1)?"CRITICAL":(p==2)?"HIGH":"MEDIUM";
        Serial.printf("\n  Priority %d (%s):\n", p, priLabel);
        Serial.println("  ─────────────────────────────────────────────────");

        for (uint8_t i = 0; i < PIN_COUNT; i++) {
            if (PIN_LIST[i].priority != p) continue;
            const char* status;
            if (results[i] == GOOD)    { status = "✅ GOOD";   good++; }
            else if (results[i] == BAD){ status = "❌ BAD";    bad++;  }
            else if (results[i] == SKIPPED){ status = "⏭ SKIP"; skip++; }
            else                        { status = "❓ UNTESTED"; }

            Serial.printf("  %-8s │ GPIO%-2d │ %s │ %s\n",
                          PIN_LIST[i].name, PIN_LIST[i].gpio,
                          status, PIN_LIST[i].function);
        }
    }

    Serial.println("\n  ─────────────────────────────────────────────────");
    Serial.printf("  SUMMARY:  ✅ GOOD=%d   ❌ BAD=%d   ⏭ SKIP=%d\n", good, bad, skip);
    Serial.println("  ─────────────────────────────────────────────────");

    if (bad == 0 && good > 0) {
        Serial.println("\n  🎉 All tested pins are functional!");
        Serial.println("     Problem is likely RS485 wiring or firmware config.");
    } else if (bad > 0) {
        Serial.println("\n  ⚠️  Some pins are BAD - check hardware.");
        Serial.println("     Use a GOOD Priority-2 pin as alternative DE pin.");
    }

    Serial.println("\n  Type 's' to restart test, 'r' to reprint results.");
    printSeparator();
}

void printSeparator() {
    Serial.println("──────────────────────────────────────────────────────────────");
}
