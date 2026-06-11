---
name: ble-advertising-nimble
description: Jak postavit non-connectable BLE advertising beacon na ESP32-S3 s NimBLE — zabalení obou teplot a příznaku překonání rozdílu do manufacturer-specific dat a plánování 5 advertising burstů za minutu, pak ticho. Použij pro BLE HAL a beacon modul; spáruj s python-bleak-monitor pro dekodér. Byte layout je sdílený zdroj pravdy s tím skillem.
---
# BLE advertising beacon (NimBLE, non-connectable)

## Payload (little-endian, manufacturer-specific data; 9 B — SPECIFICATION.md §6.2)
```
[0..1] company ID 0xFFFF (cfg::ble::kCompanyId, LE)
[2]    version = 0x01 (cfg::ble::kPayloadVersion)
[3..4] T_inner  int16 centi-°C (LE)
[5..6] T_outer  int16 centi-°C (LE)
[7]    flags: bit0 DIFF_EXCEEDED, bit1 FIRE, bit2 SENSOR_OPEN,
              bit3 ONEWIRE_ERR, bit4 INNER_VALID, bit5 OUTER_VALID
[8]    seq (0..255, wrap)
```
Enkodér drž v doménové vrstvě a testuj **byte-exact** na hostu.

## Advertising
- **Non-connectable** (jen beacon; uživatel nechce připojení).
- Každou minutu vyšli `cfg::ble::kBurstsPerMinute` (5) advertů s rozestupem `kBurstSpacingMs`,
  adv interval `kAdvIntervalMin/MaxMs`, pak ticho do další minuty. Jméno `cfg::ble::kDeviceName`.
- Běží na Core 0 vedle WiFi. Heap NimBLE drž ohraničený (bez PSRAM).

## Pozor
- Hlídej celkovou velikost AD payloadu (manufacturer data + jméno se musí vejít do adv PDU, 31 B legacy).
- 0xFFFF company ID je OK pro testy; pokud získáš reálné, zdokumentuj.
