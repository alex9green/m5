# RS485 Diagnostic Report
## M5Stamp PLC K141 — SolarEast Heat Pump

**Date:** 2026-03-07
**Firmware:** 3.0.0
**Device:** M5Stack StamPLC K141 (ESP32-S3)

---

## Summary

| Item                  | Status         | Notes                              |
|-----------------------|----------------|------------------------------------|
| RS485 DE Pin (GPIO46) | ✅ WORKING     | 5/5 tests passed, 100% success     |
| Modbus Baud Rate      | ✅ 9600 bps    | Confirmed from Elfin EW11 log      |
| Slave ID              | ✅ 0x01        | Confirmed                          |
| Block 1 (System)      | ✅ 87 bytes    | CRC OK                             |
| Block 2 (Temps)       | ✅ 124 bytes   | CRC OK                             |
| Block 3 (Water)       | ✅ 22 bytes    | CRC OK                             |
| Ambient Temp          | ✅ 11.0°C      | Register offset 23 (byte 46)       |
| WiFi Connection       | ✅ Connected   | 192.168.1.153                      |
| SD Card               | ✅ OK          | CSV logging active                 |

---

## Modbus Protocol Details

### Block 1 — System Status (Registers 0x0000–0x0028)

```
Request:  01 03 00 00 00 29 84 14
Response: 87 bytes
  SlaveID=0x01  FC=0x03  ByteCount=0x52 (82 bytes)
  Registers: 0x0000–0x0028 (41 registers)
```

**Decoded:**
| Register | Offset | Raw Value | Decoded         |
|----------|--------|-----------|-----------------|
| 0x0000   | 0      | 0x4040    | System Status   |
| 0x0001   | 2      | 0x4040    | Operating Mode  |

---

### Block 2 — Temperatures (Registers 0x0040–0x007D)

```
Request:  01 03 00 40 00 3E C5 CE
Response: 124 bytes
  SlaveID=0x01  FC=0x03  ByteCount=0x7C (124 bytes)
  Registers: 0x0040–0x007D (62 registers)
```

**Temperature Map (confirmed):**
| Register | Byte Offset | Raw    | Value   | Sensor            |
|----------|-------------|--------|---------|-------------------|
| 0 (0x40) | 0           | 0x0037 | 55→5.5°C| T1 Evap In       |
| 1 (0x41) | 2           | 0x0037 | 55→5.5°C| T2 Evap Out      |
| 2 (0x42) | 4           | 0x00F8 | 248→24.8°C| T3 Comp Discharge|
| 23(0x57) | 46          | 0x006E | 110→**11.0°C**| **T_Ambient** ✅ |

---

### Block 3 — Water Temperatures (Registers 0x00F8–0x00FF)

```
Request:  01 03 00 F8 00 08 C5 FD
Response: 22 bytes
  SlaveID=0x01  FC=0x03  ByteCount=0x10 (16 bytes)
  Registers: 0x00F8–0x00FF (8 registers)
```

**Decoded:**
| Register | Raw    | Value   | Sensor         |
|----------|--------|---------|----------------|
| 0 (0xF8) | 0x00BF | 191→19.1°C | Water Inlet |
| 1 (0xF9) | 0x0002 | 2→0.2°C    | (see note)   |
| 2 (0xFA) | 0x0039 | 57→5.7°C   | Water Outlet |

---

## Wiring Configuration (Confirmed Working)

```
M5Stamp PLC K141
  GPIO 42 (TX) ──────────────── A+ (RS485)
  GPIO 43 (RX) ──────────────── B− (RS485)
  GPIO 46 (DE) ──── [internal]  (Driver Enable)
  GND ───────────────────────── GND

Heat Pump RS485 Port
  A+ (D+) ───────────────────── GPIO 42
  B− (D−) ───────────────────── GPIO 43
  GND ───────────────────────── GND

Termination: 120Ω between A+ and B− recommended
```

---

## Issues Found & Solutions

### Issue 1: Temperature Offset Mapping (RESOLVED)

**Problem:** Temperature register offsets were unknown.
**Solution:** Compared Elfin EW11 hex dump with known ambient temperature (11.0°C).
- Raw value 0x006E = 110 decimal → 110/10 = 11.0°C ✅
- Located at byte offset 46 in Block 2 = register index 23

### Issue 2: DE Pin Uncertainty (RESOLVED)

**Problem:** Unsure which GPIO to use for RS485 DE.
**Solution:** Auto-scan tested GPIO 46, 1, 4, 5, 2 — GPIO 46 passed 5/5 tests.

---

## Next Steps

- [ ] Map remaining temperature registers (T2–T8)
- [ ] Verify Water Inlet/Outlet offsets with known temperatures
- [ ] Add MQTT publishing for cloud integration
- [ ] Implement write commands (pump control)
