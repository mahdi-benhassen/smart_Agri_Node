# SAGRI — Smart Agriculture IoT System Implementation Plan

## Overview
Build the complete, production-ready firmware for a Smart Agriculture IoT System consisting of:

- **Gateway (ESP32-S3):** Zigbee Coordinator + Wi-Fi/MQTT bridge + REST API + OTA manager
- **Field Nodes (ESP32-H2):** Zigbee End Devices with 7 sensor drivers, actuator control, deep-sleep
- **Common libraries:** Shared data model, Zigbee cluster definitions, CRC utilities
- **Tools:** Factory provisioning, OTA server, MQTT simulator
- **Documentation:** README, ARCHITECTURE, API, SECURITY

> [!IMPORTANT]
> This is a ~60+ file industrial-grade firmware project. All code must compile against ESP-IDF v5.2+, use no Arduino abstractions, contain no placeholder/stub functions, and follow the strict coding rules from the spec.

## User Review Required

> [!WARNING]
> **Project root location:** The code will be generated inside `c:\Users\G573442RTS\Desktop\workspace\Opencode\smart_Agri_Node\` using the exact directory structure from the spec. Please confirm this is the desired location.

> [!IMPORTANT]
> **Target hardware:** The spec calls for ESP32-S3 (Gateway) and ESP32-H2 (Field Node) as two separate firmware images. Each has its own `CMakeLists.txt` and `main.c`. The top-level CMake references both as sub-projects. Confirm you want both targets implemented.

## Proposed Changes
The implementation follows the exact execution order from the agent prompt (Section 8).

### Phase 1: Common Libraries (Foundation)
*These are shared between Gateway and Field Node — no hardware dependencies.*

- **[NEW]** `common/agri_data_model/agri_data_model.h`
  - `agri_sensor_data_t` struct (packed, version-tagged) — all sensor fields
  - `agri_cmd_t` struct — actuator command envelope
  - `agri_status_t` struct — device health/status
  - JSON codec: `agri_data_to_json()`, `agri_cmd_from_json()`
  - Schema version constant (`AGRI_SCHEMA_VER = 2`)
- **[NEW]** `common/agri_data_model/agri_data_model.c`
  - cJSON-based JSON encode/decode implementation
  - Field validation, bounds checking
- **[NEW]** `common/agri_data_model/CMakeLists.txt`
  - Component registration, REQUIRES: json (cJSON)
- **[NEW]** `common/agri_zigbee_clusters/agri_zigbee_clusters.h`
  - `AGRI_CLUSTER_ID` (0xFF00), Manufacturer ID (0x1234)
  - All 15 attribute ID defines (0x0001–0x000F) from tech spec Table 5
  - Attribute type mappings, access modes
  - Cluster init/register functions
- **[NEW]** `common/agri_zigbee_clusters/agri_zigbee_clusters.c`
  - Custom cluster attribute list creation
  - Helper functions for attribute get/set
- **[NEW]** `common/agri_zigbee_clusters/CMakeLists.txt`
- **[NEW]** `common/crc_utils/crc_utils.h` + `crc_utils.c` + `CMakeLists.txt`
  - CRC-16 Modbus for NPK RS-485 communication
  - CRC-8 for data integrity checks

### Phase 2: Field Node — Sensor Drivers (7 drivers)
*Each driver follows the pattern: `component_name.h` (API) + `component_name.c` (impl) + `CMakeLists.txt`.*

- **[NEW]** `field_node/components/sensor_hub/dht22_driver/`
  - 1-Wire GPIO bit-bang protocol, microsecond timing via `esp_timer`
  - Returns temp (°C) and humidity (%), CRC-8 validation
  - Handles timeout, bad CRC, sensor not responding
- **[NEW]** `field_node/components/sensor_hub/sht40_driver/`
  - I2C driver (addr 0x44), high-precision mode
  - Temperature -40..125°C, Humidity 0..100%
  - I2C NACK error handling, heater control for dehumidification
- **[NEW]** `field_node/components/sensor_hub/soil_moisture_driver/`
  - ADC oneshot driver (12-bit), multi-depth probe support
  - VWC calibration: raw ADC → percentage via NVS offset/gain
  - Median filter (window=5)
- **[NEW]** `field_node/components/sensor_hub/npk_rs485_driver/`
  - UART + MAX3485 RS-485 half-duplex, Modbus RTU protocol
  - Read N/P/K registers (0–1999 mg/L), CRC-16 validation
  - TX enable GPIO control, configurable baud (default 9600)
- **[NEW]** `field_node/components/sensor_hub/rain_gauge_driver/`
  - GPIO interrupt (ISR) with debounce timer (50ms)
  - Pulse counting: 0.2mm per pulse, cumulative total in RTC memory
  - NVS persistence on sleep, counter reset via command
- **[NEW]** `field_node/components/sensor_hub/bh1750_driver/`
  - I2C driver (addr 0x23), continuous high-res mode
  - 1–65535 lux range, 120ms measurement time
  - Power down between readings for sleep efficiency
- **[NEW]** `field_node/components/sensor_hub/scd40_driver/`
  - I2C driver (addr 0x62), periodic measurement mode
  - CO₂ 400–5000 ppm, also provides temp/humidity secondary
  - Forced recalibration support, 5s measurement interval
- **[NEW]** `field_node/components/sensor_hub/sensor_hub.h` + `sensor_hub.c` + `CMakeLists.txt`
  - Orchestrator: initializes all sensors, runs parallel acquisition tasks
  - Median filter engine (window=5), NVS calibration offset/gain application
  - Populates `agri_sensor_data_t` struct

### Phase 3: Field Node — Actuator Controller
- **[NEW]** `field_node/components/actuator_controller/`
  - **Relay control:** GPIO for irrigation valve (open/close) + feedback current sensor
  - **PWM control:** LEDC for pump (0–100% duty), fan (0–100%), LED grow-light (RGBW)
  - **PID controller:** temperature setpoint → fan PWM (configurable Kp/Ki/Kd via NVS)
  - **Fertigation pump:** dosing with flow-meter pulse counting
  - Command processing from `agri_cmd_t` struct

### Phase 4: Field Node — Power Manager
- **[NEW]** `field_node/components/power_manager/`
  - Deep-sleep scheduling with RTC wakeup timer (configurable, default 30s)
  - Battery voltage monitoring via ADC
  - Pre-sleep: flush NVS rain counter, check pending Zigbee TX
  - Wake source detection (timer vs GPIO)
  - Low-battery alarm threshold

### Phase 5: Field Node — Zigbee End Device
- **[NEW]** `field_node/components/zigbee_end_device/`
  - ESP-ZB SDK End Device role initialization
  - Network join with 60s timeout, retry logic
  - AGRI_CLUSTER attribute reporting (all 15 attributes)
  - Standard cluster servers: Basic, Power Config, Temperature, Humidity, On/Off, Level Control
  - OTA Upgrade client (cluster 0x0019)
  - Sleepy ED poll-parent configuration
  - Actuator command receive via On/Off + Level Control clusters
  - ACK handling with 5s timeout, 3x retry, alarm flag on failure

### Phase 6: Field Node — NVS Config + OTA Node
- **[NEW]** `field_node/components/nvs_config/`
  - NVS namespace `agri_config` — calibration offsets, gains, thresholds
  - Default value handling for `ESP_ERR_NVS_NOT_FOUND`
  - Runtime config getters/setters
- **[NEW]** `field_node/components/ota_node/`
  - Zigbee OTA image receive (cluster 0x0019 client)
  - Block-by-block (64B) transfer, resume from last block on sleep/wake
  - SHA-256 verification before boot switch

### Phase 7: Field Node — Main Entry Point
- **[NEW]** `field_node/main/main.c`
  - `app_main()` implementing the full wake/sleep state machine from tech spec §6.1:
    `BOOT_INIT` → `SENSOR_ACQUIRE` → `DATA_PROCESS` → `ZIGBEE_JOIN_CHECK` → `ZIGBEE_REPORT` → `ACTUATOR_POLL` → `OTA_CHECK` → `SLEEP_PREP` → `DEEP_SLEEP`
  - Parallel sensor acquisition using FreeRTOS tasks
  - State tracking, error recovery, alarm flag management
- **[NEW]** `field_node/main/CMakeLists.txt` + `field_node/CMakeLists.txt`

### Phase 8: Gateway — All Components (9 components)
- **[NEW]** `gateway/components/nvs_config/`
  - NVS config loader for all gateway settings (Wi-Fi SSID/pass, MQTT broker, farm_id, etc.)
  - All secrets read from NVS, never hard-coded
- **[NEW]** `gateway/components/system_monitor/`
  - Heap monitoring with low-heap warning
  - Task watchdog registration and periodic feeds
  - RSSI reporting, uptime tracking
  - `sys_monitor_task` (stack=2048, prio=1, core=0)
- **[NEW]** `gateway/components/sntp_sync/`
  - SNTP time sync to NTP server (configurable)
  - RTC set, ISO-8601 UTC timestamp formatting
  - `sntp_task` (stack=2048, prio=1, core=0)
- **[NEW]** `gateway/components/wifi_manager/`
  - Wi-Fi STA mode, auto-reconnect with exponential backoff
  - IP event handling, EventGroup WIFI_CONNECTED bit
  - Event-driven MQTT connect trigger
  - `wifi_mgr_task` (stack=4096, prio=5, core=0)
- **[NEW]** `gateway/components/zigbee_coordinator/`
  - Zigbee network formation (PAN ID, channel scan, AES-128 key)
  - Device join handling, permit join control
  - AGRI_CLUSTER attribute report receive → parse → enqueue to MQTT queue
  - Actuator command dispatch from MQTT queue → Zigbee On/Off/Level cluster
  - Device tracking (joined node list, RSSI, LQI, parent)
  - `zb_main_task` (stack=8192, prio=6, core=1)
- **[NEW]** `gateway/components/mqtt_client/`
  - MQTT 3.1.1 over TLS 1.3, configurable broker/port/creds from NVS
  - Publish telemetry: `agri/{farm_id}/{node_id}/telemetry` (QoS 1)
  - Subscribe commands: `agri/{farm_id}/{node_id}/cmd` (QoS 1)
  - Status publish: `agri/{farm_id}/{node_id}/status` (retained)
  - Cloud buffer integration: enqueue to buffer when offline
  - `mqtt_pub_task` + `mqtt_sub_task` (stack=8192/4096, prio=4, core=0)
- **[NEW]** `gateway/components/rest_api/`
  - ESP HTTP server on port 8080
  - Endpoints per tech spec Table 11: `/api/v1/nodes`, `/sensors`, `/actuators`, `/cmd`, `/config`, `/ota`, `/health`
  - JWT Bearer token auth (RS256, 1h expiry)
  - cJSON request/response handling
  - `rest_api_task` (stack=6144, prio=3, core=0)
- **[NEW]** `gateway/components/cloud_buffer/`
  - SPIFFS ring-buffer for offline telemetry storage (72h max)
  - Write: store JSON telemetry when MQTT offline
  - Read: replay buffered data on reconnect, FIFO ordering
  - Overflow policy: overwrite oldest
  - `cloud_buffer_task` (stack=4096, prio=3, core=0)
- **[NEW]** `gateway/components/ota_manager/`
  - GW OTA: dual-partition A/B scheme via HTTPS
  - Poll `${OTA_SERVER}/api/v1/version?target=gw` every 6 hours
  - Semantic version comparison, SHA-256 + RSA-2048 verification
  - Rollback on 3 consecutive boot failures
  - Node OTA: receive URL via MQTT, download image, serve via Zigbee OTA cluster 0x0019
  - `ota_check_task` (stack=8192, prio=2, core=0)

### Phase 9: Gateway — Main Entry Point
- **[NEW]** `gateway/main/main.c`
  - `app_main()` following the reference scaffolding from agent prompt §9.1
  - Init sequence: NVS → netif → event loop → config → monitor → SNTP → WiFi → Zigbee → buffer → MQTT → REST → OTA
  - Free heap report on startup
- **[NEW]** `gateway/main/CMakeLists.txt` + `gateway/CMakeLists.txt`

### Phase 10: Build System
- **[NEW]** `CMakeLists.txt` (top-level)
  - References both `gateway/` and `field_node/` as sub-projects
  - Common components path inclusion
- **[NEW]** `sdkconfig.defaults`
  - All settings from tech spec §12 (both S3 gateway and H2 node configs)
- **[NEW]** `partitions.csv`
  - OTA dual-partition layout from tech spec §9.3

### Phase 11: Tools
- **[NEW]** `tools/provision.py`
  - Factory provisioning: generate unique `device_id`, create NVS CSV, flash via esptool
  - eFuse burn flow for Secure Boot + Flash Encryption
- **[NEW]** `tools/ota_server.py`
  - Local HTTP server serving OTA firmware images with version metadata
- **[NEW]** `tools/mqtt_simulator.py`
  - Dev/test MQTT message injector for telemetry and commands

### Phase 12: Documentation
- **[NEW]** `README.md`
  - Build steps for both targets, flash commands, hardware wiring tables
- **[NEW]** `ARCHITECTURE.md`
  - System block diagram, data-flow description, task topology
- **[NEW]** `API.md`
  - REST API endpoint spec, MQTT topic/payload schemas
- **[NEW]** `SECURITY.md`
  - Secure boot setup, flash encryption steps, key management

## Open Questions

> [!IMPORTANT]
> - **Project location:** Should the code go directly in `smart_Agri_Node/` or in a subdirectory like `smart_Agri_Node/smart-agri-iot/`?
> - **Build verification:** Do you have ESP-IDF v5.2+ installed on this machine for compile testing, or should I focus on code correctness without local build validation?
> - **Scope prioritization:** This is ~60+ files of production firmware. Should I implement everything in one go, or would you prefer I deliver in phases (e.g., common + field_node first, then gateway)?

## Verification Plan

### Automated Tests
- Verify all `CMakeLists.txt` have correct `REQUIRES` dependencies
- Verify no `printf()` calls (only `ESP_LOGx`)
- Verify no hard-coded credentials
- Verify all `esp_err_t` returns are checked
- Verify all structs are packed and version-tagged
- Cross-reference Zigbee cluster attributes against data model structs

### Manual Verification
- User builds with `idf.py set-target esp32s3 && idf.py build` (gateway)
- User builds with `idf.py set-target esp32h2 && idf.py build` (field_node)
- Flash to hardware and verify sensor readings, Zigbee join, MQTT telemetry

## File Count Summary

| Component | Files |
| :--- | :--- |
| Common (data model, clusters, CRC) | 9 |
| Field Node sensor drivers (7 drivers + hub) | 24 |
| Field Node other components (actuator, power, zigbee, nvs, ota) | 15 |
| Field Node main + build | 3 |
| Gateway components (9 components) | 27 |
| Gateway main + build | 3 |
| Build system (top-level) | 3 |
| Tools | 3 |
| Documentation | 4 |
| **Total** | **~91 files** |