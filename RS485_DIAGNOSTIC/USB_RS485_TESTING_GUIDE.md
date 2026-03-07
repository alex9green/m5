# USB-RS485 Testing Guide
## For SolarEast Heat Pump Modbus Diagnostic

**Version:** 1.0.0
**Date:** 2026-03-07

---

## Overview

Use a USB-RS485 adapter connected directly to the heat pump to verify
communication **independently of the M5Stack**, isolating hardware vs firmware issues.

---

## Hardware Required

- USB-RS485 adapter (CH340, FT232, or CP2102 based)
- 2-wire RS485 cable (A+, B−)
- PC with Python 3.8+

---

## Wiring

```
PC (USB) → USB-RS485 Adapter → Heat Pump RS485 Port
                 A+ ──────────────── A+ (or D+)
                 B− ──────────────── B− (or D−)
                GND ──────────────── GND

Optional: 120Ω resistor between A+ and B− at the adapter end
```

---

## Software Setup

```bash
# Install pyserial
pip3 install pyserial

# Find your USB port:
# Windows: Device Manager → Ports (COM & LPT)
# Linux:   ls /dev/ttyUSB*
# Mac:     ls /dev/tty.usbserial*
```

---

## Test 1: Modbus Ping

```bash
python3 modbus_ping.py --port /dev/ttyUSB0 --baud 9600 --slave 1 --count 5
```

**Expected output (working):**
```
[MODBUS PING]
  Port:     /dev/ttyUSB0
  Baud:     9600
  Slave ID: 0x01
  Request:  01 03 00 00 00 29 84 14
  TX: Sent 8 bytes
  RX: 01 03 52 00 00 ... (87 bytes)
  ✓ VALID Modbus response — device is alive!

Result: 5/5 successful
✅ Device responding normally
```

**If timeout:**
```
  ✗ No response (timeout)
Result: 0/5 successful
❌ No response — check port, baud rate, slave ID, wiring
```

---

## Test 2: Find Slave ID

If slave ID is unknown:

```bash
# Quick scan (IDs 1-10, common range)
python3 modbus_scan.py --port /dev/ttyUSB0 --baud 9600 --start 1 --end 10

# Full scan (all 247 IDs)
python3 modbus_scan.py --port /dev/ttyUSB0 --baud 9600
```

**Try baud rates if no response:**
- 9600  ← most common for SolarEast
- 19200
- 38400
- 4800

---

## Test 3: Passive Sniff

Listen passively — does not send anything, just captures traffic:

```bash
python3 modbus_sniffer.py --port /dev/ttyUSB0 --baud 9600 --duration 120
```

If the heat pump is already connected to a controller (e.g., wall panel),
you can see the existing Modbus traffic in real time.

**Example output:**
```
[12:00:15.123] Frame #1  (8 bytes)
    0000  01 03 00 00 00 29 84 14                     ........)...
  Slave=0x01  FC=0x03  → READ_HOLDING  addr=0x0000  count=41  CRC=✓

[12:00:15.890] Frame #2  (87 bytes)
    0000  01 03 52 00 00 00 00 00 00 00 00 00 00 00 ...
  Slave=0x01  FC=0x03  → RESPONSE  82bytes  [0(0.0°C?), ...]  CRC=✓
```

---

## Troubleshooting

| Symptom                        | Cause                              | Fix                                    |
|--------------------------------|------------------------------------|----------------------------------------|
| Permission denied (Linux)      | User not in dialout group          | `sudo usermod -aG dialout $USER`       |
| Port not found                 | Driver not installed               | Install CH340/FT232 driver             |
| Timeout at baud 9600           | Wrong baud rate                    | Try 19200                              |
| Timeout at all baud rates      | Wrong slave ID                     | Run modbus_scan.py                     |
| Timeout after scan             | A+/B− swapped                      | Swap the two wires                     |
| Still timeout                  | Pump Modbus disabled               | Enable in pump menu                    |
| CRC errors only                | Missing 120Ω resistor              | Add termination resistor               |
| Intermittent responses         | Cable too long or no ground        | Shorten cable, verify GND              |

---

## Reading the 3 Elfin Blocks Manually

Once ping works, you can read the 3 standard blocks:

**Block 1 — System Status:**
```
Request:  01 03 00 00 00 29 84 14
Response: 87 bytes expected
```

**Block 2 — Temperatures:**
```
Request:  01 03 00 40 00 3E C5 CE
Response: 124 bytes expected
```

**Block 3 — Water Temperatures:**
```
Request:  01 03 00 F8 00 08 C5 FD
Response: 22 bytes expected
```

To send a raw request manually:
```python
import serial, struct

def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c

ser = serial.Serial('/dev/ttyUSB0', 9600, timeout=1)
req = bytes([0x01,0x03,0x00,0x00,0x00,0x29])
c = crc16(req)
req += bytes([c&0xFF, c>>8])
ser.write(req)
resp = ser.read(87)
print(resp.hex(' ').upper())
```
