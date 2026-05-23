#!/usr/bin/env python3
import serial
import time
import sys

PORT  = "/dev/cu.usbserial-5AC90402891"
BAUD  = 115200
LOG   = "candump.log"

s = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(1.5)  # wait for ESP32 to finish booting

ts_line = f"T{int(time.time())}\n"
s.write(ts_line.encode())
print(f"Synced: {ts_line.strip()}", file=sys.stderr)

with open(LOG, "w") as f:
    try:
        while True:
            line = s.readline().decode("ascii", errors="ignore").strip()
            if not line:
                continue
            print(line)
            f.write(line + "\n")
            f.flush()
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
    finally:
        s.close()