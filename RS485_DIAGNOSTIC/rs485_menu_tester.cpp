/*
 * ================================================================
 *  RS485 DIAGNOSTIC TOOL — Serial Menu Interface
 *  M5Stack StampS3 / Stamp PLC + Grove2Grove Port B
 *
 *  USAGE (Serial Monitor 115200 baud):
 *    l         → list all pins
 *    t <N>     → test pin by index (0-based) or GPIO number
 *    a         → auto-test ALL pins sequentially
 *    g         → mark current pin as GOOD ✅
 *    b         → mark current pin as BAD  ❌
 *    s         → skip current pin
 *    r         → show results summary
 *    c         → export CSV (paste into Excel)
 *    x         → stop current test
 *    h         → help
 *
 *  OSCILLOSCOPE SETUP:
 *    Probe → GPIO pin under test
 *    GND   → M5Stack GND
 *    Scale: 2V/div, 1ms/div
 *    Trigger: rising edge, ~1.5V
 *    Expected: 9600 baud UART frames (RS485 TX bursts every 100ms)
 * ================================================================
 */

#include <M5Unified.h>
#include <HardwareSerial.h>

// ── Result types (here so forward declarations can reference it) ──
enum Result : uint8_t { UNTESTED = 0, GOOD, BAD, SKIPPED, CONSOLE_PIN };

// ── Forward declarations ─────────────────────────────────────────
void startTest(int idx);
void stopTest(bool silent = false);
void markResult(Result r);
void finishAuto();
void printResults();

// ── Config ──────────────────────────────────────────────────────
#define SERIAL_BAUD     115200
#define RS485_BAUD      9600
#define TEST_TIME_MS    5000
#define PACKET_DELAY_MS 100

// ── Pin table ───────────────────────────────────────────────────
struct RS485Pin {
    uint8_t     gpio;
    const char* name;
    const char* desc;
    bool        uart_tx_capable;  // HardwareSerial TX capable
};

const RS485Pin PINS[] = {
    {  1, "G1",  "IO General",               true  },
    {  2, "G2",  "IO General",               true  },
    {  3, "G3",  "IO General",               true  },
    {  4, "G4",  "IO General",               true  },
    {  5, "G5",  "IO General",               true  },
    {  6, "G6",  "IO General",               true  },
    {  7, "G7",  "IO General",               true  },
    {  8, "G8",  "IO General",               true  },
    {  9, "G9",  "IO General",               true  },
    { 10, "G10", "IO General",               true  },
    { 11, "G11", "IO General",               true  },
    { 12, "G12", "IO General",               true  },
    { 13, "G13", "IO General",               true  },
    { 14, "G14", "IO General",               true  },
    { 15, "G15", "IO General",               true  },
    { 16, "G16", "IO General",               true  },
    { 17, "G17", "IO General",               true  },
    { 18, "G18", "IO General",               true  },
    { 21, "G21", "IO General",               true  },
    { 38, "G38", "IO General",               true  },
    { 39, "G39", "Grove-B / Grove2Grove P1", true  },
    { 40, "G40", "Grove-B / Grove2Grove P2", true  },
    { 41, "G41", "IO General",               true  },
    { 42, "G42", "IO General",               true  },
    { 43, "G43", "UART0-TX *** CONSOLA ***", false },  // console TX — skip RS485
    { 44, "G44", "UART0-RX *** CONSOLA ***", false },  // console RX — skip RS485
    { 45, "G45", "IO (Strapping)",            true  },
    { 46, "G46", "IO (Strapping)",            true  },
    { 47, "G47", "IO General",               true  },
    { 48, "G48", "IO General",               true  },
};
const int TOTAL_PINS = sizeof(PINS) / sizeof(PINS[0]);

// ── Result storage ────────────────────────────────────────────────
Result  results[TOTAL_PINS];
int     pktCounts[TOTAL_PINS];   // packets sent per pin

// ── State ────────────────────────────────────────────────────────
HardwareSerial RS485Serial(2);

int           currentPin  = -1;   // index in PINS[], -1 = idle
bool          testActive  = false;
bool          autoMode    = false;
unsigned long tStart      = 0;
int           pktCount    = 0;

// ── Helpers ──────────────────────────────────────────────────────

const char* resultStr(Result r) {
    switch (r) {
        case GOOD:        return "GOOD      ✅";
        case BAD:         return "BAD       ❌";
        case SKIPPED:     return "SKIP      ⏭";
        case CONSOLE_PIN: return "CONSOLE   ⚠️ ";
        default:          return "UNTESTED  ❓";
    }
}

void serialLine(char c = '-', int n = 62) {
    for (int i = 0; i < n; i++) Serial.print(c);
    Serial.println();
}

// Find pin index by GPIO number
int findByGPIO(int gpio) {
    for (int i = 0; i < TOTAL_PINS; i++)
        if (PINS[i].gpio == gpio) return i;
    return -1;
}

// ── Display helpers ───────────────────────────────────────────────

void displayIdle() {
    M5.Display.fillScreen(BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setCursor(0, 0);
    M5.Display.println(" RS485 DIAGNOSTIC TOOL");
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.println(" StampS3 / Stamp PLC");
    M5.Display.drawLine(0, 20, M5.Display.width(), 20, TFT_YELLOW);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(0, 26);
    M5.Display.println(" Serial Menu activ");
    M5.Display.println(" 115200 baud");
    M5.Display.setTextColor(TFT_GREEN);
    M5.Display.setCursor(0, 52);
    M5.Display.println(" Comenzi:");
    M5.Display.setTextColor(WHITE);
    M5.Display.println("  l = lista pini");
    M5.Display.println("  t N = test pin N");
    M5.Display.println("  a = auto-test ALL");
    M5.Display.println("  r = rezultate");
    M5.Display.println("  c = export CSV");
}

void displayTest(int idx, int rem, int pkt) {
    M5.Display.fillRect(0, 0, M5.Display.width(), 14, TFT_NAVY);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(2, 3);
    M5.Display.printf("RS485 TX [%d/%d]", idx + 1, TOTAL_PINS);

    M5.Display.setTextSize(3);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(10, 18);
    M5.Display.printf("%s  ", PINS[idx].name);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(2, 64);
    M5.Display.printf("GPIO %-2d | %d baud", PINS[idx].gpio, RS485_BAUD);

    M5.Display.setTextColor(TFT_ORANGE);
    M5.Display.setCursor(2, 76);
    M5.Display.printf("%-28s", PINS[idx].desc);

    M5.Display.drawLine(0, 88, M5.Display.width(), 88, TFT_CYAN);

    // Timer
    M5.Display.setTextSize(2);
    M5.Display.setCursor(2, 93);
    M5.Display.setTextColor(rem <= 2 ? TFT_RED : TFT_GREEN, BLACK);
    M5.Display.printf(" %ds  PKT:%-4d", rem, pkt);

    // Progress bar
    int barW = M5.Display.width() - 4;
    int barH = 8;
    int barY = M5.Display.height() - barH - 14;
    int elapsed = millis() - tStart;
    int filled  = (int)((float)elapsed / TEST_TIME_MS * barW);
    if (filled > barW) filled = barW;
    M5.Display.drawRect(2, barY, barW, barH, TFT_WHITE);
    M5.Display.fillRect(2, barY, filled, barH, TFT_GREEN);

    // Controls hint
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setCursor(0, M5.Display.height() - 10);
    M5.Display.printf(" g=GOOD  b=BAD  s=SKIP  x=STOP");
}

// ── Serial Menu ───────────────────────────────────────────────────

void printHelp() {
    serialLine('=');
    Serial.println("  RS485 DIAGNOSTIC TOOL — Serial Menu");
    Serial.println("  M5Stack StampS3 / Stamp PLC");
    serialLine('=');
    Serial.println("  COMENZI:");
    Serial.println("   l        → listeaza toti pinii");
    Serial.println("   t <N>    → testeaza pin (index 0-29 SAU GPIO number)");
    Serial.println("              ex: 't 5'  sau  't 39'");
    Serial.println("   a        → auto-test TOTI pinii secvential");
    Serial.println("   g        → marcheaza pin curent = GOOD ✅");
    Serial.println("   b        → marcheaza pin curent = BAD  ❌");
    Serial.println("   s        → skip pin curent");
    Serial.println("   x        → opreste testul curent");
    Serial.println("   r        → afiseaza rezultate");
    Serial.println("   c        → export CSV (lipeste in Excel)");
    Serial.println("   h        → help");
    serialLine();
    Serial.println("  OSCILOSCOP:");
    Serial.println("   Probe   → GPIO pin afisat");
    Serial.println("   GND     → GND M5Stack");
    Serial.println("   Scale   → 2V/div, 1ms/div");
    Serial.println("   Trigger → Rising edge, 1.5V");
    Serial.println("   Signal  → UART 9600 baud bursts la fiecare 100ms");
    serialLine('=');
}

void printPinList() {
    serialLine('=');
    Serial.println("  IDX | GPIO | NAME | STATUS      | DESC");
    serialLine('-');
    for (int i = 0; i < TOTAL_PINS; i++) {
        char line[80];
        snprintf(line, sizeof(line), "  %3d | G%-3d | %-4s | %-12s | %s",
                 i, PINS[i].gpio, PINS[i].name,
                 resultStr(results[i]), PINS[i].desc);
        // Color coding based on result
        if      (results[i] == GOOD)        Serial.print("\033[32m");  // green
        else if (results[i] == BAD)         Serial.print("\033[31m");  // red
        else if (results[i] == SKIPPED)     Serial.print("\033[33m");  // yellow
        else if (results[i] == CONSOLE_PIN) Serial.print("\033[35m");  // magenta
        Serial.println(line);
        Serial.print("\033[0m");  // reset color
    }
    serialLine();
    int good = 0, bad = 0, skip = 0, unt = 0;
    for (int i = 0; i < TOTAL_PINS; i++) {
        if (results[i] == GOOD)    good++;
        else if (results[i] == BAD)    bad++;
        else if (results[i] == SKIPPED) skip++;
        else if (results[i] == UNTESTED) unt++;
    }
    Serial.printf("  TOTAL: ✅ GOOD=%d  ❌ BAD=%d  ⏭ SKIP=%d  ❓ NETESTAT=%d\n",
                  good, bad, skip, unt);
    serialLine('=');
}

void printResults() {
    serialLine('=');
    Serial.println("  REZULTATE FINALE");
    serialLine('-');

    Serial.println("\n  ✅ PINI BUNI (compatibili RS485 TX):");
    bool any = false;
    for (int i = 0; i < TOTAL_PINS; i++) {
        if (results[i] == GOOD) {
            Serial.printf("     GPIO %-2d (%s) — %d pachete trimise\n",
                          PINS[i].gpio, PINS[i].name, pktCounts[i]);
            any = true;
        }
    }
    if (!any) Serial.println("     (niciunul confirmat inca)");

    Serial.println("\n  ❌ PINI DEFECTI:");
    any = false;
    for (int i = 0; i < TOTAL_PINS; i++) {
        if (results[i] == BAD) {
            Serial.printf("     GPIO %-2d (%s)\n", PINS[i].gpio, PINS[i].name);
            any = true;
        }
    }
    if (!any) Serial.println("     (niciunul)");

    Serial.println("\n  ❓ NETESTATI:");
    any = false;
    for (int i = 0; i < TOTAL_PINS; i++) {
        if (results[i] == UNTESTED) {
            Serial.printf("     GPIO %-2d (%s)\n", PINS[i].gpio, PINS[i].name);
            any = true;
        }
    }
    if (!any) Serial.println("     (toti testati)");
    serialLine('=');
}

void printCSV() {
    serialLine('=');
    Serial.println("  CSV EXPORT (copiaza in Excel):");
    serialLine('-');
    Serial.println("Index,GPIO,Name,Status,Packets,Description,Timestamp");
    for (int i = 0; i < TOTAL_PINS; i++) {
        const char* st;
        if      (results[i] == GOOD)        st = "GOOD";
        else if (results[i] == BAD)         st = "BAD";
        else if (results[i] == SKIPPED)     st = "SKIPPED";
        else if (results[i] == CONSOLE_PIN) st = "CONSOLE";
        else                                st = "UNTESTED";
        Serial.printf("%d,%d,%s,%s,%d,%s,2026-03-07\n",
                      i, PINS[i].gpio, PINS[i].name,
                      st, pktCounts[i], PINS[i].desc);
    }
    serialLine('=');
}

// ── Test control ──────────────────────────────────────────────────

void startTest(int idx) {
    if (idx < 0 || idx >= TOTAL_PINS) {
        Serial.printf("  ⚠️  Index invalid: %d (valid: 0-%d)\n", idx, TOTAL_PINS - 1);
        return;
    }

    // Console pins — skip RS485
    if (!PINS[idx].uart_tx_capable) {
        results[idx] = CONSOLE_PIN;
        serialLine();
        Serial.printf("  ⚠️  GPIO %d = CONSOLA UART0 — nu se poate folosi pentru RS485!\n",
                      PINS[idx].gpio);
        Serial.println("      Skip automat.");
        serialLine();
        if (autoMode) {
            // advance to next
            int next = idx + 1;
            if (next < TOTAL_PINS) startTest(next);
            else                   finishAuto();
        }
        return;
    }

    currentPin  = idx;
    tStart      = millis();
    pktCount    = 0;
    testActive  = true;

    RS485Serial.begin(RS485_BAUD, SERIAL_8N1, -1, PINS[idx].gpio);

    serialLine('=');
    Serial.printf("  🔬 TESTARE PIN [%d / %d]\n", idx + 1, TOTAL_PINS);
    serialLine('-');
    Serial.printf("  Index    : %d\n", idx);
    Serial.printf("  GPIO     : %d\n", PINS[idx].gpio);
    Serial.printf("  Nume     : %s\n", PINS[idx].name);
    Serial.printf("  Descriere: %s\n", PINS[idx].desc);
    Serial.printf("  Baud     : %d (8N1)\n", RS485_BAUD);
    Serial.printf("  Durata   : %d secunde\n", TEST_TIME_MS / 1000);
    serialLine('-');
    Serial.println("  📍 Conecteaza osciloscopul la GPIO si GND.");
    Serial.println("     Semnal asteptat: UART 9600 baud, burst la fiecare 100ms");
    serialLine('-');
    Serial.println("  > g = GOOD  | b = BAD  | s = SKIP  | x = STOP");
    serialLine();

    M5.Display.fillScreen(BLACK);
}

void stopTest(bool silent = false) {
    if (!testActive) return;
    RS485Serial.end();
    delay(50);
    testActive = false;
    pktCounts[currentPin] = pktCount;
    if (!silent) {
        Serial.printf("  Pachete trimise pe GPIO %d: %d\n",
                      PINS[currentPin].gpio, pktCount);
    }
}

void markResult(Result r) {
    if (currentPin < 0) {
        Serial.println("  ⚠️  Niciun test activ.");
        return;
    }
    stopTest(true);
    results[currentPin] = r;
    pktCounts[currentPin] = pktCount;

    serialLine();
    Serial.printf("  GPIO %d → %s  (%d pachete)\n",
                  PINS[currentPin].gpio, resultStr(r), pktCount);
    serialLine();

    if (autoMode) {
        int next = currentPin + 1;
        if (next < TOTAL_PINS) startTest(next);
        else                   finishAuto();
    } else {
        currentPin = -1;
        displayIdle();
    }
}

void finishAuto() {
    autoMode  = false;
    currentPin = -1;
    Serial.println("\n  ✅ AUTO-TEST COMPLET!");
    printResults();
    displayIdle();
}

// ── Serial command parser ─────────────────────────────────────────

String inputBuf = "";

void parseCommand(String cmd) {
    cmd.trim();
    if (cmd.length() == 0) return;

    char c = tolower(cmd.charAt(0));

    // Single-char commands
    if      (c == 'h') { printHelp(); return; }
    else if (c == 'l') { printPinList(); return; }
    else if (c == 'r') { printResults(); return; }
    else if (c == 'c') { printCSV(); return; }
    else if (c == 'g') { markResult(GOOD); return; }
    else if (c == 'b') { markResult(BAD); return; }
    else if (c == 's') { markResult(SKIPPED); return; }
    else if (c == 'x') {
        if (testActive) {
            stopTest();
            autoMode = false;
            Serial.println("  Test oprit.");
            displayIdle();
        } else {
            Serial.println("  Niciun test activ.");
        }
        return;
    }
    else if (c == 'a') {
        // Auto-test all
        if (testActive) { Serial.println("  Opreste testul curent mai intai (x)."); return; }
        autoMode = true;
        Serial.println("  ▶ AUTO-TEST ALL PINS...");
        startTest(0);
        return;
    }
    else if (c == 't') {
        // Test specific pin: "t 5" or "t 39"
        if (cmd.length() < 3) {
            Serial.println("  Sintaxa: t <N>  (ex: t 5  sau  t 39)");
            return;
        }
        int val = cmd.substring(2).toInt();
        if (testActive) { Serial.println("  Opreste testul curent mai intai (x)."); return; }

        // Try as index first (0-29), else as GPIO number
        int idx = -1;
        if (val >= 0 && val < TOTAL_PINS) {
            idx = val;  // treat as index
        } else {
            idx = findByGPIO(val);
            if (idx < 0) {
                Serial.printf("  ⚠️  GPIO %d nu exista in lista. Foloseste 'l' sa vezi lista.\n", val);
                return;
            }
        }
        autoMode = false;
        startTest(idx);
        return;
    }

    Serial.printf("  Comanda necunoscuta: '%s'. Tasteaza 'h' pentru help.\n", cmd.c_str());
}

// ── Setup / Loop ──────────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);

    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(200);
    M5.Display.setRotation(0);

    // Init results
    for (int i = 0; i < TOTAL_PINS; i++) {
        results[i]   = UNTESTED;
        pktCounts[i] = 0;
    }
    // Mark console pins upfront
    for (int i = 0; i < TOTAL_PINS; i++) {
        if (!PINS[i].uart_tx_capable)
            results[i] = CONSOLE_PIN;
    }

    displayIdle();
    printHelp();
    Serial.println("  Gata! Tasteaza 'l' sa vezi pinii sau 'a' sa incepi auto-test.");
    serialLine('=');
}

void loop() {
    M5.update();

    // BtnA = start/next (hardware fallback)
    if (M5.BtnA.wasPressed()) {
        if (!testActive && !autoMode) {
            autoMode = true;
            startTest(0);
        }
    }

    // Read serial input
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (inputBuf.length() > 0) {
                parseCommand(inputBuf);
                inputBuf = "";
            }
        } else {
            inputBuf += ch;
            Serial.print(ch);  // echo
        }
    }

    // Active test loop
    if (testActive && currentPin >= 0) {
        uint32_t elapsed = millis() - tStart;

        if (elapsed < TEST_TIME_MS) {
            // Send RS485 packet
            RS485Serial.printf(
                "RS485 TX G%02d PKT:%05d 55AA FF00 ABCD ts:%lu [OK]\r\n",
                PINS[currentPin].gpio, pktCount++, millis());

            // Update display
            int rem = (TEST_TIME_MS - elapsed) / 1000 + 1;
            displayTest(currentPin, rem, pktCount);

            // Serial progress every second
            static uint32_t lastPrint = 0;
            if (millis() - lastPrint >= 1000) {
                lastPrint = millis();
                int rem2 = (TEST_TIME_MS - elapsed) / 1000;
                Serial.printf("  GPIO %-2d | %ds rimasi | PKT:%d\n",
                              PINS[currentPin].gpio, rem2, pktCount);
            }

            delay(PACKET_DELAY_MS);
        } else {
            // Time expired — auto-advance in autoMode, else wait for user
            if (autoMode) {
                Serial.printf("  ⏱ Timeout GPIO %d — marcat SKIP automat\n",
                              PINS[currentPin].gpio);
                markResult(SKIPPED);
            } else {
                // Stop sending but wait for user decision
                stopTest(true);
                Serial.println("\n  ⏱ Timp expirat. Tasteaza: g / b / s");
            }
        }
    }
}
