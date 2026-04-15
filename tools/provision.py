#!/usr/bin/env python3
"""
SAGRI Provisioning Tool — provision.py
Burns Wi-Fi credentials, MQTT config, device IDs, and calibration
into NVS partition via esptool or ESP-IDF NVS generator.

Usage:
  python provision.py --port COM4 --device-id NODE-0001 --farm-id farm01 \
      --wifi-ssid "MyNet" --wifi-pass "secret" \
      --mqtt-uri "mqtts://broker.example.com:8883"
"""

import argparse
import subprocess
import csv
import tempfile
import os
import sys


NVS_NAMESPACE = "agri_config"
NVS_PARTITION_SIZE = "0x6000"


def create_nvs_csv(args):
    """Generate NVS CSV from CLI args."""
    rows = [["key", "type", "encoding", "value"]]
    rows.append([NVS_NAMESPACE, "namespace", "", ""])

    if args.device_id:
        rows.append(["device_id", "data", "string", args.device_id])
    if args.farm_id:
        rows.append(["farm_id", "data", "string", args.farm_id])
    if args.field_id:
        rows.append(["field_id", "data", "string", args.field_id])
    if args.wifi_ssid:
        rows.append(["wifi_ssid", "data", "string", args.wifi_ssid])
    if args.wifi_pass:
        rows.append(["wifi_pass", "data", "string", args.wifi_pass])
    if args.mqtt_uri:
        rows.append(["mqtt_uri", "data", "string", args.mqtt_uri])
    if args.mqtt_user:
        rows.append(["mqtt_user", "data", "string", args.mqtt_user])
    if args.mqtt_pass:
        rows.append(["mqtt_pass", "data", "string", args.mqtt_pass])
    if args.ota_url:
        rows.append(["ota_url", "data", "string", args.ota_url])
    if args.ntp_server:
        rows.append(["ntp_server", "data", "string", args.ntp_server])
    if args.sleep_sec:
        rows.append(["sleep_sec", "data", "u32", str(args.sleep_sec)])
    if args.latitude:
        rows.append(["latitude", "data", "string", str(args.latitude)])
    if args.longitude:
        rows.append(["longitude", "data", "string", str(args.longitude)])

    return rows


def write_csv(rows, path):
    """Write NVS CSV to file."""
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(rows)
    print(f"[+] NVS CSV written to: {path}")


def generate_binary(csv_path, bin_path):
    """Convert CSV to NVS binary using ESP-IDF nvs_partition_gen.py."""
    idf_path = os.environ.get("IDF_PATH", "")
    nvs_gen = os.path.join(idf_path, "components", "nvs_flash",
                           "nvs_partition_generator", "nvs_partition_gen.py")

    if not os.path.exists(nvs_gen):
        print(f"[!] nvs_partition_gen.py not found at {nvs_gen}")
        print("[!] Make sure IDF_PATH is set correctly")
        return False

    cmd = [sys.executable, nvs_gen, "generate", csv_path, bin_path, NVS_PARTITION_SIZE]
    print(f"[*] Generating NVS binary: {' '.join(cmd)}")

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[!] Error: {result.stderr}")
        return False

    print(f"[+] NVS binary generated: {bin_path}")
    return True


def flash_binary(bin_path, port, baud=460800):
    """Flash NVS binary to device using esptool."""
    nvs_offset = "0x9000"  # From partitions.csv

    cmd = [
        sys.executable, "-m", "esptool",
        "--chip", "auto",
        "--port", port,
        "--baud", str(baud),
        "write_flash", nvs_offset, bin_path
    ]

    print(f"[*] Flashing NVS to {port} at offset {nvs_offset}...")
    result = subprocess.run(cmd)

    if result.returncode == 0:
        print("[+] NVS partition flashed successfully!")
    else:
        print("[!] Flash failed")
    return result.returncode == 0


def main():
    parser = argparse.ArgumentParser(
        description="SAGRI Smart Agriculture Node Provisioning Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Provision a field node:
  python provision.py --port COM4 --device-id NODE-0001 --farm-id farm01

  # Provision a gateway:
  python provision.py --port COM5 --device-id GW-0001 --farm-id farm01 \\
      --wifi-ssid "FarmNet" --wifi-pass "s3cur3" \\
      --mqtt-uri "mqtts://broker.example.com:8883" \\
      --mqtt-user "sagri" --mqtt-pass "p4ssw0rd"

  # Generate CSV only (no flash):
  python provision.py --csv-only --device-id NODE-0001 --farm-id farm01
        """
    )

    parser.add_argument("--port", help="Serial port (e.g., COM4, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=460800, help="Flash baud rate")
    parser.add_argument("--csv-only", action="store_true", help="Generate CSV only, don't flash")

    parser.add_argument("--device-id", required=True, help="Device ID (e.g., NODE-0001)")
    parser.add_argument("--farm-id", default="farm01", help="Farm identifier")
    parser.add_argument("--field-id", default="", help="Field/zone identifier")

    parser.add_argument("--wifi-ssid", default="", help="Wi-Fi SSID (gateway only)")
    parser.add_argument("--wifi-pass", default="", help="Wi-Fi password")
    parser.add_argument("--mqtt-uri", default="", help="MQTT broker URI (mqtts://...)")
    parser.add_argument("--mqtt-user", default="", help="MQTT username")
    parser.add_argument("--mqtt-pass", default="", help="MQTT password")

    parser.add_argument("--ota-url", default="", help="OTA server URL")
    parser.add_argument("--ntp-server", default="pool.ntp.org", help="NTP server")
    parser.add_argument("--sleep-sec", type=int, default=30, help="Deep-sleep interval (seconds)")
    parser.add_argument("--latitude", type=float, default=0.0, help="GPS latitude")
    parser.add_argument("--longitude", type=float, default=0.0, help="GPS longitude")

    args = parser.parse_args()

    if not args.csv_only and not args.port:
        parser.error("--port is required unless --csv-only is used")

    print("═" * 50)
    print("  SAGRI Provisioning Tool")
    print("═" * 50)
    print(f"  Device ID:  {args.device_id}")
    print(f"  Farm ID:    {args.farm_id}")
    print(f"  Sleep:      {args.sleep_sec}s")
    print("═" * 50)

    rows = create_nvs_csv(args)

    with tempfile.TemporaryDirectory() as tmpdir:
        csv_path = os.path.join(tmpdir, "nvs_data.csv")
        bin_path = os.path.join(tmpdir, "nvs_data.bin")

        write_csv(rows, csv_path)

        if args.csv_only:
            # Copy CSV to working directory
            import shutil
            out = f"nvs_{args.device_id}.csv"
            shutil.copy(csv_path, out)
            print(f"[+] CSV saved: {out}")
            return

        if not generate_binary(csv_path, bin_path):
            sys.exit(1)

        if not flash_binary(bin_path, args.port, args.baud):
            sys.exit(1)

    print("\n[+] Provisioning complete!")


if __name__ == "__main__":
    main()
