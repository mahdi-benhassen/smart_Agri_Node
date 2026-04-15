# SAGRI — Smart Agriculture IoT System

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Cloud / MQTT Broker                  │
│                  (TLS 1.2, QoS 1, LWT)                  │
└──────────────────────────┬──────────────────────────────┘
                           │ Wi-Fi                                
┌──────────────────────────┴──────────────────────────────┐
│                ESP32-S3 Gateway (sagri_gateway)          │
│                                                          │
│  ┌─────────────┐  ┌─────────┐  ┌────────────┐          │
│  │   Zigbee    │  │  MQTT   │  │  REST API  │          │
│  │ Coordinator │  │  Client │  │  :8080     │          │
│  └──────┬──────┘  └────┬────┘  └────────────┘          │
│         │              │                                 │
│  ┌──────┴──────────────┴──────────────────┐             │
│  │  Bridge Logic + Cloud Buffer (SPIFFS)  │             │
│  └────────────────────────────────────────┘             │
│  ┌──────────────────────────────────────────┐           │
│  │  System Monitor + OTA Manager + SNTP     │           │
│  └──────────────────────────────────────────┘           │
└──────────────────────────┬──────────────────────────────┘
                           │ IEEE 802.15.4 (Zigbee 3.0)
           ┌───────────────┼───────────────┐
           │               │               │
    ┌──────┴──────┐ ┌──────┴──────┐ ┌──────┴──────┐
    │  Field Node │ │  Field Node │ │  Field Node │
    │  ESP32-H2   │ │  ESP32-H2   │ │  ESP32-H2   │
    │             │ │             │ │             │
    │ Sensors:    │ │             │ │             │
    │ • DHT22     │ │ • SHT40    │ │ • All       │
    │ • SHT40     │ │ • Soil     │ │   sensors   │
    │ • Soil ADC  │ │ • NPK      │ │             │
    │ • NPK RS485 │ │ • SCD40   │ │             │
    │ • BH1750    │ │ • BH1750  │ │             │
    │ • SCD40     │ │ • Rain    │ │             │
    │ • Rain gauge│ │            │ │             │
    │             │ │            │ │             │
    │ Actuators:  │ │            │ │             │
    │ • Valve     │ │            │ │             │
    │ • Pump PWM  │ │            │ │             │
    │ • Fan PID   │ │            │ │             │
    │ • LED R/B   │ │            │ │             │
    └─────────────┘ └────────────┘ └─────────────┘
```

## Project Structure

```
smart_Agri_Node/
├── common/                     # Shared libraries
│   ├── agri_data_model/        # Sensor data structs + JSON codec
│   ├── agri_zigbee_clusters/   # Custom AGRI_CLUSTER (0xFF00)
│   └── crc_utils/              # CRC-16 Modbus + CRC-8 Maxim
│
├── field_node/                 # ESP32-H2 firmware
│   ├── main/                   # State machine (Boot→Sense→Report→Sleep)
│   └── components/
│       ├── sensor_hub/         # Parallel sensor orchestrator
│       │   ├── dht22_driver/   # 1-Wire bit-bang
│       │   ├── sht40_driver/   # I2C high-precision
│       │   ├── soil_moisture_driver/ # ADC + median filter
│       │   ├── npk_rs485_driver/    # Modbus RTU
│       │   ├── rain_gauge_driver/   # GPIO ISR + debounce
│       │   ├── bh1750_driver/       # I2C light sensor
│       │   └── scd40_driver/        # I2C CO₂ sensor
│       ├── actuator_controller/ # Relay + LEDC PWM + PID
│       ├── power_manager/       # Deep sleep + battery ADC
│       ├── zigbee_end_device/   # ESP-ZB SDK (ED)
│       ├── nvs_config/          # Persistent config
│       └── ota_node/            # Zigbee OTA receive
│
├── gateway/                    # ESP32-S3 firmware
│   ├── main/                   # Service orchestrator
│   └── components/
│       ├── wifi_manager/       # STA + exponential backoff
│       ├── zigbee_coordinator/ # Network formation + device track
│       ├── mqtt_client/        # TLS pub/sub + topic routing
│       ├── rest_api/           # HTTP server :8080
│       ├── cloud_buffer/       # SPIFFS offline ring buffer (72h)
│       ├── ota_manager/        # HTTPS A/B + node OTA relay
│       ├── system_monitor/     # Heap/WDT/uptime
│       ├── sntp_sync/          # NTP + ISO-8601
│       └── nvs_config/         # Gateway credentials
│
├── tools/
│   └── provision.py            # NVS provisioning tool
│
├── partitions.csv              # OTA dual-partition layout
└── sdkconfig.defaults          # Production Kconfig
```

## Building

### Gateway (ESP32-S3)

```bash
cd gateway
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

### Field Node (ESP32-H2)

```bash
cd field_node
idf.py set-target esp32h2
idf.py build
idf.py -p COM4 flash monitor
```

## Provisioning

```bash
# Field node
python tools/provision.py --port COM4 --device-id NODE-0001 --farm-id farm01 --sleep-sec 30

# Gateway
python tools/provision.py --port COM5 --device-id GW-0001 --farm-id farm01 \
    --wifi-ssid "FarmNet" --wifi-pass "s3cur3" \
    --mqtt-uri "mqtts://broker.example.com:8883"
```

## MQTT Topics

| Topic | Direction | QoS | Description |
|-------|-----------|-----|-------------|
| `agri/{farm}/NODE-XXXX/telemetry` | GW→Cloud | 1 | Sensor data JSON |
| `agri/{farm}/NODE-XXXX/status` | GW→Cloud | 1 | Online/Offline (retained) |
| `agri/{farm}/NODE-XXXX/cmd` | Cloud→GW | 1 | Actuator commands |
| `agri/{farm}/gw/health` | GW→Cloud | 0 | Gateway health stats |
| `agri/{farm}/+/ota` | Cloud→GW | 1 | OTA trigger |

## REST API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/health` | GET | System health (heap, uptime) |
| `/api/v1/nodes` | GET | List all connected field nodes |
| `/api/v1/nodes/{id}/cmd` | POST | Send actuator command to node |
| `/api/v1/config` | GET | Runtime configuration |

## Field Node State Machine

```
BOOT_INIT → SENSOR_ACQUIRE → DATA_PROCESS → ZIGBEE_JOIN_CHECK
    → ZIGBEE_REPORT → ACTUATOR_POLL → OTA_CHECK → SLEEP_PREP → DEEP_SLEEP
```

## Custom Zigbee Cluster (0xFF00)

15 manufacturer-specific attributes covering soil moisture, NPK, CO₂, rainfall, light, battery, alarms, and actuator state feedback.

## License

Proprietary — © 2025 SAGRI Project
