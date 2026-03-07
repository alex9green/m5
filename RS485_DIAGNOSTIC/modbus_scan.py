#!/usr/bin/env python3
"""
modbus_scan.py - Auto-detect Modbus slave ID (1-247)
Usage: python3 modbus_scan.py --port /dev/ttyUSB0 [--baud 9600]

Scans all slave IDs and reports which ones respond.
"""

import serial
import struct
import time
import argparse

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def append_crc(data: bytes) -> bytes:
    c = crc16(data)
    return data + bytes([c & 0xFF, (c >> 8) & 0xFF])

def check_crc(data: bytes) -> bool:
    if len(data) < 3:
        return False
    payload = data[:-2]
    received = data[-2] | (data[-1] << 8)
    return crc16(payload) == received

def probe_slave(ser: serial.Serial, slave_id: int, timeout: float) -> bool:
    request = append_crc(struct.pack('>BBHH', slave_id, 0x03, 0x0000, 0x0001))
    ser.reset_input_buffer()
    ser.write(request)
    ser.flush()

    start = time.time()
    response = b''
    while time.time() - start < timeout:
        chunk = ser.read(64)
        if chunk:
            response += chunk
            if len(response) >= 7:  # minimum valid response
                break
        time.sleep(0.005)

    if not response:
        return False
    if response[0] != slave_id:
        return False
    if response[1] not in (0x03, 0x83):
        return False
    return check_crc(response)

def main():
    parser = argparse.ArgumentParser(description='Modbus RTU Slave Scanner')
    parser.add_argument('--port',    default='/dev/ttyUSB0')
    parser.add_argument('--baud',    type=int, default=9600)
    parser.add_argument('--start',   type=int, default=1,   help='Start slave ID (default: 1)')
    parser.add_argument('--end',     type=int, default=247, help='End slave ID (default: 247)')
    parser.add_argument('--timeout', type=float, default=0.3, help='Per-ID timeout seconds (default: 0.3)')
    args = parser.parse_args()

    print(f"\nModbus Slave Scanner")
    print(f"  Port:    {args.port}")
    print(f"  Baud:    {args.baud}")
    print(f"  Range:   {args.start} – {args.end}")
    print(f"  Timeout: {args.timeout}s per ID")
    print(f"\nScanning...\n")

    try:
        ser = serial.Serial(
            port=args.port, baudrate=args.baud,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=args.timeout
        )
    except serial.SerialException as e:
        print(f"✗ Cannot open port: {e}")
        return

    found = []
    total = args.end - args.start + 1

    for sid in range(args.start, args.end + 1):
        pct = (sid - args.start + 1) * 100 // total
        print(f"\r  Scanning ID {sid:3d}/{args.end}  [{pct:3d}%]", end='', flush=True)

        if probe_slave(ser, sid, args.timeout):
            found.append(sid)
            print(f"\n  ✅ Found slave ID: {sid}")

        time.sleep(0.02)

    ser.close()

    print(f"\n\n{'='*50}")
    if found:
        print(f"Found {len(found)} slave(s): {found}")
        print(f"\n➡ Use --slave {found[0]} in modbus_ping.py")
    else:
        print("No slaves found.")
        print("\nCheck:")
        print("  1. RS485 wiring (A+, B-, GND)")
        print("  2. Baud rate (try 19200)")
        print("  3. Device powered on")
        print("  4. 120Ω termination resistor")

if __name__ == '__main__':
    main()
