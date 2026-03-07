# 🔴 DIAGNOSTIC RAPID - ERROR 0xFF (TIMEOUT)

## ⚡ CHECK 1: ALIMENTARE & PORNIRE POMPĂ (30 secunde)

```
[ ] Pompa de căldură este alimentată electric? (verifică tablou electric)
[ ] Display pompă aprins și funcțional?
[ ] Pompă în modul STANDBY sau RUNNING? (nu OFF complet!)
[ ] Ventilatorul exterior se învârte? (dacă da, pompa e pornită)
```

**DACĂ NU** → Pornește pompa MAI ÎNTÂI, apoi retry Modbus!

---

## ⚡ CHECK 2: CONEXIUNI FIZICE RS485 (60 secunde)

### Verificare Vizuală:

```
Conexiune corectă (STANDARD):
┌──────────────┐                    ┌──────────────┐
│   M5Stamp    │                    │ Heat Pump    │
│   PLC K141   │                    │  SolarEast   │
├──────────────┤                    ├──────────────┤
│ GPIO17 (TX)  │──────┐    ┌────────│ RS485 A+     │
│              │      │    │        │  (sau D+)    │
│ GPIO18 (RX)  │──────┼────┼───┐    │              │
│              │      │    │   │    │ RS485 B-     │
│ GPIO16 (DE)  │──┐   │    │   └────│  (sau D-)    │
│              │  │   │    │        │              │
│ GND          │──┼───┼────┼────────│ GND          │
└──────────────┘  │   │    │        └──────────────┘
                  │   │    │
              [120Ω] │    │ [120Ω]
           (termination)  (termination)
                RS485     RS485
              Transceiver
```

### Checklist Conexiuni:

```
[ ] GPIO 17 (TX) → RS485 A+ (galben/roșu)?
[ ] GPIO 18 (RX) → RS485 B- (albastru/negru)?
[ ] GPIO 16 (DE) → RS485 transceiver DE/RE pin?
[ ] GND → GND (comun între M5 și pompă)?
[ ] Rezistor 120Ω între A+ și B- (LA AMBELE CAPETE)?
[ ] Cablu twisted pair (nu fire simple drepte)?
[ ] Lungime cablu < 100m?
```

**TESTARE RAPIDĂ:** Deconectează și reconectează fiecare fir, verificând tensiuni:
- A+ vs GND: ~2-5V idle (variază, dar NU 0V)
- B- vs GND: ~0-3V idle

---

## ⚡ CHECK 3: SWAP POLARITATE (10 secunde)

**Dacă tot timeout:**

```
ÎNCERCAȚI SWAP:
    Schimbă A+ cu B-
    Schimbă B- cu A+

MOTIV: Unii producători folosesc notații diferite:
    - Standard Modbus: A+/B-
    - Alte notații: D+/D-, P/N, T+/T-
```

**Procedură:**
1. Oprește M5Stamp (scoate alimentare)
2. Interschimbă firele A+ și B- la capătul pompei
3. Repornește M5Stamp
4. Rulează `>>> modbus`

---

## ⚡ CHECK 4: SLAVE ID AUTO-DETECT (30 secunde)

```bash
Serial Monitor (115200 baud):
>>> modbus

# Așteaptă scanare 1-16 (~20 sec)
# CĂUTAȚI LINIE CA ACEASTA:
[ 2/16] Slave ID  2... ✓ FOUND! Status: 0x4200 [RUNNING]
        → Ambient temp: 12.3°C
```

**Dacă găsește alt ID decât 1:**
1. Notează slave ID găsit (ex: 2, 3, etc.)
2. Editează `config.h`:
   ```cpp
   #define MODBUS_SLAVE_ID     2    // Schimbă aici!
   ```
3. Re-upload firmware (`pio run -t upload`)

---

## ⚡ CHECK 5: BAUDRATE POMPĂ (90 secunde)

**Verificare meniu pompă:**

```
Meniu pompă → Settings → Communication/Parameters → Baudrate

TREBUIE SĂ FIE: 9600 bps

Dacă e altceva (ex: 4800, 19200):
  Opțiune A: Schimbă în pompă → 9600
  Opțiune B: Schimbă în config.h → #define MODBUS_BAUDRATE 4800
```

**NOTE:**
- SolarEast default: 9600 bps (conform manual pag. 160)
- Format: 8 data bits, No parity, 1 stop bit (8N1)

---

## ⚡ CHECK 6: ACTIVARE MODBUS ÎN POMPĂ (60 secunde)

**Unele pompe au Modbus DEZACTIVAT by default!**

```
Meniu pompă → Communication → Modbus Enable

TREBUIE SĂ FIE: ON/Enabled

Dacă e OFF:
  1. Setează → ON
  2. Salvează (uneori necesită restart pompă)
  3. Retry M5Stamp modbus scan
```

---

## ⚡ CHECK 7: TESTARE HARDWARE M5 (60 secunde)

**Test loopback (fără pompă):**

```cpp
// În setup() TEMPORAR:
pinMode(RS485_RX_PIN, INPUT_PULLUP);
pinMode(RS485_TX_PIN, OUTPUT);

digitalWrite(RS485_TX_PIN, HIGH);
delay(10);
Serial.println(digitalRead(RS485_RX_PIN));  // Ar trebui: 1

digitalWrite(RS485_TX_PIN, LOW);
delay(10);
Serial.println(digitalRead(RS485_RX_PIN));  // Ar trebui: 0
```

**Dacă NU funcționează loopback → HARDWARE DEFECT M5Stamp!**

---

## ⚡ CHECK 8: TERMINATION RESISTORS (30 secunde)

```
EXTREM DE IMPORTANT pentru RS485 > 5m!

FĂRĂ REZISTORI:
    ┌────────┐          ┌────────┐
A+──│        │──────────│        │──
    │        │          │        │
B-──│        │──────────│        │──
    └────────┘          └────────┘
      M5                  Pump
      ❌ Reflexii semnal → CRC errors / Timeout

CU REZISTORI 120Ω:
    ┌────────┐          ┌────────┐
A+──│ [120Ω] │──────────│ [120Ω] │──
    │   │    │          │   │    │
B-──│   └────│──────────│   └────│──
    └────────┘          └────────┘
      M5                  Pump
      ✓ Linie terminată corect
```

**Măsurare rezistență (multimetru):**
- Scoate alimentarea de la M5 și pompă
- Măsoară între A+ și B-: ar trebui ~60Ω (120Ω || 120Ω = 60Ω)
- Dacă măsoară >100Ω → lipsește un rezistor!
- Dacă măsoară <50Ω → scurtcircuit sau rezistori greșiți!

---

## 🔴 EROARE CRITICĂ: TOT NU MERGE?

### Ultima Soluție: COMUNICARE DIRECTĂ CU ADAPTOR USB-RS485

**Hardware necesar:**
- Adaptor USB-RS485 (ex: FTDI, CH340)
- Software: QModMaster (Windows/Linux) sau ModScan

**Procedură:**
1. Deconectează M5Stamp de la RS485
2. Conectează USB-RS485 la pompă:
   ```
   USB-RS485 A+ → Pump A+
   USB-RS485 B- → Pump B-
   USB-RS485 GND → Pump GND
   ```
3. Deschide QModMaster:
   - Port: COM_X (unde e adaptorul)
   - Baudrate: 9600
   - Parity: None (8N1)
   - Slave ID: 1
   - Function: 03 (Read Holding Registers)
   - Start Address: 0x0000
   - Number of Registers: 2

4. **Click "Read"**
   - Dacă FUNCȚIONEAZĂ → problema e la M5Stamp (hardware sau soft)
   - Dacă NU FUNCȚIONEAZĂ → problema e la pompă sau wiring

---

## 📞 CONTACT SUPORT

Dacă NIMIC din checklist nu funcționează:

**Email suport:**
- Subject: "M5Stamp PLC - Modbus Timeout Error 0xFF"
- Include:
  1. Output complet comanda `>>> modbus`
  2. Output complet comanda `>>> status`
  3. Poze clare conexiuni RS485
  4. Model exact pompă de căldură
  5. Rezultate checklist 1-8 (ce ați încercat)

**ProEnergy Green SRL**  
Email: [your-support-email]  
Tel: [your-phone]

---

## ✅ SUCCESS INDICATOR

**Când merge CORECT, veți vedea:**

```bash
Serial Monitor output:

[MODBUS] ===== INITIALIZATION =====
[MODBUS] TX Pin: GPIO 17
[MODBUS] RX Pin: GPIO 18
[MODBUS] DE/RE Pin: GPIO 16
[MODBUS] ✓ UART initialized

[MODBUS] ===== AUTO-DETECTION =====
[ 1/16] Testing slave ID  1... ✓ FOUND! Status: 0x4200 [RUNNING]
        → Ambient temp: 15.2°C (confirms heat pump!)

[MODBUS] ✓✓✓ DEVICE FOUND ✓✓✓
[MODBUS] Detected slave ID: 1
[MODBUS] ✓ Slave ID matches configuration

[MODBUS] Reading temperatures... ✓ OK
  T_ambient: 15.2°C
  T_water_return: 35.0°C
  T_water_outlet: 45.0°C

[MODBUS] ✓✓✓ COMMUNICATION ESTABLISHED ✓✓✓
```

**LED Status:**
- Clipire periodică → Citire Modbus reușită
- Aprins continuu → Eroare comunicare
- Stins → Normal (idle)

---

**Document version:** 1.0  
**Data:** 2026-03-07
