#!/usr/bin/env python3
"""
modbus_ping.py - Simple Modbus RTU ping test for SolarEast heat pump
Usage: python3 modbus_ping.py --port /dev/ttyUSB0 [--baud 9600] [--slave 1]

Sends a Read Holding Registers request (FC03) and checks for a valid response.
"""

import serial
import struct
import time
import argparse

# ── CRC16 Modbus ────────────────────────────────────────────────────────────

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

# ── Build request ────────────────────────────────────────────────────────────

def build_read_request(slave_id: int, start_addr: int, count: int) -> bytes:
    pdu = struct.pack('>BBHH', slave_id, 0x03, start_addr, count)
    return append_crc(pdu)

# ── Ping ─────────────────────────────────────────────────────────────────────

def ping(port: str, baud: int, slave_id: int, timeout: float = 1.0) -> bool:
    request = build_read_request(slave_id, 0x0000, 0x0029)  # Block 1 (Elfin)

    print(f"\n[MODBUS PING]")
    print(f"  Port:     {port}")
    print(f"  Baud:     {baud}")
    print(f"  Slave ID: 0x{slave_id:02X} ({slave_id})")
    print(f"  Request:  {request.hex(' ').upper()}")

    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout
        )
    except serial.SerialException as e:
        print(f"\n  ✗ Cannot open port: {e}")
        return False

    # Flush
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.05)

    # Send
    ser.write(request)
    ser.flush()
    print(f"  TX: Sent {len(request)} bytes")

    # Receive
    start = time.time()
    response = b''
    while time.time() - start < timeout:
        chunk = ser.read(256)
        if chunk:
            response += chunk
            if len(response) >= 5:
                expected = response[2] + 5  # byte count + header + CRC
                if len(response) >= expected:
                    break
        time.sleep(0.01)

    ser.close()

    if not response:
        print("  ✗ No response (timeout)")
        return False

    print(f"  RX: {response.hex(' ').upper()} ({len(response)} bytes)")

    if response[0] != slave_id:
        print(f"  ✗ Wrong slave ID in response: {response[0]:#04x}")
        return False

    if response[1] == 0x83:  # Exception response for FC03
        print(f"  ✗ Modbus exception: code {response[2]}")
        return False

    if not check_crc(response):
        print("  ✗ CRC error")
        return False

    print("  ✓ VALID Modbus response — device is alive!")
    return True

# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='Modbus RTU Ping Tool')
    parser.add_argument('--port',  default='/dev/ttyUSB0', help='Serial port (default: /dev/ttyUSB0)')
    parser.add_argument('--baud',  type=int, default=9600, help='Baud rate (default: 9600)')
    parser.add_argument('--slave', type=int, default=1,    help='Slave ID (default: 1)')
    parser.add_argument('--count', type=int, default=3,    help='Number of pings (default: 3)')
    args = parser.parse_args()

    success = 0
    for i in range(args.count):
        print(f"\nPing {i+1}/{args.count}...")
        if ping(args.port, args.baud, args.slave):
            success += 1
        if i < args.count - 1:
            time.sleep(1.0)

    print(f"\n{'='*50}")
    print(f"Result: {success}/{args.count} successful")
    if success == args.count:
        print("✅ Device responding normally")
    elif success > 0:
        print("⚠️  Intermittent responses — check wiring")
    else:
        print("❌ No response — check port, baud rate, slave ID, wiring")

if __name__ == '__main__':
    main()
