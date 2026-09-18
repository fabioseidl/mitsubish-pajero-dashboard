---
name: host-tests
description: The Unity host test suites under test/host — how to run them, how the unusual single-file compilation and mock strategy works, arm64-vs-ESP32 type rules, and how to add a test or a new suite. Use when writing or fixing tests, when a test build fails with duplicate-symbol or missing-source errors, or before flashing a server change.
---

# Host tests

All server-side and `lib/core` logic is tested on the **host** (macOS arm64), with
no hardware. Run them before flashing anything.

```bash
cd test && pio test -e native_tests
```

## Layout

```
test/
  platformio.ini            [platformio] test_dir = host  +  [env:native_tests]
  host/
    test_server/            suite — server classes
    test_lib/               suite — shared lib/core classes
    mocks/                  shared headers, NOT a suite
```

PlatformIO only treats subfolders named `test_*` as suites, which is why
`host/mocks/` is safe to keep alongside them. **`test_dir = host` under
`[platformio]` is load-bearing**: without it the test dir defaulted to `test/`,
which once held a stale duplicate of both suites — and that copy was what
`pio test` actually built, so the real tests never ran. Keep both when adding a suite.

Build flags: `-std=c++17 -DUNIT_TEST`, with `lib/core/include`,
`projects/server/include` and `projects/server_emulator/include` on the include path.

## The compilation model — read this before adding a file

Sources are **never passed to the compiler separately**. Each test file
`#include`s the implementation `.cpp` it exercises **at the bottom of the file**:

```cpp
#include "../../../projects/server/src/derived_calculator.cpp"
```

Consequences:
- A new test file must end with the same kind of include, or nothing links.
- Two test files in the same suite must not include the same `.cpp`, or you get
  duplicate symbols.
- Production code must compile without Arduino/ESP-IDF headers. That is what the
  `#ifndef UNIT_TEST` guards in the sources are for — keep hardware includes
  behind them and keep logic classes free of framework headers.

Each suite has a `test_main.cpp` with `setUp()`/`tearDown()` and a `main()` that
calls one `run_*_tests()` per test file. **Registering a new file means declaring
its `run_*_tests()` and calling it there** — otherwise it silently never runs.

## arm64 host vs. 32-bit Xtensa target

The native platform on Apple silicon compiles to `aarch64`; the firmware target is
32-bit Xtensa. `int` is 32-bit on both and `long` is **not** (64 vs 32 bit).
`float` (IEEE-754 single) and `bool` (1 byte) are identical.

**Rule:** every `Payload` field and every test assertion uses explicit fixed-width
types — `uint8_t`, `uint16_t`, `uint32_t`, `float`. Never `int`, `long` or
`size_t`. This is also what makes `sizeof(Payload)` meaningful as a host test.

## Mocks (`test/host/mocks/`)

Hardware drivers are replaced at the interface, not stubbed at the call site:

| Mock | Replaces | Notes |
|---|---|---|
| `mock_can_driver.h` | `ICANDriver` | `pushFrame()` queues frames for `readFrame()`; `begin_return_value` forces init failure |
| `mock_display.h` | `IDisplay` | records `begin_called` and `last_percent` |
| `mock_espnow.h` | ESP-NOW transport | captures sent/received payloads |

`CANDriver` itself is excluded from host tests — it is thin MCP2515/SPI glue.
Test everything above it through `MockCANDriver`.

## Coverage

Current suites cover `PIDTranslator`, `PIDDictionary`, `DataAggregator`,
`DerivedCalculator`, `SessionAccumulator`, `PayloadBuilder`, `BrightnessController`,
`ESPNowReceiver` and `ServerConnectionMonitor`.

New non-trivial logic is expected to arrive with tests. The habit this repo
follows: specify the behaviour (inputs, outputs, **edge cases**) first, write the
tests, then implement. The `DerivedCalculator` suite is the model to copy — it
pins down each fuel-rate source, the overrun-vs-cruise discrimination and every
zero/negative guard, which is what makes the calibration constants safe to touch.
