# 🚀 QUICK START GUIDE
## M5Stamp PLC K141 - Heat Pump Controller

### ⏱️ Instalare în 5 MINUTE

---

## STEP 1: HARDWARE SETUP (2 min)

### Conexiuni RS485:

```
M5Stamp          SolarEast Pump
=======          ==============
GPIO 17   ───>   A+ (sau D+)
GPIO 18   <───   B- (sau D-)
GPIO 16   ───>   DE/RE (transceiver)
GND       ───>   GND
```

**⚠️ CRITICAL:**
- Instalați rezistor 120Ω între A+ și B-
- Folosiți cablu twisted pair
- Max 100m lungime

### Conexiuni SD Card (Opțional):

```
M5Stamp          SD Module
=======          =========
GPIO 11   ───>   MOSI
GPIO 13   <───   MISO
GPIO 12   ───>   SCK
GPIO 10   ───>   CS
3.3V      ───>   VCC
GND       ───>   GND
```

---

## STEP 2: SOFTWARE UPLOAD (2 min)

### PlatformIO (Recomandat):

```bash
# 1. Conectați M5Stamp la PC via USB
# 2. Verificați portul COM (Device Manager → Ports)
# 3. Editați platformio.ini:
upload_port = COM3    # Schimbă cu portul tău!

# 4. Upload
pio run --target upload
```

### Arduino IDE (Alternativ):

1. **Board**: ESP32S3 Dev Module
2. **Upload Speed**: 921600
3. **Flash Size**: 8MB
4. **PSRAM**: Disabled
5. **Port**: COM3 (sau portul tău)
6. **Click**: Upload →

---

## STEP 3: TESTARE & DIAGNOSTIC (1 min)

### Serial Monitor (115200 baud):

```bash
# După upload, deschide Serial Monitor
# Ar trebui să vezi:

[MODBUS] ===== INITIALIZATION =====
[MODBUS] TX Pin: GPIO 17
[MODBUS] RX Pin: GPIO 18
...
[MODBUS] ✓✓✓ DEVICE FOUND ✓✓✓
[MODBUS] Detected slave ID: 1
[MODBUS] T_ambient: 15.2°C
```

### Comenzi de test:

```
>>> help           # Listează toate comenzile
>>> modbus         # Auto-detect pompă (slave ID scan)
>>> status         # Status complet sistem
```

---

## ⚠️ DACĂ NU MERGE - Troubleshooting Ultra-Rapid

### Error 0xFF (Timeout):

1. **Pompă pornită?** → Verifică display pompă aprins
2. **Wiring corect?** → A+ la A+, B- la B-, GND la GND
3. **Termination?** → 120Ω între A+ și B- (OBLIGATORIU!)
4. **Slave ID?** → Rulează `>>> modbus` pentru auto-detect
5. **Swap A/B?** → Încercați interschimbare A+ cu B-

### Detalii complete:
- Vezi `README.md` - Ghid complet instalare
- Vezi `DIAGNOSTIC.md` - Troubleshooting detaliat

---

## 📊 CONFIGURARE AVANSATĂ (Opțional)

### WiFi Setup:

Editează `config.h`:
```cpp
#define WIFI_SSID        "NumeReteaTa"
#define WIFI_PASSWORD    "ParolaTa"
#define ENABLE_WIFI      1
```

Re-upload firmware.

### Web Dashboard:

După conectare WiFi, vezi IP în Serial Monitor:
```
✓ WiFi connected! IP: 192.168.1.100
```

Browser: `http://192.168.1.100`
- Dashboard real-time COP, temperaturi
- Comenzi START/STOP
- Download CSV logs

---

## 📖 RESURSE COMPLETE

| Fișier | Scop |
|--------|------|
| `config.h` | Toate parametrii (pini, baudrate, slave ID) |
| `ModbusRTU.h` | Librărie Modbus RTU |
| `main.cpp` | Cod principal (NU modifica dacă nu știi!) |
| `README.md` | Documentație completă |
| `DIAGNOSTIC.md` | Troubleshooting detaliat |
| `platformio.ini` | Configurare PlatformIO |

---

## ✅ SUCCESS CHECKLIST

După instalare reușită, veți avea:

- [x] Serial Monitor arată `✓✓✓ DEVICE FOUND ✓✓✓`
- [x] Temperaturi reale citite de la pompă (ex: 15.2°C)
- [x] COP calculat (ex: 3.45)
- [x] Date salvate pe SD card (dacă conectat)
- [x] Web dashboard accesibil (dacă WiFi activat)

---

## 🆘 SUPORT TEHNIC

**Dacă nimic nu funcționează:**

Email: [your-support-email]  
Include în email:
1. Output complet `>>> modbus`
2. Output complet `>>> status`
3. Poze conexiuni hardware
4. Model exact pompă

**ProEnergy Green SRL**  
ThermXpert - EO-AI4HP Project

---

**Quick Start Version:** 1.0  
**Data:** 2026-03-07
