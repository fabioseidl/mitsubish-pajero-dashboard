# Architecture

## System Overview

```
[ Vehicle CAN Bus ]
        |
   OBD-II Port
        |
  [ MCP2515 + TJA1050 ] (SPI)
        |
  [ ESP32 SERVER ]              [ ESP32 SERVER EMULATOR ]
    - CAN frame reading            - Synthetic data generation
    - PID translation              - Same broadcast logic
    - Derived computation          - No CAN dependency
    - Session accumulation
    - ESP-NOW broadcast (PMK)
        |                                   |
        +-----------------------------------+
                        |
                 (ESP-NOW, 2.4GHz)
                        |
              __________+__________
             |                     |
       [ Client A ]          [ Client B ] ...
         CYD 2.4"              (future)
         LVGL                  LVGL
```

---

## Component Responsibilities

### Server (`projects/server`)

| Responsibility | Description |
|---|---|
| CAN reading | Poll MCP2515 via SPI; receive raw CAN frames at 500 kbps |
| Frame filtering | Accept only frames matching known PIDs in `pid_map.h` |
| PID translation | Convert raw bytes to engineering values per formula in `pid_map.h` |
| Bitmask extraction | PID 0x01: extract `mil_on` and `dtc_count` from raw 4-byte response |
| Session accumulation | Integrate speed and fuel rate over time for distance and avg consumption |
| Derived computation | Calculate instantaneous consumption from speed and fuel rate |
| Broadcast | Serialize `Payload` and send via ESP-NOW at 10 Hz |

### Server Emulator (`projects/server_emulator`)

| Responsibility | Description |
|---|---|
| Synthetic data | Generate realistic sinusoidal values for all `Payload` fields |
| Scenario simulation | Support idle, city, and highway driving profiles |
| Broadcast | Reuse `ESPNowBroadcaster` from `lib/core`; same 10 Hz rate and PMK |

### Shared Library (`lib/core`)

| Responsibility | Description |
|---|---|
| Payload definition | `Payload` struct shared by server, emulator, and all clients |
| PID map | `pid_map.h` — hand-maintained, all known PIDs with formulas |
| ESP-NOW abstraction | `ESPNowBroadcaster` and `ESPNowReceiver` base classes |
| Display abstraction | `IDisplay` interface decoupling LVGL from hardware |
| Brightness control | Touch event → 4-level brightness cycle logic |
| Connection monitoring | `ServerConnectionMonitor` — timeout-based online/offline detection for clients |

### Client (`projects/client_cyd`)

| Responsibility | Description |
|---|---|
| ESP-NOW receive | Uses `ESPNowReceiver` from `lib/core` |
| Screen rendering | Builds LVGL widgets; updates on each received `Payload` |
| Brightness | Registers touch callback via `BrightnessController` |
| Connection monitoring | Tracks last payload timestamp via `ServerConnectionMonitor`; triggers UI update on online/offline transition |

---

## Communication Protocol — ESP-NOW

- Mode: broadcast (`FF:FF:FF:FF:FF:FF`)
- Security: PMK (16-byte compile-time constant, shared by all nodes)
- Direction: server → clients only (unidirectional)
- Payload: fixed-size packed `Payload` C struct
- Transmission rate: 10 Hz (every 100 ms)

### PMK Provisioning

The PMK is defined in `lib/core/include/security_config.h`. This file is listed in `.gitignore` and never committed. A template `security_config.h.example` is committed instead. Every development machine must create `security_config.h` from the template before building.

---

## Data Flow — Server

```
[can_rx_task — Core 1, Priority 5]

  CANDriver::readFrame()
        |
        v
  PIDDictionary::lookup(can_id, pid_code)
        |
        v (if found)
  PIDTranslator::translate(frame, definition)
        |
        v
  DataAggregator::update(pid_id, value)


[broadcast_task — Core 0, Priority 3]

  Every 100ms:
        |
        v
  DerivedCalculator::computeConsumption(aggregator)
        |
        v
  SessionAccumulator::update(speed, fuel_rate, delta_ms)
        |
        v
  PayloadBuilder::build(aggregator, session, timestamp_ms)
        |
        v
  ESPNowBroadcaster::send(payload)
```

---

## Data Flow — Server Emulator

```
[simulation_task — Core 0, Priority 3]

  Every 100ms:
        |
        v
  SimulationDataGenerator::tick(delta_ms)
        |
        v
  SimulationDataGenerator::getPayload()
        |
        v
  ESPNowBroadcaster::send(payload)
```

---

## Data Flow — Client

```
[ESP-NOW ISR callback]
        |
        v
  ESPNowReceiver::onReceiveISR(mac, data, len)
        |
        v (validated)
  PayloadCallback invoked with deserialized Payload
        |
        +-------------------------------------------+
        |                                           |
        v                                           v
  ServerConnectionMonitor::onPayloadReceived(now_ms)   IScreenController::onPayloadReceived(payload)
                                                    |
                                                    v
                                             LVGL widget updates

[main loop]
  ServerConnectionMonitor::tick(now_ms)
        |
        v (on transition)
  IScreenController::onServerStatusChanged(online)
        |
        v
  server_status_label_ updated (ONLINE / OFFLINE)

  IScreenController::tick()
        |
        v
  lv_timer_handler()
  XPT2046 touch poll → BrightnessController::onTouch()
```

---

## FreeRTOS Tasks — Server

| Task | Core | Priority | Stack | Description |
|---|---|---|---|---|
| `can_rx_task` | 1 | 5 | 4096 | Reads CAN frames, updates aggregator |
| `broadcast_task` | 0 | 3 | 4096 | Computes derived values, builds and sends payload |

The `DataAggregator` is shared between tasks and protected by a FreeRTOS mutex.

---

## Derived Value Formulas

### Instantaneous Consumption (km/L)

Requires PID `0x5E` (Fuel Rate, L/h) and PID `0x0D` (Speed, km/h):

```
if speed_km_h > 0.0 AND fuel_rate_l_per_h > 0.0:
    consumption_km_per_l = speed_km_h / fuel_rate_l_per_h
else:
    consumption_km_per_l = 0.0
```

### Session Distance (km)

Integrated every broadcast tick (100 ms = 0.1 s = 1/36000 h):

```
delta_distance_km = speed_km_h * (delta_ms / 3_600_000.0f)
total_distance_km += delta_distance_km
```

### Session Average Consumption (km/L)

```
delta_fuel_l = fuel_rate_l_per_h * (delta_ms / 3_600_000.0f)
total_fuel_l += delta_fuel_l

if total_fuel_l > 0.0:
    avg_consumption_km_per_l = total_distance_km / total_fuel_l
else:
    avg_consumption_km_per_l = 0.0
```

All session values reset on server reboot. There is no persistent storage.
