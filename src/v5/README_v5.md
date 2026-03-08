# Heat Pump Monitor v5.0 - Hardware RS485 + SD Debug Logging

## Rezumat Schimbări față de v1-v4

### 🔴 BUG FIX CRITIC #1: RS485 DE Pin
```
GREȘIT (v1-v4): GPIO 2, 46, 1, 4, 5
CORECT (v5):    GPIO 0  ← pin oficial StamPLC.pdf pagina 9
```

### 🔴 BUG FIX CRITIC #2: Hardware RS485 Mode
```
GREȘIT (v1-v4):
  digitalWrite(dePin, HIGH);  // manual
  delayMicroseconds(500);     // imprecis
  ModbusSerial.write(data, 8);
  ModbusSerial.flush();
  delay(2);
  digitalWrite(dePin, LOW);   // race condition

CORECT (v5):
  ModbusSerial.setPins(43, 42, -1, 0);           // RTS=GPIO 0
  ModbusSerial.setMode(UART_MODE_RS485_HALF_DUPLEX); // hardware auto
  // Then just:
  ModbusSerial.write(data, 8);  // hardware face tot!
  ModbusSerial.flush();
```

## Fișiere

| Fișier | Descriere |
|--------|-----------|
| `config_v5.h` | Configurație: GPIO 0, log levels, SD paths |
| `debug_logger.h` | Sistem logging: Serial + SD card |
| `modbus_v5.h` | Modbus RTU cu hardware RS485 mode |
| `pin_test_v5.h` | Pin scanner: include GPIO 0 PRIMUL! |
| `main_v5.cpp` | Program principal |
| `platformio.ini` | Build configuration |

## SD Card Logs

| Fișier | Conținut |
|--------|----------|
| `/debug_v5.log` | Log complet (toate nivelele) |
| `/heatpump_v5.csv` | Date temperatură în format CSV |
| `/errors_v5.log` | Doar erorile critice |
| `/boot_v5.log` | Info boot și configurație |

## Web API

| Endpoint | Descriere |
|----------|-----------|
| `GET /` | Status simplu HTML |
| `GET /status` | JSON complet cu toate datele |
| `GET /history` | Ultimele 60 citiri |
| `GET /logs` | Conținut log SD |
| `GET /download/csv` | Download CSV |
| `GET /pinscan` | Rulează pin scan |
| `GET /loglevel?level=N` | Schimbă log level runtime |

## Butoane

| Buton | Pin | Acțiune |
|-------|-----|---------|
| KEYA | GPIO 39 | Reset statistici |
| KEYB | GPIO 40 | Citire imediată |
| KEYC | GPIO 41 | Cicleaza log level |

## Log Levels

| Level | Valoare | Descriere |
|-------|---------|-----------|
| NONE | 0 | Fără log |
| ERROR | 1 | Doar erori critice |
| WARN | 2 | Erori + avertismente |
| INFO | 3 | Normal (default) |
| DEBUG | 4 | Debug detaliat + hex dump RS485 |
| VERBOSE | 5 | Totul |

## Test cu Red Pitaya

Cu v5 și GPIO 0 configurat corect:
- `IN1 → GPIO 0`: Trebuie să oscileze 0V → 3.3V când transmite
- `IN2 → GND`

Dacă GPIO 0 rămâne constant → hardware mode nu e activ!

## Compilare

```bash
cd src/v5
pio run --target upload
pio device monitor --baud 115200
```
