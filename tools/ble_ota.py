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

from bleak import BleakClient, BleakError, BleakScanner
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

    # Device advertises for ~750 ms once per minute (kSamplePeriodMs = 60 s).
    # Worst-case: scan starts right after a burst → must wait up to 60 s.
    # 65 s covers one full cycle + margin.
    #
    # We scan manually (not find_device_by_name) so we can:
    #   - match case-insensitively
    #   - match by manufacturer company ID 0xFFFF as fallback
    #   - print every new device found (helps debug wrong/unexpected names)
    print(f"Scanning for '{name}' (up to {timeout:.0f} s, device advertises ~1×/min) …")

    found_target = None
    seen: dict[str, tuple] = {}  # address → (device, adv_data)

    def callback(device, adv_data):
        nonlocal found_target
        addr = device.address.upper()
        if addr not in seen:
            seen[addr] = (device, adv_data)
            dev_name = device.name or adv_data.local_name or "?"
            print(f"  [{addr}]  {dev_name!r:24s}  RSSI={adv_data.rssi}")

        if found_target is not None:
            return

        # Match 1: case-insensitive name
        dev_name = (device.name or adv_data.local_name or "").strip()
        if dev_name.lower() == name.lower():
            found_target = device
            return

        # Match 2: contains target name (handles trailing spaces / BLE truncation)
        if name.lower() in dev_name.lower():
            found_target = device
            return

        # Match 3: manufacturer data with company ID 0xFFFF (our beacon).
        # Device name may be in the scan response (arrives later), so the ADV
        # callback can show None — that is expected; we still identified it.
        if adv_data.manufacturer_data and 0xFFFF in adv_data.manufacturer_data:
            found_target = device
            return

    scanner = BleakScanner(detection_callback=callback)
    await scanner.start()
    loop = asyncio.get_running_loop()
    end = loop.time() + timeout
    while loop.time() < end:
        if found_target is not None:
            await scanner.stop()
            display_name = found_target.name or name  # name in scan response may lag
            print(f"Found: {display_name!r} ({found_target.address})")
            return found_target
        await asyncio.sleep(1)
    await scanner.stop()

    print(f"\nDevice '{name}' not found in {timeout:.0f} s.", file=sys.stderr)
    if seen:
        print(
            "Devices seen during scan (use --device ADDR to connect directly):",
            file=sys.stderr,
        )
        for addr, (dev, adv) in sorted(seen.items()):
            n = dev.name or adv.local_name or "?"
            print(f"  {addr}  {n!r}", file=sys.stderr)
    else:
        print("No BLE devices seen at all — check adapter and proximity.", file=sys.stderr)
    sys.exit(1)


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

    disconnected = asyncio.Event()

    def on_disconnect(_: BleakClient) -> None:
        print("\n[!] Device disconnected.", file=sys.stderr)
        disconnected.set()

    try:
        async with BleakClient(target, timeout=30.0,
                               disconnected_callback=on_disconnect) as client:
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

            # Stream DATA chunks with response=True for per-chunk flow control.
            # response=False (write-without-response) flooded NimBLE's MSYS pool
            # and caused silent disconnects; response=True limits in-flight data
            # to one chunk at a time and surfaces any write error immediately.
            chunk_size = max(1, mtu - 3)  # ATT overhead: 1 opcode + 2 handle
            sent = 0
            for offset in range(0, size, chunk_size):
                if disconnected.is_set():
                    print(f"\n[!] Lost connection at {sent}/{size} B.", file=sys.stderr)
                    sys.exit(1)
                chunk = firmware[offset : offset + chunk_size]
                await client.write_gatt_char(DATA_UUID, chunk, response=True)
                sent += len(chunk)
                pct = sent * 100 // size
                print(f"\r  {pct:3d}%  {sent // 1024}/{size // 1024} kB", end="", flush=True)

            print(f"\r  100%  {size // 1024}/{size // 1024} kB — commit…")

            # Commit: triggers SHA-256 verify + set boot partition + restart.
            # response=True: firmware now defers esp_restart() to a separate task
            # so onWrite() returns and NimBLE can send the ATT Write Response.
            await client.write_gatt_char(CTRL_UUID, bytes([CTRL_COMMIT]), response=True)
            print("Waiting for STATUS notify…")

            try:
                await asyncio.wait_for(ota_done.wait(), timeout=60.0)
            except asyncio.TimeoutError:
                print("Timeout waiting for STATUS notify.", file=sys.stderr)
                try:
                    await client.write_gatt_char(
                        CTRL_UUID, bytes([CTRL_ABORT]), response=True
                    )
                except Exception:
                    pass
                sys.exit(1)

    except BleakError as exc:
        # After a successful OTA the device restarts; BleakClient.__aexit__
        # may throw "Unreachable" while trying to disconnect a gone device.
        # Treat that as non-fatal when the STATUS notify already confirmed success.
        if not ota_success:
            print(f"BLE error: {exc}", file=sys.stderr)
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
