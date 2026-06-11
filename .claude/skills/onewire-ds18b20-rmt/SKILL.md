---
name: onewire-ds18b20-rmt
description: Jak číst dvě DS18B20, KAŽDÉ na vlastní OneWire sběrnici, přes RMT periferii ESP32-S3 — zapojení s externím VDD a 4,7k pull-upem, 12-bit rozlišení, kontrola CRC, čtení ROM ID pro identifikaci a detekce utrženého/odpojeného čidla (sentinel ~85 °C / žádné zařízení). Použij pro OneWire HAL a doménový modul čidel.
---
# DS18B20 přes OneWire (RMT, dvě oddělené sběrnice)

## Zapojení
- Jedno čidlo na GPIO sběrnici (robustnost + triviální identifikace). **Externí VDD** (NE parasite).
  Pull-up 4,7 kΩ z datové linky na 3V3 na každé sběrnici. Piny z `cfg::pin::kOneWireInside/Outside`.

## Driver
- Časování OneWire implementuj na **RMT** periferii (jeden RMT kanál na sběrnici) pro pulzy bez jitteru.
  Callbacky drž malé.
- 12-bit rozlišení (`cfg::temp::kResolutionBits`); konverze ~750 ms — spusť konverzi, vrať se, čti
  v příštím cyklu (neblokuj celou sekundu).
- Validuj **CRC** scratchpadu; při selhání → `ONEWIRE_ERR`.
- Přečti a vystav 64-bit ROM ID každého čidla pro web/diagnostiku.

## Detekce poruchy
- Žádné zařízení na sběrnici, nebo sentinel `~85,0 °C` po zapnutí/při plovoucí lince bez platné
  konverze, nebo selhání CRC → po N=3 chybách `SENSOR_OPEN` / `ONEWIRE_ERR`. Rozliš „open"
  (nikdo neodpovídá) od „bus error" (odpoví, špatné CRC), kde to jde.
- Hodnota mimo `cfg::temp::kValidMin/MaxC` (−30…80) → `WEIRD_VALUE`, vyřaď z alarmů/průměru.

## Host fake
Programovatelný per sběrnice: nastav příští teplotu, vynuť CRC chybu, vynuť open, vynuť divnou
hodnotu, hlas zvolené ROM ID.
