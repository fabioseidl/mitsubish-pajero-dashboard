# Folder Structure

## Monorepo Layout

```
car-dashboard/
|
|-- lib/
|   |-- core/
|       |-- include/
|       |   |-- can_frame.h
|       |   |-- payload.h
|       |   |-- pid_map.h
|       |   |-- i_display.h
|       |   |-- cyd_display.h
|       |   |-- brightness_controller.h
|       |   |-- espnow_broadcaster.h
|       |   |-- espnow_receiver.h
|       |   |-- i_screen_controller.h
|       |   |-- security_config.h         # GITIGNORED — copy from .example
|       |   |-- security_config.h.example # Committed template
|       |-- src/
|           |-- cyd_display.cpp
|           |-- brightness_controller.cpp
|           |-- espnow_broadcaster.cpp
|           |-- espnow_receiver.cpp
|
|-- projects/
|   |
|   |-- server/
|   |   |-- platformio.ini
|   |   |-- include/
|   |   |   |-- ican_driver.h
|   |   |   |-- can_driver.h
|   |   |   |-- pid_dictionary.h
|   |   |   |-- pid_translator.h
|   |   |   |-- data_aggregator.h
|   |   |   |-- derived_calculator.h
|   |   |   |-- session_accumulator.h
|   |   |   |-- payload_builder.h
|   |   |-- src/
|   |       |-- main.cpp
|   |       |-- can_driver.cpp
|   |       |-- pid_dictionary.cpp
|   |       |-- pid_translator.cpp
|   |       |-- data_aggregator.cpp
|   |       |-- derived_calculator.cpp
|   |       |-- session_accumulator.cpp
|   |       |-- payload_builder.cpp
|   |
|   |-- server_emulator/
|   |   |-- platformio.ini
|   |   |-- include/
|   |   |   |-- simulation_data_generator.h
|   |   |-- src/
|   |       |-- main.cpp
|   |       |-- simulation_data_generator.cpp
|   |
|   |-- client_cyd/
|       |-- platformio.ini
|       |-- include/
|       |   |-- cyd_screen_controller.h
|       |-- src/
|           |-- main.cpp
|           |-- cyd_screen_controller.cpp
|
|-- test/
|   |-- host/
|   |   |-- server/
|   |   |   |-- test_pid_translator.cpp
|   |   |   |-- test_pid_dictionary.cpp
|   |   |   |-- test_data_aggregator.cpp
|   |   |   |-- test_derived_calculator.cpp
|   |   |   |-- test_session_accumulator.cpp
|   |   |   |-- test_payload_builder.cpp
|   |   |-- lib/
|   |   |   |-- test_brightness_controller.cpp
|   |   |   |-- test_espnow_receiver.cpp
|   |   |-- mocks/
|   |       |-- mock_can_driver.h
|   |       |-- mock_display.h
|   |       |-- mock_espnow.h
|   |-- platformio.ini
|
|-- .gitignore
|-- README.md
```

---

## PlatformIO Configuration

### `projects/server/platformio.ini`

```ini
[env:server]
platform  = espressif32
board     = esp32dev
framework = espidf

lib_deps =
    coryjfowler/MCP_CAN_lib @ ^1.5.0

lib_extra_dirs = ../../lib

monitor_speed = 115200
```

### `projects/server_emulator/platformio.ini`

```ini
[env:server_emulator]
platform  = espressif32
board     = esp32dev
framework = espidf

lib_extra_dirs = ../../lib

monitor_speed = 115200
```

Note: `server_emulator` has no MCP_CAN_lib dependency — it produces no CAN traffic.

### `projects/client_cyd/platformio.ini`

```ini
[env:client_cyd]
platform  = espressif32
board     = esp32dev
framework = espidf

lib_deps =
    lvgl/lvgl @ ^8.3.0

lib_extra_dirs = ../../lib

monitor_speed = 115200
```

### `test/platformio.ini`

```ini
[env:native_tests]
platform = native

build_flags =
    -std=c++17
    -I../lib/core/include
    -I../projects/server/include
    -I../projects/server_emulator/include
    -DUNIT_TEST

test_framework = unity
test_filter    = host/*
```

Note: The `native` platform on macOS ARM64 (Apple M2) compiles to `aarch64`. All test assertions use fixed-width types (`uint8_t`, `uint16_t`, `float`) to avoid `int`/`long` size differences between ARM64 host and ESP32 target.

---

## `.gitignore` (relevant entries)

```gitignore
# Security keys — never commit
lib/core/include/security_config.h

# PlatformIO build artifacts
.pio/
```

---

## Setup Checklist (new development machine)

```bash
# 1. Clone the repository
git clone <repo_url>
cd car-dashboard

# 2. Create security config from template
cp lib/core/include/security_config.h.example \
   lib/core/include/security_config.h
# Then edit security_config.h and set a real PMK value

# 3. Build server
cd projects/server
pio run

# 4. Build server emulator
cd ../server_emulator
pio run

# 5. Build client
cd ../client_cyd
pio run

# 6. Run host tests
cd ../../test
pio test -e native_tests
```

---

## Notes

- `pid_map.h` is hand-maintained and committed to version control. It is not generated.
- `security_config.h` is machine-local and must never be committed. The `.example` file documents the required structure.
- Each sub-project is independently buildable and flashable. There is no root-level build step.
- `lib/core` is consumed by all three projects via `lib_extra_dirs`. PlatformIO resolves it automatically.
