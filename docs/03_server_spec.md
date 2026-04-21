# Server Specification

## Projects: `projects/server` and `projects/server_emulator`

---

## Shared Data Structures

### `CANFrame`

Plain data struct representing a single CAN bus frame. No methods.

```cpp
// lib/core/include/can_frame.h

struct CANFrame {
    uint32_t id;          // CAN frame ID (11-bit or 29-bit)
    uint8_t  data[8];     // Frame payload bytes
    uint8_t  dlc;         // Data Length Code (0–8)
    bool     is_extended; // true if 29-bit extended ID
};
```

---

## Server Classes

### `ICANDriver` (interface)

Abstract interface for CAN bus access. Exists solely to enable host-side testing via mock injection without requiring MCP2515 hardware.

```cpp
// projects/server/include/ican_driver.h

class ICANDriver {
public:
    virtual ~ICANDriver() = default;

    /**
     * Initializes SPI and MCP2515 controller.
     *
     * @return true  Initialization succeeded; driver ready to receive frames.
     * @return false Initialization failed (SPI error, MCP2515 not responding).
     */
    virtual bool begin() = 0;

    /**
     * Checks whether at least one CAN frame is available in the RX buffer.
     *
     * @return true  One or more frames are available.
     * @return false RX buffer is empty.
     */
    virtual bool isFrameAvailable() = 0;

    /**
     * Reads one frame from the MCP2515 RX buffer.
     *
     * @param out_frame Reference to a CANFrame that will be populated.
     * @return true  Frame was read successfully; out_frame contains valid data.
     * @return false Buffer was empty or a read error occurred; out_frame is unchanged.
     */
    virtual bool readFrame(CANFrame& out_frame) = 0;
};
```

---

### `CANDriver : ICANDriver`

Concrete MCP2515 implementation. Wraps the hardware SPI driver.

```cpp
// projects/server/include/can_driver.h

class CANDriver : public ICANDriver {
public:
    /**
     * @param cs_pin  GPIO number for MCP2515 chip select (SPI CS).
     * @param int_pin GPIO number for MCP2515 interrupt line (active low).
     */
    explicit CANDriver(uint8_t cs_pin, uint8_t int_pin);

    /**
     * Initializes SPI bus and configures MCP2515 for 500 kbps, normal mode.
     * Must be called before any readFrame() calls.
     *
     * @return true  MCP2515 responded and is configured.
     * @return false MCP2515 did not acknowledge initialization.
     */
    bool begin() override;

    /**
     * Polls the MCP2515 INT pin state to determine frame availability.
     * Does not perform a full SPI transaction.
     *
     * @return true  INT pin is asserted (frame available).
     * @return false INT pin is deasserted (no frame).
     */
    bool isFrameAvailable() override;

    /**
     * Performs a full SPI read of one frame from the MCP2515 RX buffer.
     *
     * @param out_frame Populated with the received frame on success.
     * @return true  Frame read successfully.
     * @return false No frame available or SPI error.
     */
    bool readFrame(CANFrame& out_frame) override;

private:
    uint8_t cs_pin_;
    uint8_t int_pin_;
};
```

---

### `PIDDictionary`

Provides lookup from `(can_id, pid_code)` pairs to `PidDefinition` entries in `PID_MAP`. Backed entirely by compile-time data; no dynamic allocation.

OBD-II responses always arrive on CAN ID `0x7E8` (primary ECU). The PID code is embedded at byte index 2 of the frame data.

```cpp
// projects/server/include/pid_dictionary.h

class PIDDictionary {
public:
    PIDDictionary() = default;

    /**
     * Looks up a PID definition by CAN frame ID and PID code.
     *
     * @param can_id   CAN frame ID of the response (typically 0x7E8).
     * @param pid_code OBD-II PID code extracted from frame data byte 2.
     * @return Pointer to the matching PidDefinition in PID_MAP,
     *         or nullptr if no match is found.
     */
    const PidDefinition* lookup(uint32_t can_id, uint8_t pid_code) const;

    /**
     * Checks whether a CAN ID is a known OBD-II response identifier.
     *
     * @param can_id CAN frame ID to check.
     * @return true  The ID is a known ECU response address.
     * @return false The ID is not recognized.
     */
    bool isKnownId(uint32_t can_id) const;

    /**
     * Returns the total number of entries in PID_MAP.
     *
     * @return PID_MAP_SIZE constant from pid_map.h.
     */
    size_t size() const;
};
```

---

### `PIDTranslator`

Stateless class. Converts raw CAN frame bytes to engineering float values using the formula encoded in a `PidDefinition`. Handles the `FORMULA_BITMASK` case for PID `0x01` separately.

```cpp
// projects/server/include/pid_translator.h

class PIDTranslator {
public:
    /**
     * Translates a CAN frame's data bytes into a float engineering value
     * using the formula defined in the PidDefinition.
     *
     * Applies: value = (byte_A * def.a_mult + byte_B * def.b_mult) * def.scale + def.offset
     *
     * For FORMULA_BITMASK and FORMULA_COMPLEX, delegates to specialized methods.
     *
     * @param frame CAN frame containing raw data bytes.
     * @param def   PidDefinition describing formula, scale, and offset.
     * @return Translated engineering value as float.
     *         Returns 0.0f for unsupported formula types.
     */
    static float translate(const CANFrame& frame, const PidDefinition& def);

    /**
     * Extracts MIL on/off status from PID 0x01 response.
     * MIL is encoded in bit 7 of byte A (frame.data[3]).
     *
     * @param frame CAN frame for PID 0x01.
     * @return true  MIL is active (check engine light on).
     * @return false MIL is not active.
     */
    static bool extractMilStatus(const CANFrame& frame);

    /**
     * Extracts the DTC count from PID 0x01 response.
     * DTC count is encoded in bits 0–6 of byte A (frame.data[3]).
     *
     * @param frame CAN frame for PID 0x01.
     * @return Number of stored DTCs (0–127).
     */
    static uint8_t extractDtcCount(const CANFrame& frame);

private:
    /**
     * Applies linear formula to raw frame bytes.
     *
     * @param frame CAN frame.
     * @param def   PidDefinition with linear formula parameters.
     * @return Computed float value.
     */
    static float applyLinearFormula(const CANFrame& frame, const PidDefinition& def);
};
```

---

### `DataAggregator`

Holds the latest valid value for each PID. Thread-safe: written by `can_rx_task` and read by `broadcast_task` via FreeRTOS mutex.

```cpp
// projects/server/include/data_aggregator.h

class DataAggregator {
public:
    DataAggregator();
    ~DataAggregator();

    /**
     * Stores the latest translated value for a given PID.
     * Marks the PID as valid. Thread-safe.
     *
     * @param pid   PID code (e.g. PID_RPM).
     * @param value Translated engineering value to store.
     */
    void update(uint16_t pid, float value);

    /**
     * Stores MIL status extracted from PID 0x01 bitmask.
     *
     * @param mil_on true if MIL is active.
     */
    void updateMilStatus(bool mil_on);

    /**
     * Stores DTC count extracted from PID 0x01 bitmask.
     *
     * @param count Number of stored DTCs.
     */
    void updateDtcCount(uint8_t count);

    /**
     * Retrieves the last stored value for a PID.
     * Thread-safe.
     *
     * @param pid PID code.
     * @return Last stored float value, or 0.0f if never updated.
     */
    float get(uint16_t pid) const;

    /**
     * Returns whether a PID has received at least one valid update.
     *
     * @param pid PID code.
     * @return true  PID has been updated at least once.
     * @return false PID has never been updated.
     */
    bool isValid(uint16_t pid) const;

    /**
     * Returns the last stored MIL status.
     *
     * @return true if MIL was last reported as active.
     */
    bool getMilStatus() const;

    /**
     * Returns the last stored DTC count.
     *
     * @return DTC count, or 0 if never updated.
     */
    uint8_t getDtcCount() const;

    /**
     * Returns true when all PIDs required for a valid broadcast
     * (PID_RPM, PID_SPEED, PID_FUEL_RATE) have received at least one update.
     *
     * @return true  All required PIDs are valid.
     * @return false One or more required PIDs have not been received.
     */
    bool allRequiredPidsReceived() const;

    /**
     * Clears all stored values and marks all PIDs as invalid.
     * Thread-safe. Intended for testing; not called during normal operation.
     */
    void reset();

private:
    // Storage indexed by PID code. Covers all PIDs in PID_MAP.
    float   values_[256];
    bool    valid_[256];
    bool    mil_on_;
    uint8_t dtc_count_;
    // FreeRTOS mutex protecting values_, valid_, mil_on_, dtc_count_
    void*   mutex_; // SemaphoreHandle_t — void* to avoid ESP-IDF header in interface
};
```

---

### `DerivedCalculator`

Stateless. Computes instantaneous fuel consumption from aggregated sensor values.

```cpp
// projects/server/include/derived_calculator.h

class DerivedCalculator {
public:
    /**
     * Computes instantaneous fuel consumption in km/L.
     *
     * Formula: consumption = speed_km_h / fuel_rate_l_per_h
     *
     * Returns 0.0 if speed or fuel rate is zero or negative,
     * to avoid division by zero and invalid states (e.g. engine off).
     *
     * @param aggregator Current DataAggregator holding PID_SPEED and PID_FUEL_RATE.
     * @return Instantaneous consumption in km/L, or 0.0f if not computable.
     */
    static float computeConsumption(const DataAggregator& aggregator);
};
```

---

### `SessionAccumulator`

Maintains time-integrated session totals for distance and fuel consumed since server start. Not thread-safe — called only from `broadcast_task`.

```cpp
// projects/server/include/session_accumulator.h

class SessionAccumulator {
public:
    SessionAccumulator();

    /**
     * Integrates speed and fuel rate over a time delta to accumulate
     * session distance and fuel consumed.
     *
     * distance_delta = speed_km_h * (delta_ms / 3_600_000.0f)
     * fuel_delta     = fuel_rate_l_per_h * (delta_ms / 3_600_000.0f)
     *
     * Negative or zero values for speed or fuel_rate are treated as 0.0.
     *
     * @param speed_km_h       Current vehicle speed in km/h.
     * @param fuel_rate_l_per_h Current fuel rate in L/h.
     * @param delta_ms         Elapsed time since last update in milliseconds.
     */
    void update(float speed_km_h, float fuel_rate_l_per_h, uint32_t delta_ms);

    /**
     * Returns accumulated distance since session start.
     *
     * @return Total distance in km. Always >= 0.0f.
     */
    float getDistanceKm() const;

    /**
     * Returns total fuel consumed since session start.
     *
     * @return Total fuel in litres. Always >= 0.0f.
     */
    float getTotalFuelL() const;

    /**
     * Returns session average fuel consumption.
     *
     * Formula: avg = total_distance_km / total_fuel_l
     *
     * @return Average consumption in km/L, or 0.0f if total fuel is zero.
     */
    float getAvgConsumptionKmPerL() const;

    /**
     * Resets all accumulators to zero.
     * Called on server start. Not called during normal operation.
     */
    void reset();

private:
    float    total_distance_km_;
    float    total_fuel_l_;
};
```

---

### `PayloadBuilder`

Stateless. Assembles a `Payload` struct from the current aggregator and session state.

```cpp
// projects/server/include/payload_builder.h

class PayloadBuilder {
public:
    /**
     * Builds a fully populated Payload from current sensor and session data.
     *
     * Sets PAYLOAD_FLAG_DATA_VALID if aggregator.allRequiredPidsReceived().
     * Sets PAYLOAD_FLAG_ENGINE_RUNNING if RPM > 400.
     *
     * @param aggregator   Current DataAggregator with all translated PID values.
     * @param session      Current SessionAccumulator with distance and fuel totals.
     * @param consumption  Precomputed instantaneous consumption from DerivedCalculator.
     * @param timestamp_ms Server uptime in milliseconds (from esp_timer_get_time()/1000).
     * @return Fully populated Payload ready for broadcast.
     */
    static Payload build(const DataAggregator& aggregator,
                         const SessionAccumulator& session,
                         float consumption,
                         uint32_t timestamp_ms);
};
```

---

### `ESPNowBroadcaster`

Manages ESP-NOW initialization and outbound broadcast on the server and server emulator.

```cpp
// lib/core/include/espnow_broadcaster.h

class ESPNowBroadcaster {
public:
    ESPNowBroadcaster() = default;

    /**
     * Initializes Wi-Fi in station mode, initializes ESP-NOW,
     * and sets the PMK for encryption.
     *
     * Must be called once before any send() calls.
     *
     * @param pmk 16-byte Primary Master Key. Must match PMK on all clients.
     * @return true  ESP-NOW initialized and PMK set.
     * @return false Initialization failed (Wi-Fi or ESP-NOW error).
     */
    bool begin(const uint8_t pmk[16]);

    /**
     * Serializes the Payload and sends it via ESP-NOW broadcast.
     *
     * Destination address: FF:FF:FF:FF:FF:FF.
     * Non-blocking: actual transmission is asynchronous.
     *
     * @param payload The Payload struct to transmit.
     * @return true  esp_now_send() accepted the packet (does not guarantee delivery).
     * @return false esp_now_send() returned an error code.
     */
    bool send(const Payload& payload);

    /**
     * Returns the status of the most recent completed transmission.
     * Updated asynchronously by the registered ESP-NOW send callback.
     *
     * @return ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL.
     */
    esp_now_send_status_t lastSendStatus() const;

private:
    bool initialized_;
    esp_now_send_status_t last_send_status_;

    /**
     * Internal callback registered with esp_now_register_send_cb.
     * Updates last_send_status_.
     *
     * @param mac    MAC address of the destination (broadcast).
     * @param status Delivery status reported by ESP-NOW layer.
     */
    static void onSendComplete(const uint8_t* mac, esp_now_send_status_t status);
};
```

---

## Server Emulator Classes

### `SimulationDataGenerator`

Generates synthetic sensor data following configurable driving profiles. Produces a ready-to-broadcast `Payload` on each tick.

```cpp
// projects/server_emulator/include/simulation_data_generator.h

enum class DrivingProfile {
    IDLE,      // RPM ~800, speed 0, low fuel rate
    CITY,      // RPM 1000–3000, speed 0–60 km/h, sinusoidal variation
    HIGHWAY,   // RPM 1800–2500, speed 80–120 km/h, low variation
};

class SimulationDataGenerator {
public:
    /**
     * @param profile Initial driving profile to simulate.
     */
    explicit SimulationDataGenerator(DrivingProfile profile = DrivingProfile::CITY);

    /**
     * Advances the simulation clock and recomputes all sensor values
     * for the current driving profile.
     *
     * @param delta_ms Elapsed time since last tick in milliseconds.
     */
    void tick(uint32_t delta_ms);

    /**
     * Returns a fully populated Payload reflecting the current simulation state.
     * Includes session-accumulated distance and average consumption.
     *
     * @return Payload ready for broadcast via ESPNowBroadcaster.
     */
    Payload getPayload() const;

    /**
     * Changes the active driving profile.
     * Does not reset the session accumulator.
     *
     * @param profile New driving profile to simulate.
     */
    void setProfile(DrivingProfile profile);

private:
    DrivingProfile    profile_;
    float             elapsed_ms_;
    SessionAccumulator session_;

    float computeRpm() const;
    float computeSpeedKmh() const;
    float computeFuelRateLPerH(float rpm, float speed) const;
};
```

---

## Server Main Loop (pseudocode)

```cpp
// projects/server/src/main.cpp

void can_rx_task(void* param) {
    CANDriver     driver(PIN_CAN_CS, PIN_CAN_INT);
    PIDDictionary dictionary;

    driver.begin();

    while (true) {
        if (driver.isFrameAvailable()) {
            CANFrame frame;
            if (driver.readFrame(frame)) {
                if (frame.data[2] == PID_MONITOR_STATUS) {
                    aggregator.updateMilStatus(PIDTranslator::extractMilStatus(frame));
                    aggregator.updateDtcCount(PIDTranslator::extractDtcCount(frame));
                } else {
                    const PidDefinition* def = dictionary.lookup(frame.id, frame.data[2]);
                    if (def != nullptr) {
                        float value = PIDTranslator::translate(frame, *def);
                        aggregator.update(def->pid, value);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void broadcast_task(void* param) {
    SessionAccumulator session;
    ESPNowBroadcaster  broadcaster;
    uint32_t           last_tick_ms = 0;

    broadcaster.begin(PMK_KEY);

    while (true) {
        uint32_t now_ms    = esp_timer_get_time() / 1000;
        uint32_t delta_ms  = now_ms - last_tick_ms;
        last_tick_ms       = now_ms;

        float speed       = aggregator.get(PID_SPEED);
        float fuel_rate   = aggregator.get(PID_FUEL_RATE);
        float consumption = DerivedCalculator::computeConsumption(aggregator);

        session.update(speed, fuel_rate, delta_ms);

        Payload payload = PayloadBuilder::build(aggregator, session, consumption, now_ms);
        broadcaster.send(payload);

        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz
    }
}
```

---

## Server Emulator Main Loop (pseudocode)

```cpp
// projects/server_emulator/src/main.cpp

void emulator_task(void* param) {
    SimulationDataGenerator generator(DrivingProfile::CITY);
    ESPNowBroadcaster       broadcaster;
    uint32_t                last_tick_ms = 0;

    broadcaster.begin(PMK_KEY);

    while (true) {
        uint32_t now_ms   = esp_timer_get_time() / 1000;
        uint32_t delta_ms = now_ms - last_tick_ms;
        last_tick_ms      = now_ms;

        generator.tick(delta_ms);
        Payload payload = generator.getPayload();
        broadcaster.send(payload);

        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz
    }
}
```
