#!/usr/bin/env python3
"""BLE monitor for the ESP32-S3 thermometer beacon (Windows / bleak).

Decodes the manufacturer-specific advertising payload (SPECIFICATION.md §6.2,
shared contract with firmware core/ble_payload) and prints inner/outer
temperatures, decoded flags, sequence number and RSSI with the PC clock.

Byte layout (little-endian, §6.2): the firmware transmits 9 bytes starting with
the 2-byte company ID. bleak parses the company ID out as the manufacturer_data
*key*, so the value handed to decode() is the remaining 7 bytes:

    [0]    version            (== 0x01)
    [1..2] T_inner  int16 LE  (centi-°C)
    [3..4] T_outer  int16 LE  (centi-°C)
    [5]    flags
    [6]    seq

Usage:
    pip install bleak
    python monitor.py                 # print readings to the console
    python monitor.py --log out.csv   # also append to a CSV file
    python monitor.py --selftest      # decode known vectors, no radio needed
"""
from __future__ import annotations

import argparse
import asyncio
import csv
import struct
import sys
from datetime import datetime
from typing import Optional

COMPANY_ID = 0xFFFF  # cfg::ble::kCompanyId
PROTO_VERSION = 0x01  # cfg::ble::kPayloadVersion
PAYLOAD_LEN = 7  # company ID already stripped by bleak

# (bit, name) — §6.2 flags byte.
FLAG_BITS = [
    (0, "DIFF_EXCEEDED"),
    (1, "FIRE"),
    (2, "SENSOR_OPEN"),
    (3, "ONEWIRE_ERR"),
    (4, "INNER_VALID"),
    (5, "OUTER_VALID"),
]


def decode(value: bytes) -> Optional[dict]:
    """Decode the 7-byte (company-stripped) §6.2 payload, or None if invalid.

    Temperatures are returned in °C, or None when the matching validity bit is
    clear (so a disconnected sensor is reported as 'n/a', never a bogus value).
    """
    if value is None or len(value) < PAYLOAD_LEN:
        return None
    if value[0] != PROTO_VERSION:
        return None
    t_inner, t_outer = struct.unpack_from("<hh", value, 1)  # two signed LE int16
    flags = value[5]
    seq = value[6]
    inner_valid = bool(flags & (1 << 4))
    outer_valid = bool(flags & (1 << 5))
    return {
        "inner_c": (t_inner / 100.0) if inner_valid else None,
        "outer_c": (t_outer / 100.0) if outer_valid else None,
        "flags": flags,
        "seq": seq,
    }


def flag_names(flags: int) -> str:
    names = [name for bit, name in FLAG_BITS if flags & (1 << bit)]
    return ",".join(names) if names else "-"


def _fmt_temp(value: Optional[float]) -> str:
    return f"{value:.2f}" if value is not None else "n/a"


def _format_line(now: str, decoded: dict, rssi) -> str:
    return (
        f"[{now}] inner={_fmt_temp(decoded['inner_c'])}°C "
        f"outer={_fmt_temp(decoded['outer_c'])}°C "
        f"flags={flag_names(decoded['flags'])} seq={decoded['seq']} rssi={rssi}dBm"
    )


def _selftest() -> int:
    """Decode vectors that mirror firmware test/test_ble_payload (company stripped)."""
    # encode(2345, 2100, FIRE|DIFF_EXCEEDED, 42) -> FF FF 01 29 09 34 08 33 2A
    d = decode(bytes([0x01, 0x29, 0x09, 0x34, 0x08, 0x33, 0x2A]))
    assert d is not None
    assert abs(d["inner_c"] - 23.45) < 1e-9, d
    assert abs(d["outer_c"] - 21.00) < 1e-9, d
    assert d["seq"] == 42 and d["flags"] == 0x33, d
    assert flag_names(0x33) == "DIFF_EXCEEDED,FIRE,INNER_VALID,OUTER_VALID"

    # Negative inner (-12.34 = 0xFB2E LE) with only OUTER valid -> inner is n/a.
    d = decode(bytes([0x01, 0x2E, 0xFB, 0x34, 0x08, 0x20, 0x07]))
    assert d["inner_c"] is None, d  # INNER_VALID clear
    assert abs(d["outer_c"] - 21.00) < 1e-9, d

    # Wrong version / short / empty are rejected.
    assert decode(bytes([0x02, 0, 0, 0, 0, 0, 0])) is None
    assert decode(bytes([0x01, 0x00])) is None
    assert decode(b"") is None
    print("selftest OK")
    return 0


async def _scan(on_detect) -> None:
    from bleak import BleakScanner  # lazy: keeps decode() importable without bleak

    scanner = BleakScanner(detection_callback=on_detect)
    await scanner.start()
    try:
        while True:
            await asyncio.sleep(1.0)
    finally:
        await scanner.stop()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="ESP32-S3 thermometer BLE monitor")
    parser.add_argument("--log", metavar="CSV", help="append readings to this CSV file")
    parser.add_argument("--selftest", action="store_true", help="decode known vectors and exit")
    args = parser.parse_args(argv)

    if args.selftest:
        return _selftest()

    log_file = None
    writer = None
    if args.log:
        log_file = open(args.log, "a", newline="", encoding="utf-8")
        writer = csv.writer(log_file)
        if log_file.tell() == 0:
            writer.writerow(["pc_time", "inner_c", "outer_c", "flags", "seq", "rssi"])

    def on_detect(_device, adv):
        # Defensive: adapters differ; manufacturer_data may be absent or short.
        decoded = decode(adv.manufacturer_data.get(COMPANY_ID))
        if decoded is None:
            return
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(_format_line(now, decoded, adv.rssi))
        if writer is not None:
            writer.writerow([now, _fmt_temp(decoded["inner_c"]), _fmt_temp(decoded["outer_c"]),
                             flag_names(decoded["flags"]), decoded["seq"], adv.rssi])
            log_file.flush()

    print(f"Scanning for company ID 0x{COMPANY_ID:04X} ... Ctrl-C to stop")
    try:
        asyncio.run(_scan(on_detect))
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if log_file is not None:
            log_file.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
