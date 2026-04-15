# SAGRI System Architecture

## 1. System Block Diagram

```mermaid
graph TD
    subgraph "Cloud Infrastructure"
        MQTT[MQTT Broker]
        OTA_S[OTA Server]
        API_G[API Server]
    end

    subgraph "Edge Gateway (ESP32-S3)"
        GW_M[MQTT Client] -->|TLS/QoS 1| MQTT
        GW_R[REST API :8080] <-->|HTTP| API_G
        GW_O[OTA Manager] <-->|HTTPS| OTA_S
        
        GW_B[Cloud Buffer SPIFFS]
        GW_M <--> GW_B
        
        GW_Z[Zigbee Coordinator]
        GW_Z <--> GW_M
    end

    subgraph "Field Network (IEEE 802.15.4)"
        N1[Field Node 1<br/>ESP32-H2]
        N2[Field Node 2<br/>ESP32-H2]
        N3[Field Node 3<br/>ESP32-H2]
        
        GW_Z <..>|Zigbee 3.0| N1
        GW_Z <..>|Zigbee 3.0| N2
        GW_Z <..>|Zigbee 3.0| N3
    end

    subgraph "Sensors & Actuators (Per Node)"
        N1 --> S1[I2C: SHT40, SCD40, BH1750]
        N1 --> S2[ADC: Soil Moisture]
        N1 --> S3[RS-485: NPK]
        N1 --> A1[GPIO: Valve Relay]
        N1 --> A2[PWM: Fan PID, Pump]
    end
```

## 2. High-Level Data Flow

1. **Acquisition**: `Field Nodes` wake up from deep sleep, query their local sensors via I2C/UART/ADC/GPIO in parallel FreeRTOS tasks.
2. **Local Processing**: Raw values are processed. NVS calibration coefficients are applied, and median filters smooth the data.
3. **Local Transport**: Field nodes pack the data into standard and custom Zigbee attributes (`AGRI_CLUSTER` 0xFF00) and send a Zigbee Report to the `Gateway`.
4. **Edge Bridging**: The `Gateway` receives the ZCL Report, parses the manufacturer-specific payload, converts it to standard JSON (`agri_sensor_data_t`), and queues it.
5. **Cloud Transport**: The Gateway's MQTT component publishes the JSON to `agri/{farm_id}/{device_id}/telemetry`.
6. **Edge Buffering**: If the Wi-Fi/MQTT link is down, the Gateway writes the JSON string sequentially into a SPIFFS ring-buffer (`cloud_buffer`). It later replays them upon reconnection.

## 3. Node State Machine (ESP32-H2)

The field node operates strictly on a wake-sleep cycle to preserve battery:

1. **BOOT_INIT**: Check wake reason. Read NVS config. Check battery health. Initialize sensor hardware.
2. **SENSOR_ACQUIRE**: Map sensors to EventGroups. Spawn parallel reader tasks. Block with 2000ms timeout.
3. **DATA_PROCESS**: Gather data. Run fan PID controller. Pack `agri_sensor_data_t`.
4. **ZIGBEE_JOIN**: Ensure node is joined to Coordinator. Rejoin if orphaned.
5. **ZIGBEE_REPORT**: Transmit data via ZCL attributes to Coordinator.
6. **ACTUATOR_POLL**: Request any pending actuation commands (valves, pumps) via ZCL polling.
7. **OTA_CHECK**: Periodically (e.g. every 24 wake cycles) query Zigbee Cluster 0x0019 for new firmware.
8. **SLEEP_PREP**: De-init sensors safely. Save cumulative rain ticks to NVS.
9. **DEEP_SLEEP**: Drop to ~10µA state. ULP/RTC timer counts down to next cycle.
