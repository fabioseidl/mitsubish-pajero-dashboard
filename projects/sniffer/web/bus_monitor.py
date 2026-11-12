#!/usr/bin/env python3
"""Live CAN bus monitor for projects/sniffer.

Reads the sniffer's serial stream (or replays a candump log), tracks structural
state per CAN ID, and serves a web page over HTTP + Server-Sent Events.

Deliberately contains no signal-decoding maths. Engineering values come from the
firmware's own WATCH output, passed through verbatim; frame names come from the
repo's DBC files. Both stay single-source, so this tool cannot drift from the
firmware the way a second copy of the decoders would.

Usage:
    python3 bus_monitor.py --port /dev/cu.usbserial-XXXX
    python3 bus_monitor.py --replay ../dumps/candump.log --speed 20
"""

import argparse
import collections
import json
import os
import re
import statistics
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
SNIFFER = os.path.dirname(HERE)

# Frame IDs the sniffer's WATCH list polls but which the DBCs do not name.
# Keep in step with the candidate list in src/main.cpp. See
# .claude/skills/can-reverse-engineering/references/bus-inventory.md
#
# CONFIRMED_IDS have been verified against the vehicle itself, so their decode is
# trusted; CANDIDATE_IDS are still only the igkov/MPS2 decode. A DBC name is used
# as the row label either way, but it does not downgrade a confirmed frame.
CONFIRMED_IDS = {0x215, 0x218, 0x308, 0x418, 0x424, 0x445, 0x608}
CANDIDATE_IDS = {0x236}

# Byte classification is cumulative, not windowed. A fixed learning window gets
# this wrong whenever the window lands in a quiet stretch: on a stationary
# capture the speed bytes of 0x215 never move, get classed stable, then fire
# thousands of false events once the truck rolls.
#
# A byte counts as volatile once it has changed this often — display only.
VOLATILE_CHANGES = 8

# An event fires only for a byte that has barely ever moved and holds few
# distinct values, once its frame has been seen enough times to trust that.
# This is the ADIFF signal: a byte constant for hundreds of frames suddenly
# stepping, i.e. a dial or switch was operated. Ramps are excluded by distinct.
EVENT_MAX_CHANGES = 4
EVENT_MAX_DISTINCT = 6
EVENT_MIN_FRAMES = 50

# Per-byte distinct-value counting stops here; enough to tell a flag from a ramp.
DISTINCT_CAP = 300

FRAME_RE = re.compile(r"^\((\d+\.\d+)\)\s+\w+\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)\s*$")
WATCH_RE = re.compile(r"^WATCH\s+(\d+\.\d+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)\s*(.*)$")
DIFF_RE = re.compile(r"^(A?DIFF)\s+(\d+\.\d+)\s+([0-9A-Fa-f]+)\s+D(\d+):\s*([0-9A-Fa-f]{2})->([0-9A-Fa-f]{2})")
# Firmware VERIFY output: the value after '=' is the firmware's own decode.
M01_POS_RE = re.compile(r"^POS PID=0x([0-9A-Fa-f]{2})\s.*?raw=((?:[0-9A-Fa-f]{2}\s?){4})(?:\s*=\s*(\S+)\s*(\S*))?")
M01_NA_RE = re.compile(r"^NA\s+PID=0x([0-9A-Fa-f]{2})\s")
M22_RE = re.compile(r"^(POS|NEG|NA)\s+ECU=0x([0-9A-Fa-f]{3})\s+DID=0x([0-9A-Fa-f]{4})\s.*?(?:nrc=0x([0-9A-Fa-f]{2}))?\s*$")
RDLI_RE = re.compile(r"^RDLI ECU=0x([0-9A-Fa-f]{3}) LID=0x([0-9A-Fa-f]{2})(?: RX=0x([0-9A-Fa-f]{3}))? (POS resp=(.*)|NEG nrc=0x([0-9A-Fa-f]{2})|no response)")
# KWP discovery output: ECUS responders, KWATCH changes, IDENT and session lines go to the log.
KWP_LOG_PREFIXES = ("ECU req=", "KWATCH ", "IDENT ", "# session ")
WATCH_KV_RE = re.compile(r"(\w+)=(\S+)")
WATCH_BRACKET_RE = re.compile(r"(\w+)\[([^\]]*)\]")

PID_MAP_H = os.path.join(SNIFFER, "..", "..", "lib", "core", "include", "pid_map.h")


def _kv(key):
    """Extractor for a `key=value` field in a WATCH line."""
    def get(text):
        """Return the value of key in text, or None."""
        m = dict(WATCH_KV_RE.findall(text))
        return m.get(key)
    return get


def _bracket(key, drop=()):
    """Extractor for a `key[...]` field in a WATCH line, minus the drop tokens."""
    def get(text):
        """Return the bracket contents for key, or None when absent."""
        m = dict(WATCH_BRACKET_RE.findall(text))
        if key not in m:
            return None
        words = [w for w in m[key].split() if w not in drop]
        return " ".join(words) or "off"
    return get


def _flag(word, frame_key):
    """Extractor reporting on/off for a bare word in a WATCH line that has frame_key."""
    def get(text):
        """Return 'on'/'off', or None if this is not the expected frame text."""
        if frame_key not in text:
            return None
        return "on" if re.search(rf"(^|[\s\[]){re.escape(word)}(\s|\]|$)", text) else "off"
    return get


def _set_temp(text):
    """Return the 0x445 set point as the firmware printed it, e.g. '22C (raw 68)'."""
    m = re.search(r"temp=(\S+?)\(raw (\d+)\)", text)
    return f"{m.group(1)} (raw {m.group(2)})" if m else None


def _recirc(text):
    """Return 'recirc'/'fresh' from a 0x445 WATCH line, or None."""
    if "ac=" not in text:
        return None
    return "recirc" if " recirc" in text else "fresh"


# Broadcast signals, read from the firmware's WATCH text — never decoded here.
# status: confirmed = verified on this vehicle; candidate = still to confirm.
# Keep in step with pid_map.h and the can-reverse-engineering skill.
BROADCAST_SIGNALS = [
    # (frame, name, unit, status, extractor, description)
    (0x215, "Vehicle speed", "km/h", "confirmed", _kv("speed"),
     "D0,D1 / 128. Matched the dash speedometer; the server's speed source."),
    (0x218, "Gear", "", "confirmed", _kv("gear"),
     "D2 low nibble = current, high = target. 1 = D at rest / 1st, R, N, P."),
    (0x608, "Coolant temperature", "C", "confirmed", _kv("coolant"),
     "D0 - 40. Matched the gauge and Mode 01 0x05; the server's coolant source."),
    (0x608, "Injected fuel quantity", "mg/s", "confirmed", _kv("fuel_raw"),
     "D5,D6. Reads 0 on overrun, ~310-340 at idle; drives the fuel-rate maths."),
    (0x424, "Headlamps", "", "confirmed", _bracket("lamps", drop=("pos",)),
     "D1 0x20 low beam, 0x04 high beam. Lamp sweep on this vehicle."),
    (0x424, "Turn signals", "", "confirmed", _bracket("turn"),
     "D1 0x02 left, 0x01 right."),
    (0x424, "Doors", "", "confirmed", _bracket("doors"),
     "D2 0x02 driver, 0x01 other doors."),
    (0x445, "A/C", "", "confirmed", _kv("ac"), "D3 0x08 A/C on."),
    (0x445, "Set temperature", "", "confirmed", _set_temp,
     "D0: 50 = LO, 64 = 20 C then 2 counts/C to 82 = 29 C, 96 = 30 C, 98 = HI."),
    (0x445, "Fan step", "", "confirmed", _kv("fan"), "D5 high nibble, 0..15."),
    (0x445, "Vent routing", "", "confirmed", _bracket("vent"),
     "D5 0x02 face, 0x04 feet, 0x08 windscreen."),
    (0x445, "Air intake", "", "confirmed", _recirc, "D3 0x02 recirculation."),
    (0x218, "Low range (4LLc)", "", "confirmed", _kv("lowrange"),
     "D4 bit 0. Set in 4LLc only; 2H, 4H and 4HLc all read off."),
    (0x418, "Selector lever", "", "confirmed", _kv("lever"),
     "D6 one bit per position: 00 P, 08 R, 10 N, 20 D."),
    (0x308, "Engine rpm (broadcast)", "rpm", "confirmed", _kv("rpm"),
     "D1,D2 direct. Matched the tachometer."),
    (0x424, "Position lamps", "", "confirmed", _flag("pos", "lamps["), "D0 0x04."),
    (0x445, "Mirror heater", "", "confirmed", _flag("mirror-heat", "ac="), "D4 0x20."),
    (0x608, "Outside temperature", "C", "candidate", _kv("ambient"),
     "qhub.kz: D1 - 40. Read 00 (-40 C) throughout candump.log; compare with a thermometer."),
    (0x312, "Engine torque", "Nm", "candidate", _kv("torque"),
     "MPS2 map: ((D0<<8|D1)/4) - 500. Should rise with throttle, go negative on overrun."),
    (0x215, "Distance counter", "counts", "candidate", _kv("trip_cnt"),
     "D2,D3. MPS2 says 25700 counts/km; drive a known distance to re-derive."),
    (0x236, "Steering angle", "", "candidate", _kv("steer"),
     "MPS2 map. Never seen on this bus — likely absent on this truck."),
]


def load_pid_catalog():
    """Return (mode01_rows, mode22_rows) parsed from lib/core/include/pid_map.h."""
    try:
        src = open(PID_MAP_H, encoding="utf-8").read()
    except OSError:
        return [], []
    defines = {m.group(1): int(m.group(2), 16)
               for m in re.finditer(r"#define\s+(PID_\w+)\s+0x([0-9A-Fa-f]+)u?", src)}
    m01 = []
    for m in re.finditer(r'\{\s*(PID_\w+),\s*"([^"]*)",\s*"([^"]*)",[^}]*?(true|false)\s*\}', src):
        if m.group(1) in defines:
            m01.append(dict(pid=defines[m.group(1)], name=m.group(2).strip(),
                            unit=m.group(3), verified=m.group(4) == "true"))
    m22 = []
    for m in re.finditer(r'\{\s*(UDS_REQ_\w+),\s*0x([0-9A-Fa-f]+),[^}]*?"([^"]*)",\s*"([^"]*)",\s*(true|false)\s*\}', src):
        req = 0x7E0 if m.group(1) == "UDS_REQ_ENGINE" else 0x7E1
        m22.append(dict(req=req, did=int(m.group(2), 16), name=m.group(3), unit=m.group(4),
                        verified=m.group(5) == "true"))
    return m01, m22


def load_dbc_names():
    """Return {can_id: message_name} parsed from the sniffer's DBC files."""
    names = {}
    for fn in ("pajero_dakar.dbc", "mitsubishi_advanced.dbc"):
        path = os.path.join(SNIFFER, fn)
        if not os.path.exists(path):
            continue
        for line in open(path, encoding="utf-8", errors="ignore"):
            m = re.match(r"^BO_\s+(\d+)\s+(\S+?):", line)
            if m:
                names[int(m.group(1))] = m.group(2)
    return names


class FrameState:
    """Rolling structural state for one CAN ID."""

    def __init__(self, can_id, dlc):
        """Start tracking can_id, sized for dlc payload bytes."""
        self.id = can_id
        self.dlc = dlc
        self.count = 0
        self.first_ts = None
        self.last_ts = None
        self.ema_dt = None
        self.data = [0] * dlc
        self.distinct = [set() for _ in range(dlc)]
        self.changed_ts = [0.0] * dlc      # wall-clock of last change, for fading
        self.changes = [0] * dlc           # how often each byte has ever moved
        self.events = 0                    # settled-byte steps worth attention

    def update(self, ts, data, now):
        """Fold one frame in. Returns a list of settled-byte changes worth reporting."""
        if len(data) > self.dlc:
            self.dlc = len(data)
            self.data += [0] * (self.dlc - len(self.data))
            self.distinct += [set() for _ in range(self.dlc - len(self.distinct))]
            self.changed_ts += [0.0] * (self.dlc - len(self.changed_ts))
            self.changes += [0] * (self.dlc - len(self.changes))

        if self.first_ts is None:
            self.first_ts = ts
        elif ts > self.last_ts:
            dt = ts - self.last_ts
            if 0 < dt < 5:   # ignore the clock-sync jump and stalls
                self.ema_dt = dt if self.ema_dt is None else 0.85 * self.ema_dt + 0.15 * dt
        self.last_ts = ts
        self.count += 1

        hits = []
        for i, b in enumerate(data):
            if len(self.distinct[i]) < DISTINCT_CAP:
                self.distinct[i].add(b)
            if self.count > 1 and b != self.data[i]:
                old = self.data[i]
                # Judge on the byte's history *before* this change.
                settled = (self.changes[i] <= EVENT_MAX_CHANGES
                           and len(self.distinct[i]) <= EVENT_MAX_DISTINCT
                           and self.count >= EVENT_MIN_FRAMES)
                self.changes[i] += 1
                self.changed_ts[i] = now
                if settled:
                    self.events += 1
                    hits.append((i, old, b))
            self.data[i] = b
        return hits

    def volatile(self):
        """Per-byte flags for bytes that churn on their own."""
        return [c > VOLATILE_CHANGES for c in self.changes]

    def hz(self):
        """Instantaneous rate from the smoothed inter-arrival time."""
        return round(1.0 / self.ema_dt, 1) if self.ema_dt else 0.0

    def snapshot(self, now, names):
        """Serialisable view for the browser."""
        # Status is what is known about the frame; the label prefers its DBC name.
        if self.id in CONFIRMED_IDS:
            status = "confirmed"
        elif self.id in names:
            status = "decoded"
        elif self.id in CANDIDATE_IDS:
            status = "candidate"
        else:
            status = "unknown"
        label = names.get(self.id, status)
        return dict(
            id=f"{self.id:03X}",
            label=label,
            status=status,
            hz=self.hz(),
            count=self.count,
            dlc=self.dlc,
            data=[f"{b:02X}" for b in self.data],
            distinct=[len(d) for d in self.distinct],
            volatile=self.volatile(),
            changes=self.changes,
            # seconds since each byte last changed; the page fades on this
            age=[round(now - t, 2) if t else None for t in self.changed_ts],
            events=self.events,
        )


class Bus:
    """Shared, lock-guarded state for the whole bus."""

    def __init__(self):
        """Create an empty bus with no frames seen yet."""
        self.lock = threading.Lock()
        self.frames = {}
        self.names = load_dbc_names()
        self.log = collections.deque(maxlen=250)   # DIFF / WATCH / '#' lines
        self.watch = {}                            # id -> firmware's decoded text
        self.watch_ts = {}                         # id -> wall-clock of that text
        self.m01_cat, self.m22_cat = load_pid_catalog()
        self.m01 = {}                              # pid -> dict(value, unit, raw, ok, ts)
        self.m22 = {}                              # (req, did) -> dict(result, nrc, ts)
        self.rdli = {}                             # "7E1/02" -> dict(result, data, ts)
        self.dumping = False                       # candump lines seen; WATCH lines are then decode-only
        self.started = time.time()
        self.total = 0
        self.source = "(not connected)"

    def note(self, kind, text):
        """Append one line to the rolling event log."""
        self.log.append(dict(t=round(time.time() - self.started, 2), kind=kind, text=text))

    def feed(self, line):
        """Parse one serial line and fold it into state."""
        now = time.time()

        m = FRAME_RE.match(line)
        if m:
            self.dumping = True
            self._frame(m, now)
            return

        m = WATCH_RE.match(line)
        if m:
            cid = int(m.group(2), 16)
            with self.lock:
                self.watch[cid] = m.group(4).strip()
                self.watch_ts[cid] = now
            # With DUMP on the same frame also arrives as a candump line.
            if not self.dumping:
                self._frame(m, now)
            return

        m = DIFF_RE.match(line)
        if m:
            self.note(m.group(1), f"{m.group(3)} D{m.group(4)}: {m.group(5)}->{m.group(6)}")
            return

        m = M01_POS_RE.match(line)
        if m:
            with self.lock:
                self.m01[int(m.group(1), 16)] = dict(
                    ok=True, value=m.group(3), unit=m.group(4) or "",
                    raw=" ".join(m.group(2).split()), ts=now)
            return

        m = M01_NA_RE.match(line)
        if m:
            with self.lock:
                self.m01[int(m.group(1), 16)] = dict(ok=False, value=None, unit="", raw="", ts=now)
            return

        m = RDLI_RE.match(line)
        if m:
            result = "POS" if m.group(5) is not None else ("NEG" if m.group(6) else "NA")
            data = (m.group(5) or "").strip() if result == "POS" else (m.group(6) or "")
            with self.lock:
                self.rdli[f"{m.group(1).upper()}/{m.group(2).upper()}"] = dict(
                    result=result, data=data, rx=(m.group(3) or "").upper(),
                    ts=round(now - self.started, 2))
            self.note("dev", line)
            return

        m = M22_RE.match(line)
        if m:
            with self.lock:
                self.m22[(int(m.group(2), 16), int(m.group(3), 16))] = dict(
                    result=m.group(1), nrc=m.group(4), ts=now)
            self.note("dev", line)
            return

        if line.startswith(KWP_LOG_PREFIXES):
            self.note("KWP", line)
            return

        if line.startswith("#") or line.startswith(("POS", "NEG", "NA ", "SIG,", "PID ")):
            self.note("dev", line)

    def _frame(self, m, now):
        """Fold a parsed frame match into per-ID state."""
        hexdata = m.group(3)
        if len(hexdata) % 2:
            return
        try:
            data = bytes.fromhex(hexdata)
        except ValueError:
            return
        ts, cid = float(m.group(1)), int(m.group(2), 16)
        with self.lock:
            st = self.frames.get(cid)
            if st is None:
                st = self.frames[cid] = FrameState(cid, len(data))
            hits = st.update(ts, data, now)
            self.total += 1
        for idx, old, new in hits:
            self.note("CHANGE", f"{cid:03X} D{idx}: {old:02X}->{new:02X}  (byte had been settled)")

    def signals(self, now):
        """Every known signal with its status, description and latest firmware value."""
        out = []
        for frame, name, unit, status, get, desc in BROADCAST_SIGNALS:
            text = self.watch.get(frame)
            value = get(text) if text is not None else None
            if value is not None and unit and value.endswith(unit):
                value = value[: -len(unit)]
            ts = self.watch_ts.get(frame) if value is not None else None
            out.append(dict(status=status, source=f"CAN 0x{frame:03X}", name=name,
                            value=value, unit=unit, raw="",
                            age=round(now - ts, 1) if ts else None, note=desc))
        for p in self.m01_cat:
            r = self.m01.get(p["pid"])
            if r is None:
                value, raw, age = None, "", None
            elif r["ok"]:
                value, raw, age = r["value"] or r["raw"], r["raw"], round(now - r["ts"], 1)
            else:
                value, raw, age = "no answer", "", round(now - r["ts"], 1)
            unit = p["unit"] if r and r["ok"] else ""
            note = ("Confirmed on this vehicle." if p["verified"]
                    else "Unverified. Confirm it answers and the value is physical.")
            out.append(dict(status="confirmed" if p["verified"] else "candidate",
                            source=f"Mode 01 0x{p['pid']:02X}", name=p["name"],
                            value=value, unit=unit if r else p["unit"], raw=raw, age=age, note=note))
        for p in self.m22_cat:
            r = self.m22.get((p["req"], p["did"]))
            value = None
            if r:
                value = "NRC 0x" + r["nrc"] if r["result"] == "NEG" and r["nrc"] else r["result"]
            out.append(dict(status="confirmed" if p["verified"] else "rejected",
                            source=f"Mode 22 {p['req']:03X}/{p['did']:04X}", name=p["name"],
                            value=value, unit="", raw="",
                            age=round(now - r["ts"], 1) if r else None,
                            note="ECUs reject UDS 0x22 (engine NRC 0x11, TCM 0x80)."))
        return out

    def snapshot(self):
        """Full serialisable state for one SSE push."""
        now = time.time()
        with self.lock:
            rows = [st.snapshot(now, self.names) for st in self.frames.values()]
            watch = dict(self.watch)
            log = list(self.log)
            total, source = self.total, self.source
            signals = self.signals(now)
            rdli = dict(self.rdli)
        order = {"confirmed": 0, "decoded": 1, "candidate": 2, "unknown": 3}
        rows.sort(key=lambda r: (order[r["status"]], -r["hz"]))
        return dict(
            rows=rows, watch={f"{k:03X}": v for k, v in watch.items()},
            signals=signals, rdli=rdli,
            log=log[-60:], total=total, source=source,
            uptime=round(now - self.started, 1),
            ids=len(rows), volatile_changes=VOLATILE_CHANGES,
        )


BUS = Bus()
WRITER = None   # callable(str) that sends a command to the device, if connected


def reader_serial(port, baud):
    """Read the sniffer over serial, syncing its clock and enabling DUMP."""
    global WRITER
    import serial   # pyserial; only needed for live capture

    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(1.5)   # let the ESP32 finish booting

    def send(cmd):
        """Write one command line to the device."""
        ser.write((cmd + "\n").encode())
        BUS.note("sent", cmd)

    WRITER = send
    BUS.source = f"serial {port} @ {baud}"
    send(f"T{int(time.time())}")
    send("DUMP")        # the sniffer is quiet until this
    send("WATCH")       # firmware decodes the broadcast signals; DUMP keeps flowing

    while True:
        try:
            line = ser.readline().decode("ascii", errors="ignore").strip()
        except Exception as exc:
            BUS.note("error", f"serial read failed: {exc}")
            return
        if line:
            BUS.feed(line)


def reader_replay(path, speed):
    """Replay a candump log, honouring its timestamps scaled by speed."""
    BUS.source = f"replay {os.path.basename(path)} x{speed}"
    while True:
        prev = None
        for line in open(path, encoding="utf-8", errors="ignore"):
            line = line.strip()
            if not line:
                continue
            m = FRAME_RE.match(line)
            if m and speed > 0:
                ts = float(m.group(1))
                if prev is not None and 0 < ts - prev < 5:
                    time.sleep((ts - prev) / speed)
                prev = ts
            BUS.feed(line)
        # Clear state at the wrap; carrying bytes across it fires false events.
        with BUS.lock:
            BUS.frames.clear()
            BUS.watch.clear()
        BUS.note("dev", "# replay reached end of file, looping (state reset)")


class Handler(BaseHTTPRequestHandler):
    """HTTP endpoints: the page, an SSE stream, a snapshot, and a command post."""

    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        """Silence per-request logging; the serial stream is the interesting output."""

    def _send(self, code, ctype, body, extra=None):
        """Write one complete response."""
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        """Serve the page, the SSE stream, or a one-shot JSON snapshot."""
        if self.path in ("/", "/index.html"):
            body = open(os.path.join(HERE, "index.html"), "rb").read()
            return self._send(200, "text/html; charset=utf-8", body)

        if self.path == "/api/state":
            body = json.dumps(BUS.snapshot()).encode()
            return self._send(200, "application/json", body)

        if self.path == "/events":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "keep-alive")
            self.end_headers()
            try:
                while True:
                    payload = json.dumps(BUS.snapshot())
                    self.wfile.write(f"data: {payload}\n\n".encode())
                    self.wfile.flush()
                    time.sleep(0.2)
            except (BrokenPipeError, ConnectionResetError):
                return
            return

        self._send(404, "text/plain", b"not found")

    def do_POST(self):
        """Forward a command line to the device, if one is connected."""
        if self.path != "/cmd":
            return self._send(404, "text/plain", b"not found")
        n = int(self.headers.get("Content-Length", 0))
        cmd = self.rfile.read(n).decode("utf-8", errors="ignore").strip()
        if WRITER is None:
            return self._send(503, "text/plain", b"no device connected (replay mode)")
        if not cmd:
            return self._send(400, "text/plain", b"empty command")
        WRITER(cmd)
        self._send(200, "text/plain", b"ok")


def main():
    """Parse arguments, start the reader thread, and serve until interrupted."""
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial device, e.g. /dev/cu.usbserial-XXXX")
    src.add_argument("--replay", help="candump log to replay instead of a live device")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--speed", type=float, default=1.0,
                    help="replay speed multiplier; 0 means as fast as possible")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--http-port", type=int, default=8750)
    args = ap.parse_args()

    if args.port:
        t = threading.Thread(target=reader_serial, args=(args.port, args.baud), daemon=True)
    else:
        t = threading.Thread(target=reader_replay, args=(args.replay, args.speed), daemon=True)
    t.start()

    srv = ThreadingHTTPServer((args.host, args.http_port), Handler)
    srv.daemon_threads = True
    print(f"bus monitor on http://{args.host}:{args.http_port}", file=sys.stderr)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped.", file=sys.stderr)


if __name__ == "__main__":
    main()
