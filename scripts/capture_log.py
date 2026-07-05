#!/usr/bin/env python3
"""Capture Flipper Zero CLI log output for a fixed duration.

Usage: python capture_log.py [seconds]
Opens the Flipper serial CLI, issues the `log` command, and prints
everything received for the given number of seconds (default 20).
"""
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial not installed. Run: pip install pyserial")

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 20

# Flipper Zero USB VID:PID (STMicroelectronics CDC ACM)
FLIPPER_VID = "0483"
FLIPPER_PID = "5740"

ports = serial.tools.list_ports.comports()
flipper_ports = [
    p.device for p in ports
    if p.vid == int(FLIPPER_VID, 16) and p.pid == int(FLIPPER_PID, 16)
]

if not flipper_ports:
    print("No Flipper serial device found.")
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device}: {p.description} ({p.hwid})")
    sys.exit(1)

port = flipper_ports[0]
print(f"Found Flipper on {port} ({flipper_ports[0]})", flush=True)

with serial.Serial(port, 115200, timeout=0.1) as ser:
    time.sleep(0.2)
    ser.reset_input_buffer()
    # Enter CLI, start log streaming
    ser.write(b"\r\n")
    time.sleep(0.2)
    ser.write(b"log trace\r\n")
    print(f"# Streaming log from {port} for {DURATION}s. Do the scan now...", flush=True)
    deadline = time.time() + DURATION
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
    # Ctrl-C to stop log, return to prompt
    ser.write(b"\x03")
    time.sleep(0.1)
print("\n# --- capture complete ---", flush=True)
