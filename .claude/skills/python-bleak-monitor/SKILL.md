---
name: python-bleak-monitor
description: Jak postavit Windows Python nástroj s knihovnou bleak, který scanuje BLE advertising tohoto zařízení a dekóduje manufacturer-specific payload (obě teploty + příznaky + seq) podle sdíleného byte layoutu, a vypisuje čtení s časem PC a RSSI. Použij pro tools/ble_monitor; drž byte format v souladu se skillem ble-advertising-nimble a §6.2.
---
# Python BLE monitor (bleak, Windows)

## Účel
Doprovodný CLI nástroj, co potvrdí, co zařízení vysílá — dekóduje stejný payload, jaký produkuje firmware.

## Implementace
- `bleak` `BleakScanner` s detection callbackem; filtruj podle company ID `0xFFFF` v manufacturer datech
  (a/nebo jména zařízení).
- Dekóduj **little-endian** přesně dle `SPECIFICATION.md §6.2`:
  company(2)·version(1)·T_inner(int16)·T_outer(int16)·flags(1)·seq(1).
  `temp = int16_le / 100.0` → °C. Rozbal bity příznaků: DIFF_EXCEEDED, FIRE, SENSOR_OPEN, ONEWIRE_ERR,
  INNER_VALID, OUTER_VALID.
- Výpis: čas PC, inner/outer °C, příznaky, RSSI, seq. Volitelně append do CSV/log souboru.
- Defenzivně ošetři krátké/garbage manufacturer pakety a rozdíly mezi BLE adaptéry na Windows.

## Spuštění
Doplň do README: `pip install bleak`, spuštění skriptu, volitelně `--log soubor.csv`.

## Pozor
Byte layout je **sdílený kontrakt** s firmwarem (skill `ble-advertising-nimble`). Změna na jedné straně
bez druhé rozbije dekódování — drž je v souladu (verze v bajtu [2]).
