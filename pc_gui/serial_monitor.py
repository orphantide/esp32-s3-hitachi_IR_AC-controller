"""
serial_monitor.py — 原始串口监视，显示所有输出，不做任何过滤。
用法：python pc_gui\serial_monitor.py COM4
Ctrl+C 退出。
"""
import sys
import io
import serial
import time

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200

print(f"=== Serial Monitor: {port} @ {baud} baud ===")
print("All output will be shown. Press Ctrl+C to quit.\n")

ser = serial.Serial(port, baud, timeout=1)
time.sleep(0.3)

try:
    while True:
        line = ser.readline()
        if line:
            try:
                text = line.decode("utf-8", errors="replace").rstrip()
            except Exception:
                text = repr(line)
            print(text)
            sys.stdout.flush()
except KeyboardInterrupt:
    print("\nStopped.")
finally:
    ser.close()
