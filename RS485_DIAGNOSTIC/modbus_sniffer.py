#!/usr/bin/env python3
"""
modbus_sniffer.py - Passive RS485 sniffer, captures raw Modbus frames
Usage: python3 modbus_sniffer.py --port /dev/ttyUSB0 [--baud 9600] [--duration 60]

Connects in listen-only mode and prints all bytes received.
Tries to parse Modbus RTU frames automatically.
"""

import serial
import time
import argparse
from datetime import datetime

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

def check_crc(data: bytes) -> bool:
    if len(data) < 3:
        return False
    received = data[-2] | (data[-1] << 8)
    return crc16(data[:-2]) == received

def format_hex(data: bytes, width: int = 16) -> str:
    lines = []
    for i in range(0, len(data), width):
        chunk = data[i:i+width]
        hex_part  = ' '.join(f'{b:02X}' for b in chunk)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append(f"    {i:04X}  {hex_part:<{width*3}}  {ascii_part}")
    return '\n'.join(lines)

def decode_frame(data: bytes) -> str:
    if len(data) < 4:
        return "  (too short to parse)"

    slave = data[0]
    func  = data[1]
    crc_ok = check_crc(data)

    desc = f"  Slave={slave:#04x}  FC={func:#04x}"

    if func == 0x03:
        if len(data) == 8:
            start = (data[2] << 8) | data[3]
            count = (data[4] << 8) | data[5]
            desc += f"  → READ_HOLDING  addr={start:#06x}  count={count}"
        elif len(data) > 5:
            byte_count = data[2]
            regs = [(data[3+i*2] << 8) | data[4+i*2] for i in range(byte_count // 2)]
            vals = ', '.join(f'{r}({r/10:.1f}°C?)' for r in regs[:8])
            desc += f"  → RESPONSE  {byte_count}bytes  [{vals}{'...' if len(regs)>8 else ''}]"
    elif func == 0x06:
        if len(data) == 8:
            addr = (data[2] << 8) | data[3]
            val  = (data[4] << 8) | data[5]
            desc += f"  → WRITE_SINGLE  addr={addr:#06x}  value={val}"
    elif func & 0x80:
        exc_code = data[2] if len(data) > 2 else 0
        desc += f"  → EXCEPTION  code={exc_code}"

    desc += f"  CRC={'✓' if crc_ok else '✗'}"
    return desc

def sniff(port: str, baud: int, duration: int):
    print(f"\nModbus RTU Sniffer")
    print(f"  Port:     {port}")
    print(f"  Baud:     {baud}")
    print(f"  Duration: {duration}s")
    print(f"  Started:  {datetime.now().strftime('%H:%M:%S')}\n")
    print("─" * 70)

    try:
        ser = serial.Serial(
            port=port, baudrate=baud,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=0.1
        )
    except serial.SerialException as e:
        print(f"✗ Cannot open port: {e}")
        return

    frame_count = 0
    byte_count  = 0
    start_time  = time.time()

    buf = b''
    last_byte_time = time.time()
    INTER_FRAME_GAP = 0.01  # 10ms gap = new frame

    try:
        while time.time() - start_time < duration:
            chunk = ser.read(256)
            now = time.time()

            if chunk:
                # New bytes: if gap since last byte, treat buf as complete frame
                if buf and (now - last_byte_time) > INTER_FRAME_GAP:
                    # Emit previous frame
                    frame_count += 1
                    ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                    print(f"\n[{ts}] Frame #{frame_count}  ({len(buf)} bytes)")
                    print(format_hex(buf))
                    print(decode_frame(buf))
                    buf = b''

                buf += chunk
                byte_count += len(chunk)
                last_byte_time = now
            else:
                # No data: check if current buf is a complete frame
                if buf and (now - last_byte_time) > INTER_FRAME_GAP:
                    frame_count += 1
                    ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                    print(f"\n[{ts}] Frame #{frame_count}  ({len(buf)} bytes)")
                    print(format_hex(buf))
                    print(decode_frame(buf))
                    buf = b''

    except KeyboardInterrupt:
        print("\n\n[Interrupted by user]")
    finally:
        ser.close()

    elapsed = time.time() - start_time
    print(f"\n{'='*70}")
    print(f"Sniff complete: {frame_count} frames, {byte_count} bytes in {elapsed:.1f}s")

def main():
    parser = argparse.ArgumentParser(description='Modbus RTU Passive Sniffer')
    parser.add_argument('--port',     default='/dev/ttyUSB0')
    parser.add_argument('--baud',     type=int, default=9600)
    parser.add_argument('--duration', type=int, default=60, help='Capture duration in seconds')
    args = parser.parse_args()
    sniff(args.port, args.baud, args.duration)

if __name__ == '__main__':
    main()
