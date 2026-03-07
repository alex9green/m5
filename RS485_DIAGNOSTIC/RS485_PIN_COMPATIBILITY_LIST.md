# RS485 GPIO Pin Compatibility List
## M5Stamp PLC K141 / M5Stack Stamp S3

**Version:** 1.0.0
**Date:** 2026-03-07

---

## Priority Legend

| Priority | Label    | Meaning                                         |
|----------|----------|-------------------------------------------------|
| 🔴 P1   | CRITICAL | Must work for RS485 to function                 |
| 🟠 P2   | HIGH     | Good alternatives if P1 fails                  |
| 🟡 P3   | MEDIUM   | Can be used but may conflict with other uses    |
| ⚫ P4   | RESERVED | Do NOT use — tied to PSRAM, USB, flash, SPI     |

---

## 🔴 Priority 1 — CRITICAL (RS485 Core Pins)

| GPIO | Name    | RS485 Function     | Notes                          |
|------|---------|--------------------|--------------------------------|
| 0    | GPIO0   | DE (Driver Enable) | Strapping pin — stable output  |
| 39   | GPIO39  | RE (Receive Enable)| Input-only safe                |
| 42   | GPIO42  | TX (Transmit)      | Fixed TX for Modbus UART       |
| 43   | GPIO43  | RX (Receive)       | Fixed RX for Modbus UART       |
| 46   | GPIO46  | DE Backup          | Strapping pin — 2nd option     |

**All 5 must work for RS485 to operate.**

---

## 🟠 Priority 2 — HIGH (Recommended Alternatives)

| GPIO | Name    | Default Use        | Notes                          |
|------|---------|--------------------|--------------------------------|
| 1    | GPIO1   | Free               | Good DE alternative            |
| 2    | GPIO2   | Free               | Good DE alternative            |
| 3    | GPIO3   | Free               | Good DE alternative            |
| 4    | GPIO4   | Free               | Good DE alternative            |
| 5    | GPIO5   | Free               | Good DE alternative            |
| 15   | GPIO15  | Free               | Stable                         |
| 16   | GPIO16  | Free               | Stable                         |
| 17   | GPIO17  | Free               | Stable                         |
| 18   | GPIO18  | Free               | Stable                         |
| 21   | GPIO21  | Grove A SDA        | I2C if Grove used              |
| 22   | GPIO22  | Grove A SCL        | I2C if Grove used              |
| 23   | GPIO23  | Expansion          | Available                      |
| 24   | GPIO24  | Expansion          | Available                      |
| 25   | GPIO25  | Expansion          | Available                      |
| 26   | GPIO26  | Expansion          | Available                      |

---

## 🟡 Priority 3 — MEDIUM (Use with Caution)

| GPIO | Name    | Default Use        | Notes                          |
|------|---------|--------------------|--------------------------------|
| 8    | GPIO8   | Grove B            | Check if Grove B used          |
| 9    | GPIO9   | SD MISO            | Conflicts with SD card         |
| 10   | GPIO10  | SD CS              | Conflicts with SD card         |
| 11   | GPIO11  | Grove B            | Check if Grove B used          |
| 29   | GPIO29  | Expansion          | Extended headers               |
| 30   | GPIO30  | Expansion          | Extended headers               |
| 31   | GPIO31  | Expansion          | Extended headers               |
| 32   | GPIO32  | Expansion          | Extended headers               |
| 33   | GPIO33  | Expansion          | Extended headers               |
| 34   | GPIO34  | Expansion          | **Input only** on S3           |
| 35   | GPIO35  | Expansion          | **Input only** on S3           |
| 36   | GPIO36  | Expansion          | **Input only** on S3           |
| 37   | GPIO37  | Expansion          | Available                      |
| 38   | GPIO38  | Expansion          | Available                      |
| 40   | GPIO40  | KEYB Button        | OK if button not used          |
| 41   | GPIO41  | KEYC Button        | OK if button not used          |
| 44   | GPIO44  | UART0 TX           | Debug serial — conflicts       |
| 45   | GPIO45  | Expansion          | Available                      |

---

## ⚫ Priority 4 — RESERVED (DO NOT USE)

| GPIO | Reason                                |
|------|---------------------------------------|
| 6    | SPI CLK (Flash)                       |
| 7    | SD SCK / SPI                          |
| 12   | HSPI MISO                             |
| 13   | HSPI MOSI                             |
| 14   | HSPI CLK                              |
| 19   | USB D−                                |
| 20   | USB D+                                |
| 27   | PSRAM                                 |
| 28   | PSRAM                                 |
| 47   | Flash                                 |
| 48   | Flash                                 |

**Using these will cause boot failure, USB disconnect, or data corruption.**

---

## Recommended DE Pin Selection Order

```
1st choice:  GPIO 46  (tested working at 100% in Elfin clone)
2nd choice:  GPIO 0   (stable strapping pin)
3rd choice:  GPIO 1   (clean general purpose)
4th choice:  GPIO 2   (clean general purpose)
5th choice:  GPIO 4   (clean general purpose)
```

---

## config_v3.h Settings

```cpp
// Set the DE pin based on your test results:
#define RS485_DE_PIN_DEFAULT    46   // Change to your best pin

// Disable auto-scan if you already know the pin:
#define AUTO_SCAN_ON_BOOT       false
```
