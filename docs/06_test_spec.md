# Test Specification

## Methodology: Test-Driven Development (TDD)

All non-trivial logic is written test-first. The red-green-refactor cycle is strictly followed:

1. Write a failing test that defines the expected behavior
2. Write the minimum production code to make the test pass
3. Refactor without breaking the test

Framework: **Unity** (via PlatformIO native environment)
Execution: host machine — macOS ARM64 (Apple M2), no hardware required
Hardware dependencies are replaced with mock implementations in all host tests.

---

## ARM64 Host Considerations

The `native` PlatformIO platform on Apple M2 compiles to `aarch64`. The ESP32 target is 32-bit Xtensa. Key differences:

- `int` is 32-bit on both, but `long` is 64-bit on ARM64 vs 32-bit on ESP32
- `bool` is 1 byte on both
- `float` (IEEE 754 single precision) is identical on both

**Rule:** All struct fields and test assertions must use explicit fixed-width types (`uint8_t`, `uint16_t`, `uint32_t`, `float`). Never use `int`, `long`, or `size_t` in `Payload` fields or test values.

---

## Mock Strategy

### `mock_can_driver.h`

```cpp
class MockCANDriver : public ICANDriver {
public:
    bool begin() override { return begin_return_value; }

    bool isFrameAvailable() override {
        return !queued_frames.empty();
    }

    bool readFrame(CANFrame& out) override {
        if (queued_frames.empty()) return false;
        out = queued_frames.front();
        queued_frames.pop();
        return true;
    }

    void pushFrame(const CANFrame& frame) {
        queued_frames.push(frame);
    }

    bool begin_return_value = true;
    std::queue<CANFrame> queued_frames;
};
```

### `mock_display.h`

```cpp
class MockDisplay : public IDisplay {
public:
    bool begin() override {
        begin_called = true;
        return true;
    }

    void setBacklightPercent(uint8_t percent) override {
        last_percent = percent;
        call_count++;
    }

    bool    begin_called = false;
    uint8_t last_percent = 0;
    int     call_count   = 0;
};
```

### `mock_espnow.h`

Provides `simulateReceive(data, len)` to trigger `ESPNowReceiver::onReceiveISR` with controlled input without a real ESP-NOW stack.

```cpp
void simulateReceive(ESPNowReceiver& receiver,
                     const uint8_t* data,
                     int len);
```

---

## Test Suites

---

### Suite: `test_payload_size`

Validates `Payload` struct size is consistent between ARM64 host and the expected wire format.

```
TEST: payload_size_matches_expected
  Expected: sizeof(Payload) == 27
  Rationale: Guards against padding differences between ARM64 host
             and ESP32 target. Must match the static_assert in payload.h.
```

---

### Suite: `test_pid_translator`

Tests `PIDTranslator::translate()`, `extractMilStatus()`, and `extractDtcCount()`.

```
TEST: translate_rpm_two_bytes
  Given: frame.data = {0x41, 0x0C, 0x00, 0x1A, 0xF0}
         def = PID_MAP entry for 0x0C
  Expected: result == (256 * 0x1A + 0xF0) * 0.25f == 1724.0f

TEST: translate_speed_one_byte
  Given: frame.data = {0x41, 0x0D, 0x64}
         def = PID_MAP entry for 0x0D
  Expected: result == 100.0f

TEST: translate_maf_two_bytes
  Given: frame.data = {0x41, 0x10, 0x01, 0x2C}
         def = PID_MAP entry for 0x10
  Expected: result == (256 * 0x01 + 0x2C) * 0.01f == 3.0f

TEST: translate_fuel_rate_two_bytes
  Given: frame.data = {0x41, 0x5E, 0x00, 0x64}
         def = PID_MAP entry for 0x5E
  Expected: result == (0 * 256 + 0x64) * 0.05f == 5.0f

TEST: translate_coolant_temp_with_offset
  Given: frame.data = {0x41, 0x05, 0x5F}
         def = PID_MAP entry for 0x05
  Expected: result == 0x5F * 1.0f + (-40.0f) == 55.0f

TEST: translate_egr_error_with_negative_offset
  Given: frame.data = {0x41, 0x2D, 0x80}
         def = PID_MAP entry for 0x2D
  Expected: result == 0x80 * 0.78125f - 100.0f == 0.0f

TEST: translate_all_zero_bytes_returns_offset
  Given: all data bytes = 0x00
  Expected: result == def.offset for each PID

TEST: extract_mil_status_bit7_set
  Given: frame.data[3] = 0x80  (bit 7 set)
  Expected: extractMilStatus(frame) == true

TEST: extract_mil_status_bit7_clear
  Given: frame.data[3] = 0x05
  Expected: extractMilStatus(frame) == false

TEST: extract_dtc_count_bits_0_to_6
  Given: frame.data[3] = 0x85  (0b10000101)
  Expected: extractDtcCount(frame) == 5  (bits 0–6 = 0x05)

TEST: extract_dtc_count_zero
  Given: frame.data[3] = 0x00
  Expected: extractDtcCount(frame) == 0
```

---

### Suite: `test_pid_dictionary`

Tests PID lookup correctness against `PID_MAP`.

```
TEST: lookup_known_pid_returns_definition
  Given: lookup(0x7E8, PID_RPM)
  Expected: non-null pointer, result->pid == PID_RPM

TEST: lookup_unknown_pid_returns_null
  Given: lookup(0x7E8, 0xFF)
  Expected: nullptr

TEST: lookup_unknown_can_id_returns_null
  Given: lookup(0x123, PID_RPM)
  Expected: nullptr

TEST: lookup_all_confirmed_pids_succeed
  For each PID in PID_MAP where verified == true:
    Expected: lookup(0x7E8, pid) != nullptr

TEST: isKnownId_true_for_obd_response_address
  Expected: isKnownId(0x7E8) == true

TEST: isKnownId_false_for_unknown_address
  Expected: isKnownId(0x001) == false

TEST: size_returns_pid_map_size
  Expected: size() == PID_MAP_SIZE
```

---

### Suite: `test_data_aggregator`

```
TEST: get_before_update_returns_zero
  Expected: aggregator.get(PID_RPM) == 0.0f

TEST: isValid_before_update_returns_false
  Expected: aggregator.isValid(PID_RPM) == false

TEST: update_then_get_returns_value
  When:  aggregator.update(PID_RPM, 3000.0f)
  Expected: aggregator.get(PID_RPM) == 3000.0f

TEST: isValid_after_update_returns_true
  When:  aggregator.update(PID_RPM, 3000.0f)
  Expected: aggregator.isValid(PID_RPM) == true

TEST: update_overwrites_previous_value
  When:  aggregator.update(PID_RPM, 1000.0f)
         aggregator.update(PID_RPM, 2000.0f)
  Expected: aggregator.get(PID_RPM) == 2000.0f

TEST: allRequiredPidsReceived_false_when_partial
  When:  only PID_RPM and PID_SPEED updated
  Expected: allRequiredPidsReceived() == false

TEST: allRequiredPidsReceived_true_when_all_present
  When:  PID_RPM, PID_SPEED, PID_FUEL_RATE all updated
  Expected: allRequiredPidsReceived() == true

TEST: update_mil_status_stored_and_retrieved
  When:  updateMilStatus(true)
  Expected: getMilStatus() == true

TEST: update_dtc_count_stored_and_retrieved
  When:  updateDtcCount(3)
  Expected: getDtcCount() == 3

TEST: reset_invalidates_all_values
  When:  update several PIDs, then reset()
  Expected: isValid() == false for all PIDs
            get() == 0.0f for all PIDs
            getMilStatus() == false
            getDtcCount() == 0
```

---

### Suite: `test_derived_calculator`

```
TEST: compute_normal_conditions
  Given: aggregator with PID_SPEED = 100.0f, PID_FUEL_RATE = 5.0f
  Expected: result == 100.0f / 5.0f == 20.0f

TEST: compute_zero_speed_returns_zero
  Given: PID_SPEED = 0.0f, PID_FUEL_RATE = 5.0f
  Expected: result == 0.0f

TEST: compute_zero_fuel_rate_returns_zero
  Given: PID_SPEED = 100.0f, PID_FUEL_RATE = 0.0f
  Expected: result == 0.0f

TEST: compute_both_zero_returns_zero
  Given: PID_SPEED = 0.0f, PID_FUEL_RATE = 0.0f
  Expected: result == 0.0f

TEST: compute_negative_speed_returns_zero
  Given: PID_SPEED = -10.0f, PID_FUEL_RATE = 5.0f
  Expected: result == 0.0f

TEST: compute_negative_fuel_rate_returns_zero
  Given: PID_SPEED = 100.0f, PID_FUEL_RATE = -1.0f
  Expected: result == 0.0f

TEST: compute_low_speed_high_fuel_returns_low_value
  Given: PID_SPEED = 10.0f, PID_FUEL_RATE = 10.0f
  Expected: result == 1.0f
```

---

### Suite: `test_session_accumulator`

```
TEST: initial_distance_is_zero
  Expected: session.getDistanceKm() == 0.0f

TEST: initial_fuel_is_zero
  Expected: session.getTotalFuelL() == 0.0f

TEST: initial_avg_consumption_is_zero
  Expected: session.getAvgConsumptionKmPerL() == 0.0f

TEST: update_accumulates_distance_correctly
  Given: speed = 100.0 km/h, delta_ms = 3600000 (1 hour)
  Expected: getDistanceKm() == 100.0f  (within 0.01f tolerance)

TEST: update_accumulates_fuel_correctly
  Given: fuel_rate = 8.0 L/h, delta_ms = 3600000 (1 hour)
  Expected: getTotalFuelL() == 8.0f  (within 0.001f tolerance)

TEST: avg_consumption_computed_correctly
  Given: after accumulating 100 km and 8 L
  Expected: getAvgConsumptionKmPerL() == 100.0f / 8.0f == 12.5f

TEST: update_small_delta_accumulates_correctly
  Given: speed = 60.0 km/h, delta_ms = 100 (broadcast interval)
  Expected: getDistanceKm() ≈ 60.0f * (100.0f / 3_600_000.0f) == 0.001666f

TEST: multiple_updates_accumulate_correctly
  Given: 3600 calls with speed=60.0, fuel_rate=6.0, delta_ms=1000
  Expected: getDistanceKm() ≈ 60.0f  (within 0.1f)
            getTotalFuelL() ≈ 6.0f   (within 0.01f)

TEST: zero_speed_does_not_accumulate_distance
  Given: speed = 0.0f, fuel_rate = 0.5f, delta_ms = 100000
  Expected: getDistanceKm() == 0.0f

TEST: zero_fuel_does_not_accumulate_fuel
  Given: speed = 50.0f, fuel_rate = 0.0f, delta_ms = 100000
  Expected: getTotalFuelL() == 0.0f

TEST: negative_speed_treated_as_zero
  Given: speed = -10.0f, fuel_rate = 2.0f, delta_ms = 100000
  Expected: getDistanceKm() == 0.0f

TEST: reset_clears_all_accumulators
  When:  update called several times, then reset()
  Expected: getDistanceKm() == 0.0f
            getTotalFuelL() == 0.0f
            getAvgConsumptionKmPerL() == 0.0f
```

---

### Suite: `test_payload_builder`

```
TEST: build_sets_version_correctly
  Expected: payload.version == PAYLOAD_VERSION

TEST: build_copies_rpm_from_aggregator
  Given: aggregator.update(PID_RPM, 3200.0f)
  Expected: payload.rpm == 3200

TEST: build_copies_speed_from_aggregator
  Given: aggregator.update(PID_SPEED, 80.0f)
  Expected: payload.speed_kmh == 80

TEST: build_copies_fuel_rate_from_aggregator
  Given: aggregator.update(PID_FUEL_RATE, 6.5f)
  Expected: payload.fuel_rate_l_per_h == 6.5f  (within float tolerance)

TEST: build_copies_instantaneous_consumption
  Given: consumption argument = 12.3f
  Expected: payload.consumption_km_per_l == 12.3f

TEST: build_copies_avg_consumption_from_session
  Given: session with avg = 14.0f
  Expected: payload.avg_consumption_km_per_l == 14.0f

TEST: build_copies_distance_from_session
  Given: session with distance = 42.5f km
  Expected: payload.distance_km == 42.5f

TEST: build_copies_mil_status_from_aggregator
  Given: aggregator.updateMilStatus(true)
  Expected: payload.mil_on == true

TEST: build_copies_dtc_count_from_aggregator
  Given: aggregator.updateDtcCount(2)
  Expected: payload.dtc_count == 2

TEST: build_sets_data_valid_flag_when_all_pids_present
  Given: PID_RPM, PID_SPEED, PID_FUEL_RATE all updated
  Expected: (payload.flags & PAYLOAD_FLAG_DATA_VALID) != 0

TEST: build_clears_data_valid_flag_when_pids_missing
  Given: only PID_RPM updated
  Expected: (payload.flags & PAYLOAD_FLAG_DATA_VALID) == 0

TEST: build_sets_engine_running_flag_when_rpm_above_400
  Given: aggregator.update(PID_RPM, 800.0f)
  Expected: (payload.flags & PAYLOAD_FLAG_ENGINE_RUNNING) != 0

TEST: build_clears_engine_running_flag_when_rpm_is_zero
  Given: aggregator.update(PID_RPM, 0.0f)
  Expected: (payload.flags & PAYLOAD_FLAG_ENGINE_RUNNING) == 0

TEST: build_copies_timestamp
  Given: timestamp_ms = 12345
  Expected: payload.timestamp_ms == 12345
```

---

### Suite: `test_brightness_controller`

Uses `MockDisplay`.

```
TEST: initial_level_is_2
  Expected: controller.getCurrentLevel() == 2
  Expected: controller.getCurrentPercent() == 75

TEST: onTouch_advances_level_from_2_to_3
  When:  onTouch() called once
  Expected: getCurrentLevel() == 3
            getCurrentPercent() == 100

TEST: onTouch_wraps_from_level_3_to_0
  When:  controller at level 3, onTouch() called
  Expected: getCurrentLevel() == 0
            getCurrentPercent() == 25

TEST: onTouch_calls_setBacklightPercent_on_display
  When:  onTouch() called
  Expected: mock_display.last_percent == controller.getCurrentPercent()
            mock_display.call_count == 1

TEST: applyInitial_calls_setBacklightPercent_with_75
  When:  applyInitial() called on fresh controller
  Expected: mock_display.last_percent == 75
            mock_display.call_count == 1

TEST: four_touches_return_to_original_level
  When:  onTouch() called 4 times from initial state
  Expected: getCurrentLevel() == 2

TEST: level_percents_are_25_50_75_100
  Expected: LEVEL_PERCENTS[0] == 25
            LEVEL_PERCENTS[1] == 50
            LEVEL_PERCENTS[2] == 75
            LEVEL_PERCENTS[3] == 100

TEST: onTouch_does_not_call_begin_on_display
  Expected: mock_display.begin_called == false after any number of onTouch() calls
```

---

### Suite: `test_espnow_receiver`

Uses `mock_espnow.h` to simulate incoming receive callbacks.

```
TEST: valid_payload_invokes_callback
  Given: valid Payload (correct version, correct size, PAYLOAD_FLAG_DATA_VALID set)
  When:  simulateReceive() called
  Expected: callback invoked exactly once

TEST: callback_receives_correct_rpm
  Given: payload.rpm = 2500
  Expected: callback's payload argument has rpm == 2500

TEST: wrong_version_does_not_invoke_callback
  Given: payload.version = PAYLOAD_VERSION + 1
  Expected: callback not invoked

TEST: wrong_size_does_not_invoke_callback
  Given: len = sizeof(Payload) - 1
  Expected: callback not invoked

TEST: missing_data_valid_flag_does_not_invoke_callback
  Given: payload.flags = 0
  Expected: callback not invoked

TEST: two_valid_payloads_invoke_callback_twice
  When:  simulateReceive() called twice with valid payloads
  Expected: callback invoked exactly twice

TEST: null_callback_does_not_crash_on_receive
  Given: setCallback(nullptr) called
  When:  simulateReceive() called with valid payload
  Expected: no crash, no callback invoked
```

---

### Suite: `test_server_connection_monitor`

Tests `ServerConnectionMonitor` timeout logic, online/offline transitions, and callback behaviour. No hardware or ESP-NOW mock required — all inputs are explicit `now_ms` timestamps.

```
TEST: isOnline_returns_false_before_first_payload
  Given: fresh ServerConnectionMonitor(timeout_ms = 2000)
  Expected: isOnline() == false

TEST: isOnline_returns_true_after_first_payload
  When:  onPayloadReceived(now_ms = 1000)
         tick(now_ms = 1000)
  Expected: isOnline() == true

TEST: remains_online_within_timeout
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 1999)
  Expected: isOnline() == true

TEST: goes_offline_when_timeout_elapses
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 2000)
  Expected: isOnline() == false

TEST: callback_fired_once_on_online_transition
  When:  tick(now_ms = 0)                  // still offline
         onPayloadReceived(now_ms = 100)
         tick(now_ms = 100)                // → online
         tick(now_ms = 200)               // still online
  Expected: StatusChangeCallback invoked exactly once with online == true

TEST: callback_fired_once_on_offline_transition
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 0)                  // online
         tick(now_ms = 2000)              // → offline
         tick(now_ms = 2100)              // still offline
  Expected: StatusChangeCallback invoked exactly once with online == false

TEST: callback_fired_on_recovery_after_offline
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 0)                  // online
         tick(now_ms = 2000)              // → offline (callback: false)
         onPayloadReceived(now_ms = 2500)
         tick(now_ms = 2500)              // → online again (callback: true)
  Expected: callback invoked twice total: first false, then true

TEST: no_callback_when_not_registered
  Given: setStatusChangeCallback(nullptr)
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 0)
         tick(now_ms = 2000)
  Expected: no crash

TEST: custom_timeout_respected
  Given: ServerConnectionMonitor(timeout_ms = 500)
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 499)
  Expected: isOnline() == true
  When:  tick(now_ms = 500)
  Expected: isOnline() == false

TEST: repeated_payloads_keep_server_online
  When:  onPayloadReceived(now_ms = 0)
         tick(now_ms = 100)
         onPayloadReceived(now_ms = 100)
         tick(now_ms = 1900)
         onPayloadReceived(now_ms = 1900)
         tick(now_ms = 3800)
  Expected: isOnline() == true  (last payload at 1900, timeout at 3900)
```

---

## Coverage Goals

| Module | Target |
|---|---|
| `PIDTranslator` | 100% |
| `PIDDictionary` | 100% |
| `DataAggregator` | 100% |
| `DerivedCalculator` | 100% |
| `SessionAccumulator` | 100% |
| `PayloadBuilder` | 100% |
| `BrightnessController` | 100% |
| `ESPNowReceiver` (logic only) | 90% |
| `ServerConnectionMonitor` | 100% |
| `SimulationDataGenerator` | 80% |
| `CANDriver` | excluded (hardware) |
| `CYDDisplay` | excluded (hardware) |
| `CYDScreenController` | excluded (LVGL/hardware) |

---

## Running Tests

```bash
# From repo root
cd test
pio test -e native_tests
```

All tests must pass on the host before any code is flashed to hardware.
