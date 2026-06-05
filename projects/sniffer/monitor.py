#!/usr/bin/env python3
import serial
import threading
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

# Capture is off by default; the LOG command toggles writing to candump.log.
g_capture = False

# Forward lines typed on stdin to the sniffer, e.g.  SCAN ENG 2000 21FF.
# LOG is a host-side command (like SCAN on the firmware): it toggles file
# capture locally and is NOT sent to the device.
def forward_stdin():
    global g_capture
    for line in sys.stdin:
        cmd = line.rstrip("\n")
        if cmd.strip().upper() == "LOG":
            g_capture = not g_capture
            print(f"# capture {'ON -> ' + LOG if g_capture else 'OFF'}", file=sys.stderr)
            continue
        s.write((cmd + "\n").encode())
        print(f"> {cmd}", file=sys.stderr)

threading.Thread(target=forward_stdin, daemon=True).start()

with open(LOG, "w") as f:
    try:
        while True:
            line = s.readline().decode("ascii", errors="ignore").strip()
            if not line:
                continue
            print(line)
            if g_capture:
                f.write(line + "\n")
                f.flush()
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
    finally:
        s.close()
