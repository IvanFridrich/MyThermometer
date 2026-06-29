#!/usr/bin/env python3
"""BLE GATT OTA firmware uploader for the ESP32-S3 Thermometer.

Connects to the device via Bluetooth, transfers firmware.bin over the custom
GATT OTA service, and triggers a restart. Alternative to the HTTPS OTA path
(tools/ota_upload.py) — useful when only BLE is reachable.

Protocol:
  1. Connect to "ESP32S3-Thermo"
  2. Subscribe to STATUS notifications
  3. Write CTRL [0x01, <size LE u32>]  – begin OTA
  4. Write DATA chunks (write-without-response for throughput)
  5. Write CTRL [0x02]                 – commit (verify + restart)
  6. Receive STATUS [0x00]             – success; device restarts

Usage:
    python tools/ble_ota.py [--no-build]
    python tools/ble_ota.py --no-build --device AA:BB:CC:DD:EE:FF

Requirements:
    pip install -r tools/requirements_ble_ota.txt
"""

import argparse
import asyncio
import hashlib
import os
import subprocess
import sys

from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

FIRMWARE = ".pio/build/esp32-s3-devkitc-1/firmware.bin"
DEVICE_NAME = "ESP32S3-Thermo"

# GATT OTA service UUIDs — must match cfg::ble::kOta* in Config.h
SVC_UUID    = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CTRL_UUID   = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
DATA_UUID   = "cba1d466-344c-4be3-ab3f-189f80dd7518"
STATUS_UUID = "f27b53ad-c63d-49a0-8c0f-9309d08f1fd0"

CTRL_START  = 0x01
CTRL_COMMIT = 0x02
CTRL_ABORT  = 0x03

STATUS_OK    = 0x00
STATUS_ERROR = 0xFF


def build() -> None:
    subprocess.run(["pio", "run", "-e", "esp32-s3-devkitc-1"], check=True)


async def find_device(name: str, address: str | None, timeout: float = 65.0):
    if address:
        print(f"Connecting to {address} …")
        return address
    # Device advertises for ~750 ms once per minute; worst-case catch requires
    # just over 60 s of scanning.  Default 65 s covers one full cycle + margin.
    print(f"Scanning for '{name}' (up to {timeout:.0f} s) …")
    device = await BleakScanner.find_device_by_name(name, timeout=timeout)
    if device is None:
        print(
            f"Device '{name}' not found within {timeout:.0f} s.\n"
            "The device advertises once per minute; try again or use --device ADDR.",
            file=sys.stderr,
        )
        sys.exit(1)
    return device


async def upload(firmware_path: str, address: str | None) -> None:
    firmware = open(firmware_path, "rb").read()  # noqa: WPS515
    size = len(firmware)
    md5 = hashlib.md5(firmware).hexdigest()
    print(f"Firmware: {size / 1024:.0f} kB  MD5: {md5[:12]}…")

    target = await find_device(DEVICE_NAME, address)

    ota_done: asyncio.Event = asyncio.Event()
    ota_success = False

    def on_status(char: BleakGATTCharacteristic, data: bytearray) -> None:
        nonlocal ota_success
        code = data[0] if data else STATUS_ERROR
        if code == STATUS_OK:
            print("\nOTA success!")
            ota_success = True
        else:
            print(f"\nOTA error: 0x{code:02X}")
        ota_done.set()

    async with BleakClient(target, timeout=30.0) as client:
        mtu = client.mtu_size
        print(f"Connected (MTU={mtu})")
        if mtu < 50:
            print(
                f"Warning: MTU {mtu} is very small — MTU exchange may not have "
                "occurred. Upload will be slow.",
                file=sys.stderr,
            )

        await client.start_notify(STATUS_UUID, on_status)

        # Begin OTA: [CTRL_START, size as little-endian uint32]
        ctrl_start = bytes(
            [
                CTRL_START,
                size & 0xFF,
                (size >> 8) & 0xFF,
                (size >> 16) & 0xFF,
                (size >> 24) & 0xFF,
            ]
        )
        await client.write_gatt_char(CTRL_UUID, ctrl_start, response=True)
        print(f"OTA started ({size} B), uploading…")

        # Stream DATA chunks (write-without-response for throughput)
        chunk_size = max(1, mtu - 3)  # ATT overhead: 1 opcode + 2 handle
        sent = 0
        for offset in range(0, size, chunk_size):
            chunk = firmware[offset : offset + chunk_size]
            await client.write_gatt_char(DATA_UUID, chunk, response=False)
            sent += len(chunk)
            pct = sent * 100 // size
            print(f"\r  {pct:3d}%  {sent // 1024}/{size // 1024} kB", end="", flush=True)

        print(f"\r  100%  {size // 1024}/{size // 1024} kB — commit…")

        # Commit: triggers SHA-256 verify + set boot partition + restart
        await client.write_gatt_char(CTRL_UUID, bytes([CTRL_COMMIT]), response=True)
        print("Waiting for STATUS notify…")

        try:
            await asyncio.wait_for(ota_done.wait(), timeout=20.0)
        except asyncio.TimeoutError:
            print("Timeout waiting for STATUS notify.", file=sys.stderr)
            try:
                await client.write_gatt_char(
                    CTRL_UUID, bytes([CTRL_ABORT]), response=True
                )
            except Exception:
                pass
            sys.exit(1)

    if not ota_success:
        sys.exit(1)

    print("Device is restarting. BLE OTA complete.")


def main() -> None:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--no-build", action="store_true", help="Skip PlatformIO build")
    ap.add_argument(
        "--device",
        default=None,
        metavar="ADDR",
        help="BLE MAC address (skip scan); e.g. AA:BB:CC:DD:EE:FF",
    )
    args = ap.parse_args()

    if not args.no_build:
        print("Building firmware…")
        build()

    if not os.path.isfile(FIRMWARE):
        print(f"Firmware not found: {FIRMWARE}", file=sys.stderr)
        sys.exit(1)

    asyncio.run(upload(FIRMWARE, args.device))


if __name__ == "__main__":
    main()
