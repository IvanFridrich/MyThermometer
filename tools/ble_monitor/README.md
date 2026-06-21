# BLE monitor (`tools/ble_monitor`)

Windows CLI that confirms what the thermometer beacon advertises — it decodes the
exact manufacturer-specific payload the firmware produces (`SPECIFICATION.md §6.2`,
firmware `core/ble_payload`). FR-28.

## Install & run

```sh
pip install -r requirements.txt        # or: pip install bleak
python monitor.py                      # print readings to the console
python monitor.py --log readings.csv   # also append to a CSV file
python monitor.py --selftest           # decode known vectors, no radio needed
```

Example output:

```
Scanning for company ID 0xFFFF ... Ctrl-C to stop
[2026-06-19 21:04:11] inner=23.45°C outer=21.00°C flags=INNER_VALID,OUTER_VALID seq=42 rssi=-67dBm
```

## Payload contract (keep in sync with the firmware)

bleak strips the 2-byte company ID into the `manufacturer_data` key, so the value
decoded here is the remaining 7 bytes (little-endian):

| byte | field | notes |
|------|-------|-------|
| 0 | version | must be `0x01` (`cfg::ble::kPayloadVersion`) |
| 1..2 | T_inner | int16 centi-°C; `/100` → °C |
| 3..4 | T_outer | int16 centi-°C |
| 5 | flags | bit0 DIFF_EXCEEDED, bit1 FIRE, bit2 SENSOR_OPEN, bit3 ONEWIRE_ERR, bit4 INNER_VALID, bit5 OUTER_VALID |
| 6 | seq | 0..255 wrap |

A temperature whose validity bit is clear is shown as `n/a` (never a bogus value).
The byte layout is a **shared contract** with the firmware (`ble-advertising-nimble`
skill); change both sides together and bump the version byte.
