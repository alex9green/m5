# Ghid Complet Instalare M5Stamp PLC K141 Heat Pump Controller
## PlatformIO - Producție Ready

**Proiect:** EO-AI4HP - ProEnergy Green SRL / ThermXpert  
**Hardware:** M5StampS3 PLC Controller K141  
**Data:** 2026-03-06

---

## 📦 Conținut Proiect

```
platformio_project/
├── platformio.ini           # Configurare PlatformIO
├── include/
│   └── config.h            # Configurări și definiții
├── lib/
│   └── ModbusRTU/
│       ├── ModbusRTU.h     # Header biblioteca MODBUS
│       └── ModbusRTU.cpp   # Implementare MODBUS
├── src/
│   └── main.cpp            # Cod principal
└── data/                   # (opțional - pentru SPIFFS)
```

---

## 🚀 INSTALARE PAS CU PAS

### Pasul 1: Instalare Visual Studio Code + PlatformIO

#### Windows:

```powershell
# 1. Descarcă și instalează VS Code
https://code.visualstudio.com/Download

# 2. Deschide VS Code
# 3. Extensions (Ctrl+Shift+X) → căutare "PlatformIO IDE"
# 4. Click Install
# 5. Restart VS Code
```

#### Linux (Ubuntu/Debian):

```bash
# Instalare VS Code
sudo snap install code --classic

# SAU manual:
wget https://code.visualstudio.com/sha/download?build=stable&os=linux-deb-x64
sudo dpkg -i code_*.deb

# Instalare PlatformIO Core (opțional, CLI)
pip install platformio --break-system-packages
```

#### Mac:

```bash
# Instalare VS Code
brew install --cask visual-studio-code

# Deschide VS Code și instalează extensia PlatformIO
```

---

### Pasul 2: Copiază Proiectul pe PC

```bash
# Windows:
# Copiază folderul platformio_project în:
C:\Users\YourName\Documents\PlatformIO\Projects\heatpump-controller

# Linux/Mac:
cp -r platformio_project ~/Documents/PlatformIO/Projects/heatpump-controller
```

---

### Pasul 3: Deschide Proiectul în VS Code

```
1. VS Code → File → Open Folder
2. Selectează: heatpump-controller
3. Așteaptă ca PlatformIO să descarce librăriile (1-2 minute)
4. Verifică că librăriile sunt listate în platformio.ini
```

---

### Pasul 4: Configurare Port Serial

#### Windows:

1. Conectează M5Stamp PLC via USB-C
2. Deschide Device Manager → Ports (COM & LPT)
3. Identifică portul (ex: COM3, COM4)
4. Editează `platformio.ini`:

```ini
upload_port = COM3    ; Schimbă cu portul tău
monitor_port = COM3
```

#### Linux:

```bash
# Verifică portul
ls /dev/ttyUSB*
# SAU
ls /dev/ttyACM*

# Adaugă user la grup dialout (pentru acces serial)
sudo usermod -a -G dialout $USER
# Logout și login din nou

# Editează platformio.ini:
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0
```

#### Mac:

```bash
# Verifică portul
ls /dev/cu.usbserial-*

# Editează platformio.ini:
upload_port = /dev/cu.usbserial-XXXXXX
```

---

### Pasul 5: Configurare WiFi și Parametri

Editează `include/config.h`:

```cpp
// WiFi Credentials
#define WIFI_SSID           "ProEnergy_IoT"    // <-- Schimbă cu SSID-ul tău
#define WIFI_PASSWORD       "thermxpert2026"   // <-- Schimbă parola

// MODBUS Slave Address
#define MODBUS_SLAVE_ID     1                  // <-- Verifică adresa pompei

// Control Parameters
#define SETPOINT_INDOOR_DEFAULT     22.0       // °C temperatura cameră dorită
#define HYSTERESIS_DEFAULT          2.0        // °C bandă moartă
#define CURVE_SLOPE_DEFAULT         1.5        // Panta curbei climatice
```

---

### Pasul 6: Compilare și Upload

#### Metoda 1: UI (recomandat pentru începători)

```
1. Click pe icoana PlatformIO (alien) în stânga
2. Project Tasks → env:m5stamp-plc-production
3. Click "Build" (compiling icon)
4. Așteaptă compilare (1-2 minute prima dată)
5. Click "Upload" (arrow up icon)
6. Așteaptă upload (30-60 secunde)
7. Click "Monitor" (plug icon) pentru a vedea output serial
```

#### Metoda 2: Terminal (pentru utilizatori avansați)

```bash
# În VS Code, deschide Terminal (Ctrl+`)

# Compilare
pio run -e m5stamp-plc-production

# Upload via USB
pio run -e m5stamp-plc-production -t upload

# Monitorizare Serial
pio device monitor -p COM3 -b 115200

# SAU tot în una:
pio run -e m5stamp-plc-production -t upload && pio device monitor
```

---

### Pasul 7: Verificare Funcționare

După upload, în Serial Monitor vei vedea:

```
================================================================================
M5Stamp PLC K141 - Heat Pump Controller
ProEnergy Green SRL / ThermXpert
EO-AI4HP Project
================================================================================

[INIT] Initializing MODBUS RS485...
✓ MODBUS OK - Status: 0x4000

[INIT] Initializing SD Card...
✓ SD Card mounted: 16384 MB
✓ Created CSV log file

[INIT] Connecting to WiFi...
.....
✓ WiFi connected
   IP: 192.168.1.100
   Hostname: heatpump-plc
   mDNS: http://heatpump-plc.local

[INIT] Setting up web server...
✓ Web server started on port 80

🚀 SYSTEM READY - Starting main loop...

================================================================================
HEAT PUMP STATUS
================================================================================
Running:       YES
Mode:          1 (Heating)
T outdoor:     5.2°C
T water out:   42.3°C
T water ret:   38.7°C
Delta-T:       3.6°C
Power:         2450 W
COP:           4.23
================================================================================
```

---

## 🌐 Acces Web Interface

După conectare WiFi, deschide browser:

```
http://192.168.1.100        (înlocuiește cu IP-ul afișat)
SAU
http://heatpump-plc.local   (funcționează pe Mac/Linux/Windows 10+)
```

Vei vedea dashboard cu:
- Status pompă (Running/Stopped)
- COP în timp real
- Temperaturi
- Putere electrică
- Butoane Start/Stop
- Download CSV logs

---

## 📥 Download Date de pe SD Card

### Metoda 1: Via Web Interface

```
http://heatpump-plc.local/download/csv
→ Descarcă automat heatpump_data.csv
```

### Metoda 2: Card SD Direct

```
1. Oprește M5Stamp PLC
2. Scoate card SD
3. Inserează în laptop
4. Copiază fișierele:
   - heatpump_data.csv
   - heatpump_readings.json
   - errors.log
   - events.log
```

---

## 🔄 OTA Update (fără cablu USB)

După prima instalare USB, poți actualiza wireless:

### 1. Activează OTA în config.h:

```cpp
#define ENABLE_OTA          1
```

### 2. Editează platformio.ini pentru OTA:

```ini
[env:m5stamp-plc-ota]
extends = env:m5stamp-plc-production
upload_protocol = espota
upload_port = 192.168.1.100     ; IP-ul M5Stamp PLC
upload_flags = 
    --port=3232
    --auth=thermxpert2026
```

### 3. Upload via OTA:

```bash
pio run -e m5stamp-plc-ota -t upload
```

---

## 🔧 Troubleshooting

### Eroare: "Port COM3 not found"

**Soluție:**
```
1. Verifică că M5Stamp este conectat via USB-C
2. Windows: Device Manager → verifică portul
3. Linux: ls /dev/ttyUSB*
4. Actualizează upload_port în platformio.ini
```

### Eroare: "SD Card mount failed"

**Soluție:**
```
1. Verifică că SD card este inserat corect
2. Formatează SD card ca FAT32 (nu exFAT)
3. Verifică pinii SD în config.h:
   #define SD_CS_PIN    8
   #define SD_MOSI_PIN  9
   #define SD_MISO_PIN  7
   #define SD_SCK_PIN   6
```

### Eroare: "MODBUS timeout"

**Soluție:**
```
1. Verifică conexiunile RS485:
   - Pin 1 (TX) → A+
   - Pin 2 (RX) → B-
   - GND conectat

2. Verifică slave ID în config.h:
   #define MODBUS_SLAVE_ID     1

3. Verifică baudrate (default 9600):
   #define MODBUS_BAUDRATE     9600

4. Test cu Serial Monitor:
   - Ar trebui să vezi "MODBUS OK" la boot
```

### Eroare: "WiFi timeout"

**Soluție:**
```
1. Verifică SSID și parolă în config.h
2. Verifică semnal WiFi (apropie de router)
3. Dezactivează WiFi dacă nu e necesar:
   #define ENABLE_WIFI         0
```

### COP = 0.00 sau valori eronate

**Cauze posibile:**
```
1. Debit apă = 0 (verifică pompă)
2. Power = 0 (pompă oprită)
3. Delta-T prea mic (pompă abia pornită, așteaptă stabilizare)

Soluție:
- Așteaptă 3-5 minute după pornire pentru valori stabile
- Verifică că pompa circulă apă (debit > 0)
```

---

## 📊 Export Date pentru Analiză

### CSV → Excel/Python/Matlab

```python
import pandas as pd

# Citire date
df = pd.read_csv('heatpump_data.csv')

# Calcul COP mediu zilnic
df['timestamp'] = pd.to_datetime(df['timestamp'], unit='s')
daily_cop = df.groupby(df['timestamp'].dt.date)['cop'].mean()

print(f"COP mediu: {df['cop'].mean():.2f}")
print(f"COP max: {df['cop'].max():.2f}")
```

### JSON → Analiză ML

```python
import json
import pandas as pd

# Citire linie cu linie
data = []
with open('heatpump_readings.json', 'r') as f:
    for line in f:
        data.append(json.loads(line))

df = pd.DataFrame(data)
```

---

## 🔐 Securitate

### 1. Schimbă parolele default:

În `config.h`:
```cpp
#define WIFI_PASSWORD       "PAROLA_TA_PUTERNICA"
#define OTA_PASSWORD        "OTA_PASSWORD_DIFERITA"
```

### 2. Restricționează accesul web:

Adaugă autentificare basic în `main.cpp`:

```cpp
server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate("admin", "parola123")) {
        return request->requestAuthentication();
    }
    request->send(200, "text/html", getStatusHTML());
});
```

---

## 📱 Monitorizare Remote

### Opțiune 1: Port Forwarding

```
1. Router → Port Forwarding
2. Extern Port 8080 → Intern 192.168.1.100:80
3. Acces: http://IP_PUBLIC:8080
```

### Opțiune 2: VPN

```
Recomandare: Tailscale, WireGuard
- Mai sigur decât port forwarding
- Acces: http://heatpump-plc.local (prin VPN)
```

### Opțiune 3: Cloud Gateway (MQTT)

Modifică codul pentru a trimite date via MQTT către broker cloud.

---

## 🆘 Suport

**Documentație Micoe MODBUS:**
- Registre: vezi `config.h` - comentarii detaliate
- Protocol: `ModbusRTU.cpp` - implementare completă

**Forumuri Utile:**
- PlatformIO Community: https://community.platformio.org
- M5Stack Forum: https://community.m5stack.com
- ESP32 Arduino: https://github.com/espressif/arduino-esp32

**Contact ProEnergy Green SRL:**
- Email: support@proenergy-green.ro
- Website: thermxpert.ro

---

## ✅ Checklist Final

- [ ] VS Code + PlatformIO instalat
- [ ] Proiect deschis în VS Code
- [ ] Port serial configurat în platformio.ini
- [ ] WiFi credentials actualizate în config.h
- [ ] MODBUS slave ID verificat
- [ ] Compilare reușită (0 errors)
- [ ] Upload reușit via USB
- [ ] Serial Monitor arată "SYSTEM READY"
- [ ] MODBUS citește date (COP > 0)
- [ ] SD Card salvează date (CSV creat)
- [ ] Web interface accesibil
- [ ] OTA configurat pentru update-uri viitoare

---

**Mult succes cu proiectul EO-AI4HP!** 🚀

*Ultima actualizare: 2026-03-06*
