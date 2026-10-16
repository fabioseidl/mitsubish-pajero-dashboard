# Live bus monitor

A web view of the CAN bus for `projects/sniffer`. Shows every ID on the bus with
its rate, per-byte values, how much each byte varies, and — the point of the
tool — a highlight the moment a byte that had been sitting still steps to a new
value.

That last part is `ADIFF` made visual, running on every ID at once instead of
one frame at a time in a scrolling terminal.

## Running it

Live, against the truck:

```bash
cd projects/sniffer/web
python3 bus_monitor.py --port /dev/cu.usbserial-5AC90402891
```

It syncs the sniffer's clock and sends `DUMP` on connect, so frames start
flowing without typing anything. Open <http://127.0.0.1:8750>.

Against the existing capture, with no hardware:

```bash
python3 bus_monitor.py --replay ../dumps/candump.log --speed 40
```

`--speed 0` replays as fast as the file can be read. Needs `pyserial` only for
`--port`; replay runs on the standard library alone.

The buttons send `DUMP`, `WATCH`, `ADIFF`, `DIFF`, `OBD`, `PASV` and `STOP` to
the device, and the text box sends any other command (`SIG 7E0 20F2`). Commands
are rejected in replay mode — there is nothing to send to.

## Reading the page

Each byte cell shows its current value over the count of distinct values it has
taken.

| Cell | Meaning |
|---|---|
| grey | the byte has never moved |
| blue | volatile — it churns on its own (more than 8 changes) |
| red, fading | a settled byte just stepped |

A byte counts as **settled** if it has moved at most 4 times, holds 6 or fewer
distinct values, and its frame has been seen at least 50 times. A continuous
ramp blows past the distinct limit immediately, so it can never raise a red
highlight. That is what makes the red meaningful: it is a discrete state change
— a dial, a switch, a lock — not a sensor sweeping.

Classification is cumulative over everything seen, not a fixed learning window
at startup. A window gets this wrong whenever it lands in a quiet stretch: on a
stationary capture the speed bytes of `0x215` never move, get classed as
settled, then fire thousands of false events the moment the truck rolls.

The dot beside a frame's name counts settled-byte steps for that ID.

Row colour follows what is known about the frame: green confirmed on this
vehicle, blue named in a DBC, amber a `WATCH` candidate, red unknown. See
`.claude/skills/can-reverse-engineering/references/bus-inventory.md` for the
inventory this is based on.

## Finding a new signal

1. Start it, leave the engine running, and let the bus settle for a minute.
2. Operate one input — 4WD dial, centre diff lock, handbrake, a door.
3. Watch for a red cell. Only bytes that had been still can light up, so
   whatever flashes is tied to what you just touched.
4. Confirm it with `SIG` or a `WATCH` drive before it goes near `pid_map.h`.
   The promotion rule in the skill has not changed: this tool finds candidates,
   it does not verify them.

## Why there is no decoding here

The page shows structure, not engineering values, except for the `WATCH` text it
passes through from the firmware verbatim. Frame names come from parsing the
repo's `.dbc` files.

Both are deliberate. The decoders live in `src/main.cpp` and the PID tables in
`lib/core/include/pid_map.h`; a second copy of that maths in Python would drift
from the firmware, and then a disagreement between this page and the dashboard
would tell you nothing about which one is wrong.
