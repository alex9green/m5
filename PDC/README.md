# M5Stamp PLC K141 - Heat Pump Controller
## ProEnergy Green SRL / ThermXpert - EO-AI4HP Project

### 📋 OVERVIEW

Acest cod implementează un controller IoT pentru pompe de căldură SolarEast folosind:
- **Hardware**: M5StampS3 PLC (model K141)  
- **Comunicare**: Modbus RTU prin RS485
- **Features**: Logging SD, Web server, OTA updates, calcul COP în timp real

---

## 🔌 CONEXIUNI HARDWARE

### RS485 (Modbus RTU) - Prioritar!

```
M5Stamp PLC K141          SolarEast Heat Pump
=====================     ====================
GPIO 17 (TX1)    ------>  RS485 A+ (sau D+)
GPIO 18 (RX1)    <------  RS485 B- (sau D-)
GPIO 16 (DE/RE)  ------>  RS485 DE/RE (transceiver control)
GND              ------>  GND
```

**⚠️ IMPORTANT:**
1. **Termination resistor**: Instalați rezistor 120Ω între A+ și B- la AMBELE capete de cablu
2. **Cable twisted pair**: Folosiți cablu răsucit blindat (STP) pentru RS485
3. **Lungime max**: 100m (RS485 standard)
4. **Polaritate**: Dacă nu merge, SWAP A+ cu B- (unele echipamente au notații diferite: D+/D- vs A/B)

### SD Card (SPI - HSPI)

```
M5Stamp PLC K141          SD Card Module
=====================     ==============
GPIO 11 (MOSI)   ------>  MOSI (DI)
GPIO 13 (MISO)   <------  MISO (DO)
GPIO 12 (SCK)    ------>  SCK (CLK)
GPIO 10 (CS)     ------>  CS
3.3V             ------>  VCC
GND              ------>  GND
```

### LED Status
- GPIO 21: LED intern (optional, feedback vizual)

---

## ⚙️ CONFIGURARE SOFTWARE

### 1. PlatformIO Setup

```bash
# În Visual Studio Code cu PlatformIO extensie
pio run                    # Compilare
pio run --target upload    # Upload via USB (COM3)
pio device monitor         # Serial monitor (115200 baud)
```

### 2. Configurare WiFi (Opțional)

Editați `config.h`:
```cpp
#define WIFI_SSID               "NumeReteaTa"
#define WIFI_PASSWORD           "ParolaTa"
#define ENABLE_WIFI             1
```

### 3. Configurare Modbus Slave ID

Dacă pompa NU răspunde la slave ID=1, rulați comanda `modbus` în Serial Monitor:

```
>>> modbus

[MODBUS] Scanning slave IDs 1-16...
[ 1/16] Slave ID  1... No response (err=0xE0)
[ 2/16] Slave ID  2... ✓ FOUND! Status: 0x4200 [RUNNING]
        → Ambient temp: 12.3°C (confirms heat pump!)
```

Apoi actualizați în `config.h`:
```cpp
#define MODBUS_SLAVE_ID     2    // Schimbat din 1 în 2
```

---

## 🐛 DEPANARE PROBLEME

### Error 0xFF (Timeout) - Cel mai comun!

**Cauze posibile:**

1. **Pompa nu e pornită sau RS485 dezactivat**
   - Verificați: Pompa alimentată electric?
   - Verificați: Modbus activat în meniul pompei? (uneori e dezactivat default)

2. **Wiring incorect**
   ```
   ✓ CORECT:     M5 A+ ↔ Pump A+,  M5 B- ↔ Pump B-
   ✗ GREȘIT:     M5 A+ ↔ Pump B-,  M5 B- ↔ Pump A+
   
   DACĂ NU MERGE, ÎNCERCAȚI SWAP: A+ ↔ B-, B- ↔ A+
   (unii producători inversează notația!)
   ```

3. **Lipsă rezistor de terminare**
   - Instalați 120Ω între A+ și B- la AMBELE capete de linie
   - Fără rezistor: reflexii de semnal → timeout/CRC errors

4. **Cablu prea lung sau neadecvat**
   - Max 100m pentru RS485 la 9600 baud
   - Folosiți cablu twisted pair (nu fire drepte!)
   - Evitați cabluri nescurte alături de cabluri de putere (zgomot EMI)

5. **Slave ID greșit**
   - Rulați `modbus` în Serial Monitor pentru auto-detect
   - SolarEast default: slave ID = 1 (dar poate fi schimbat în meniu pompă)

6. **Baudrate diferit**
   - Default SolarEast: 9600 bps (conform manual pag. 160)
   - Verificați meniu pompă: Parameter → Communication → Baudrate

### Error 0xE1 (CRC Error)

**Cauze:**
- Zgomot electromagnetic (EMI) pe linie
- Cablu neecranat sau prost
- Lipsă GND comun între M5 și pompă
- Interferență de la invertoare/motoare

**Soluții:**
1. Folosiți cablu STP (Shielded Twisted Pair)
2. Conectați GND între M5 și pompă
3. Adăugați ferite pe cablu
4. Separați fizic cablul RS485 de cabluri de putere

### Error 0xE2 (Exception) - Modbus Protocol Error

**Cauze:**
- Adresă de registru invalidă
- Slave ID corect, dar registrul nu există
- Încercare scriere în registru read-only

**Soluții:**
- Verificați documentația SolarEast (PDF inclus)
- Folosiți doar registre din range-urile documentate:
  - `0x0000-0x00FF`: Status & real-time data (READ ONLY)
  - `0x0300-0x032F`: User parameters (READ/WRITE)
  - `0x0330-0x035F`: User commands (READ/WRITE)

### LED nu se aprinde / ESP32 nu bootează

**Cauze:**
- PSRAM error (non-critic, dar deranjant)
- Alimentare insuficientă
- Program prea mare pentru flash

**Soluții PSRAM Error:**
```cpp
// În platformio.ini, adăugați:
build_flags = 
    -D BOARD_HAS_PSRAM=0    // Dezactivați PSRAM dacă nu e necesar
```

---

## 📊 COMENZI SERIAL MONITOR

După upload, deschideți Serial Monitor (115200 baud) și tastați comenzi:

```
help          - Listează toate comenzile
modbus        - Scanează slave IDs 1-16 (auto-detect pompă)
modbusraw     - Dump registre raw (diagnostic avansat)
status        - Status complet sistem + pompă
stats         - Statistici comunicare (success rate, erori)
read          - Forțează citire Modbus NOW
start         - Pornește pompa (scriere registru)
stop          - Oprește pompa
restart       - Restart ESP32
scan          - Scanează rețele WiFi
wifi SSID PWD - Conectare WiFi dinamic
```

### Exemple:

```bash
>>> modbus
Scanning slave IDs...
✓ Device found at ID=1, T_ambient=15.2°C

>>> status
Running: YES
T_ambient: 15.2°C
T_water_out: 45.0°C
COP: 3.45
Power: 1200 W

>>> stats
Total reads: 150
Successful: 148 (98.7%)
Failed: 2
```

---

## 🌐 WEB INTERFACE

Dacă WiFi este activat:

1. Conectați M5Stamp la rețea (vezi `status` pentru IP)
2. Deschideți browser: `http://[IP_ADDRESS]`
3. Dashboard real-time:
   - Status pompă (ON/OFF)
   - COP live
   - Temperaturi (outdoor, water inlet/outlet)
   - Putere electrică & termică
   - Comenzi START/STOP
   - Download CSV logs

API Endpoints:
```
GET  /                  - Dashboard HTML
GET  /api/status        - JSON cu toate datele
POST /api/control/start - Pornește pompa
POST /api/control/stop  - Oprește pompa
GET  /download/csv      - Download fișier CSV
```

---

## 📁 SD CARD LOGGING

Fișiere create automat:
- `/heatpump_data.csv` - Log continuu (CSV format)
- `/heatpump_data.json` - Backup JSON

**CSV Format:**
```csv
timestamp,T_amb,T_ret,T_out,T_exh,comp_hz,fan_hz,flow,power,cop,thermal_kw,delta_t,running,fault
1678901234,15.2,35.0,45.0,75.0,60,45,1200,1200,3.45,4.14,10.0,1,0
```

**Auto-rotation:** Când fișierul depășește 10MB, se creează backup cu timestamp

---

## 🔧 TROUBLESHOOTING CHECKLIST

### Înainte de a începe:

- [ ] Pompa de căldură este PORNITĂ?
- [ ] Modbus activat în meniul pompei?
- [ ] Cabluri RS485 conectate corect (A+↔A+, B-↔B-)?
- [ ] GND comun între M5 și pompă?
- [ ] Rezistori 120Ω la AMBELE capete RS485?
- [ ] Cablu twisted pair, max 100m?
- [ ] Slave ID corect (rulați `modbus` pentru auto-detect)?
- [ ] Baudrate 9600 (check in pump menu)?

### Dacă NIMIC nu funcționează:

1. **Test hardware loopback:**
   - Scoateți cablurile de la pompă
   - Conectați direct TX la RX pe M5Stamp (shortcircuit)
   - Rulați `modbus` - dacă tot dă timeout → problemă hardware M5

2. **Test cu adaptor USB-RS485:**
   - Folosiți un adaptor USB-RS485 pe PC
   - Software: QModMaster / ModScan
   - Dacă merge pe PC dar nu pe M5 → verificați pin-out M5

3. **Verificați GPIO pins:**
   ```cpp
   // Test simplu în setup():
   Serial.println(digitalRead(RS485_RX_PIN));  // Ar trebui HIGH idle
   ```

4. **Contactați suport:**
   - ProEnergy Green SRL: [contact]
   - ThermXpert: [contact]
   - Include în email:
     - Output complet `>>> modbus`
     - Output complet `>>> status`
     - Poze conexiuni hardware
     - Model exact pompă de căldură

---

## 📖 DOCUMENTAȚIE SUPLIMENTARĂ

- `modbus-SolarEast.pdf` - Manual complet registre Modbus SolarEast
- `config.h` - Toate definițiile hardware & parametri
- `ModbusRTU.h` - Implementare librărie Modbus

---

## 🚀 NEXT STEPS

După rezolvarea comunicării Modbus:

1. **Validare date** - Verificați că temperaturile/puteri sunt rezonabile
2. **Calibrare senzori** - Eventual offset correction în cod
3. **Weather compensation** - Activare curbe climatice
4. **Cloud logging** - MQTT/HTTP push către server
5. **Predictive control** - Integrare date meteo (EO-AI4HP)

---

## 📄 LICENȚĂ

Copyright © 2026 ProEnergy Green SRL / ThermXpert  
Project: EO-AI4HP (Earth Observation - AI for Heat Pumps)

---

**Versiune:** 1.0.0  
**Data:** 2026-03-07  
**Autor:** ProEnergy Green SRL Development Team
