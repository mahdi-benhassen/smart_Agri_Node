#!/usr/bin/env python3
"""
SAGRI MQTT Simulator — mqtt_simulator.py
Injects commands and reads telemetry for development and testing.

Requires: pip install paho-mqtt
"""

import json
import time
import argparse
import paho.mqtt.client as mqtt

def on_connect(client, userdata, flags, rc):
    print(f"[*] Connected to MQTT broker with result code {rc}")
    # Subscribe to telemetry and status
    farm_id = userdata["farm_id"]
    client.subscribe(f"agri/{farm_id}/+/telemetry")
    client.subscribe(f"agri/{farm_id}/+/status")
    client.subscribe(f"agri/{farm_id}/gw/health")

def on_message(client, userdata, msg):
    print(f"\n[+] Message received on topic: {msg.topic}")
    try:
        payload = json.loads(msg.payload.decode())
        print(json.dumps(payload, indent=2))
    except json.JSONDecodeError:
        print(msg.payload.decode())

def trigger_command(client, farm_id, target_node, cmd_type, payload):
    topic = f"agri/{farm_id}/{target_node}/cmd"
    cmd = {
        "schema_ver": 2,
        "device_id": target_node,
        "cmd_type": cmd_type,
        "cmd_id": int(time.time()),
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime()),
        "payload": payload
    }
    
    print(f"[*] Sending command to {topic}:")
    print(json.dumps(cmd, indent=2))
    client.publish(topic, json.dumps(cmd), qos=1)

def main():
    parser = argparse.ArgumentParser(description="SAGRI MQTT Simulator CLI")
    parser.add_argument("--broker", default="localhost", help="MQTT Broker address")
    parser.add_argument("--port", type=int, default=1883, help="MQTT Broker port")
    parser.add_argument("--farm", default="farm01", help="Farm ID to scope topics")
    parser.add_argument("--tls", action="store_true", help="Enable TLS")
    parser.add_argument("--cmd", choices=["valve", "pump", "fan"], help="Command to inject")
    parser.add_argument("--node", default="NODE-0001", help="Target node for command")
    parser.add_argument("--val", type=int, default=0, help="Value for command (1/0 for valve, 0-100 for pump/fan)")
    
    args = parser.parse_args()

    client = mqtt.Client(userdata={"farm_id": args.farm})
    client.on_connect = on_connect
    client.on_message = on_message

    if args.tls:
        client.tls_set() # Will use system default CA certs

    print(f"[*] Connecting to {args.broker}:{args.port}...")
    client.connect(args.broker, args.port, 60)

    # If sending a command, just publish and exit after a brief wait
    if args.cmd:
        client.loop_start()
        time.sleep(1) # Allow connection to establish
        
        if args.cmd == "valve":
            trigger_command(client, args.farm, args.node, 0x01, {"valve_state": bool(args.val)})
        elif args.cmd == "pump":
            trigger_command(client, args.farm, args.node, 0x02, {"duty_pct": args.val})
        elif args.cmd == "fan":
            trigger_command(client, args.farm, args.node, 0x03, {"duty_pct": args.val})
            
        time.sleep(1) # Allow publish to finish
        client.loop_stop()
    else:
        # Otherwise, run endlessly to monitor topics
        print("[*] Monitoring mode. Press Ctrl+C to stop.")
        try:
            client.loop_forever()
        except KeyboardInterrupt:
            print("\n[*] Stopping simulator...")

if __name__ == "__main__":
    main()
