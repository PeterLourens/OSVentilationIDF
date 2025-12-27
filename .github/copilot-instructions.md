# GitHub Copilot instructions for OSVentilationIDF

Quick, actionable notes to help an AI coding agent get productive in this repository.

## Big picture
- This is an ESP-IDF C project for an environment ventilation controller running on an ESP32. Key pieces:
  - Firmware entry point: `main/main.c` (initialization sequence: mutexes → NVS → LittleFS → I2C → load configs → start tasks)
  - Web UI embedded into firmware (`/web` files are embedded at build time via `EMBED_TXTFILES` in `main/CMakeLists.txt`) and served by `main/http_functions.c`.
  - Persistent runtime configuration lives on LittleFS under `/data/json` (packaged into the image via `littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT`) and loaded by functions in `main/config_files.c`.
  - Runtime components: Wi‑Fi, HTTP/WebSocket server, statemachine (config-driven), I2C sensors, LED control, syslog over network.

## How to build, flash, and monitor (developer workflow) ✅
- Setup ESP-IDF environment (source export script for your ESP-IDF install) then use `idf.py`:
  - `idf.py menuconfig` — adjust SDK options
  - `idf.py build` — build the project
  - `idf.py -p /dev/ttyUSB0 flash` — flash to device (replace device path)
  - `idf.py -p /dev/ttyUSB0 monitor` — view logs
- Embedded LittleFS data: files in `data/json/` are included in the `storage` partition image automatically at build time. To change embedded web UI or default configs, edit files in `web/` or `data/json/` and re-run `idf.py build`.

## Key conventions and patterns (do not assume generic behavior) 🔧
- Config storage
  - JSON configs are read from `/data/json/*` on LittleFS. Example files: `data/json/network_config.json`, `bus0_sensor_config.json`, `state0_config.json`.
  - `load_*` functions in `main/config_files.c` (e.g., `load_network_config`, `load_state_config`, `load_sensor_config`) parse and validate JSON and update in-memory globals while protecting access with mutexes (e.g., `network_config_mutex`, `sensor_cfg_mutex`, `state_config_mutex`).
  - State files: `read_state_configs_from_json()` matches files by pattern (used with `state*_config.json` and similar) and supports root object or array.
  - Note on `duration_minutes`: this field is **reused** in two ways — as the *minimum hold time* for normal states (e.g., `day`, `night`) and as the *per-cycle active duration* for cycling states (`cyclingday`, `cyclingnight`). See `main/statemachine.c` (`day_transitions`) for the minimum-state-time usage and `main/statemachine_helper.c` (`valve_cycle_times_day` and `valve_cycle_times_night`) for cycling logic (these functions enforce a 60‑minute minimum interval and cap cycles to the overlap with configured day/night windows).
- Web server and config management
  - `main/http_functions.c` serves embedded web pages and exposes POST endpoints to write JSON configs atomically (e.g., `network_config_post_handler`, `sensorconfig_bus0_post_handler`). After write, handlers call the corresponding `load_*` to reload runtime state.
  - WebSocket messages accepted by the server include short commands such as `Start`/`Stop` (redirect logs to websocket), `stats`, `index`, `sensor_config`, `config`, `statemachine` — useful for testing and remote control.
- JSON parsing flexibility
  - The code uses tolerant parsers (`parse_bool_flexible`, `parse_int_flexible`) allowing booleans, numbers, or strings (`"On"`, `"true"`, `"1"`, etc.). Follow this leniency when generating config JSON.
- Naming & style
  - C style: snake_case function names, `esp_err_t` error returns, logging via `ESP_LOGx(TAG, ...)` with `static const char *TAG` in each file.
- Resource management & memory
  - Many handlers use `heap_caps_malloc`/`malloc` and expect the caller to free; fragile paths check for allocation failures and return appropriate HTTP/ESP errors — preserve these checks when changing code.

## Integration points & external dependencies ⚙️
- Declared in `main/idf_component.yml`: `joltwallet/littlefs` and `espressif/led_strip` (plus standard esp-idf subsystems like NVS, Wi‑Fi, HTTP server, cJSON).
- Partition layout: `partitions.csv` and `littlefs_create_partition_image(storage ../data FLASH_IN_PROJECT)` in `main/CMakeLists.txt` control the storage partition.

## Debugging tips
- Use `idf.py monitor` for logs; note that the firmware can redirect logs to a websocket (via `esp_log_set_vprintf(websocket_log_output)`) when `Start` is sent from the web UI/websocket.
- To inspect or modify runtime config without reflashing, use the web UI (`/sensor_config.html`, `/config.html`, `/statemachine.html`) which POSTs updates to `/data/json/*.json` and triggers reloads.

## Files to inspect for common tasks (quick references) 📌
- Initialization & main loop: `main/main.c`
- HTTP routes, WebSocket, config POST handling: `main/http_functions.c`
- Config parsing and state loader: `main/config_files.c`
- Web assets & embed rules: `main/CMakeLists.txt` (see `EMBED_TXTFILES` and `EMBED_FILES`)
- Default JSONs included in build: `data/json/*`

## Tests & CI
- `sdkconfig` includes Unity testing support flags (Unity is available in ESP-IDF), but there are no unit tests in this repo. If adding tests, follow ESP-IDF Unity conventions and update `sdkconfig` / `CMakeLists.txt` accordingly.

## Do / Don't (agent guidance) 💡
- DO: Preserve mutex semantics when editing config load/modify code — updates must be atomic and protected.
- DO: Use `write_json_atomic_fs()` / existing atomic write helpers when writing files to LittleFS to avoid corrupt configs.
- DO: When editing embedded web assets, rebuild the firmware (`idf.py build`) to re-embed assets.
- DON'T: Assume config values are strict typed JSON — code accepts string representations; follow existing flexible parsing.

---

If any section is unclear or you want examples added (e.g., a minimal new-state JSON template or a small how-to for adding a web route), tell me which parts to expand and I’ll iterate. Thanks!