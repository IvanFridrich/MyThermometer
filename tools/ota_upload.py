#!/usr/bin/env python3
"""OTA firmware uploader for the ESP32-S3 Thermometer.

The thermometer runs a small HTTPS server on port 8443 (OTA-only).
This script builds the firmware (optional), POSTs the .bin file, and
waits for the device to restart and come back online.

Usage:
    python tools/ota_upload.py [--host teplomer.local] [--port 8443] [--no-build]

Requirements:
    pip install -r tools/requirements_ota.txt
"""
import argparse
import hashlib
import os
import subprocess
import sys
import time

import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

FIRMWARE = ".pio/build/esp32-s3-devkitc-1/firmware.bin"
DEFAULT_HOST = "teplomer.local"
DEFAULT_PORT = 8443


def build() -> None:
    subprocess.run(["pio", "run", "-e", "esp32-s3-devkitc-1"], check=True)


def wait_reconnect(host: str, timeout: int = 120) -> bool:
    """Wait for device to disappear (restart) then come back online."""
    print("Waiting for device to restart", end="", flush=True)
    deadline = time.time() + timeout

    # Phase 1: wait for the device to go offline
    while time.time() < deadline:
        try:
            requests.get(f"http://{host}/api/current", timeout=2)
            time.sleep(1)
            print(".", end="", flush=True)
        except Exception:
            break  # went offline — good

    # Phase 2: wait for the device to come back
    while time.time() < deadline:
        try:
            r = requests.get(f"http://{host}/api/current", timeout=2)
            if r.ok:
                print(" online!")
                return True
        except Exception:
            pass
        time.sleep(2)
        print(".", end="", flush=True)

    print(" timeout!")
    return False


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=DEFAULT_HOST, help="Device hostname or IP")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help="OTA HTTPS port")
    ap.add_argument("--no-build", action="store_true",
                    help="Skip PlatformIO build (use existing .bin)")
    args = ap.parse_args()

    if not args.no_build:
        print("Building firmware...")
        build()

    if not os.path.isfile(FIRMWARE):
        print(f"Error: firmware not found at {FIRMWARE}", file=sys.stderr)
        sys.exit(1)

    size = os.path.getsize(FIRMWARE)
    md5 = hashlib.md5(open(FIRMWARE, "rb").read()).hexdigest()
    print(f"Firmware: {size / 1024:.0f} kB  MD5: {md5[:12]}…")

    url = f"https://{args.host}:{args.port}/api/ota"
    print(f"Uploading to {url} …")
    try:
        with open(FIRMWARE, "rb") as f:
            r = requests.post(
                url,
                data=f,
                headers={
                    "Content-Type": "application/octet-stream",
                    "Content-Length": str(size),
                },
                verify=False,  # self-signed cert; encryption-only, no chain trust
                timeout=60,
            )
    except requests.exceptions.ConnectionError as exc:
        print(f"Connection error: {exc}", file=sys.stderr)
        sys.exit(1)

    if not r.ok:
        print(f"Error: HTTP {r.status_code}  {r.text}", file=sys.stderr)
        sys.exit(1)

    print("Upload accepted, waiting for restart…")
    if wait_reconnect(args.host):
        print("OTA complete.")
    else:
        print("Device did not come back online — check UART log.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
