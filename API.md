# SAGRI API & Protocol Specification

## 1. MQTT Topics & Schemas

The gateway leverages MQTT 3.1.1. All payloads are UTF-8 encoded JSON.

### Telemetry (Gateway → Cloud)
Topic: `agri/{farm_id}/{node_id}/telemetry`
QoS: `1`

```json
{
  "schema_ver": 2,
  "device_id": "NODE-1A2B",
  "fw_ver": "1.4.2",
  "ts": "2025-06-15T14:32:00.000Z",
  "loc": {
    "lat": 34.0522,
    "lon": -118.2437,
    "field": "Zone_A"
  },
  "sensors": {
    "temp_c": 24.5,
    "humidity_pct": 55.2,
    "soil_moist_pct": 42.1,
    "npk_n": 105,
    "rain_mm": 12.4,
    "lux": 45000,
    "co2_ppm": 412
  },
  "actuators": {
    "valve_open": false,
    "pump_pct": 0,
    "fan_pct": 50
  },
  "sys": {
    "batt_mv": 3200,
    "rssi_dbm": -65,
    "lqi": 120,
    "alarm_flags": 0
  }
}
```

### Commands (Cloud → Gateway)
Topic: `agri/{farm_id}/{node_id}/cmd`
QoS: `1`

```json
{
  "schema_ver": 2,
  "device_id": "NODE-1A2B",
  "cmd_type": 1,
  "cmd_id": 1686835200,
  "timestamp": "2025-06-15T14:32:00.000Z",
  "payload": {
    "valve_state": true
  }
}
```
*Note: `cmd_type` mappings: `0x01`=Valve, `0x02`=Pump, `0x03`=Fan, `0x20`=OTA Trigger.*


## 2. Gateway REST API

The Gateway ESP32-S3 hosts an HTTP server on port `:8080` for local administration.

### Health Check
`GET /api/v1/health`
**Response:** `{"status":"ok","heap":124000,"min_heap":110000,"uptime":3600}`

### Connected Nodes
`GET /api/v1/nodes`
Returns an array of actively joined Zigbee devices with their last known state, LQI, and FW versions.

### Send Command
`POST /api/v1/nodes/{zigbee_short_addr}/cmd`
**Body:** Identical formatting to the MQTT Command schema payload.

### Provisioning Config
`GET /api/v1/config`
Retrieves live gateway configuration (Farm ID, poll rates) as defined in NVS.
