---
name: uart-logging
description: Jak implementovat ukecané strukturované UART logování tohoto firmwaru — USB sériová linka 115200, leveled log (TRACE/DEBUG/INFO/WARN/ERROR), formát [+uptime_ms][LEVEL][MODULE] zpráva, bez alokace v hot path a bez secrets v logu. Použij pro event_log modul a UART log sink v HAL.
---
# UART logování (USB 115200)

## Formát (FR-25)
`[+uptime_ms][LEVEL][MODULE] zpráva` — např. `[+0123456][WARN][sensor] inner CRC fail (2/3)`.
Úrovně `cfg::log::Level` (TRACE/DEBUG/INFO/WARN/ERROR); práh `cfg::log::kMinLevel`.

## Implementace
- `core/event_log` formátuje řádek do **statického** bufferu (žádná alokace v hot path); HAL `LogSink`
  (UART 115200) ho vypíše. Doména volá log přes rozhraní, ne přímo `Serial`.
- Uptime ber z HAL hodin (`esp_timer`/millis). Modul = krátký tag (`sensor`, `alarm`, `wifi`, `web`, `ble`, `mail`).
- Loguj všechny události a změny stavu (boot, vzorky mimo rozsah, hrany alarmů, WiFi up/down, odeslání/selhání e-mailu, config change).

## Pravidla
- **Nikdy neloguj secrets** (WiFi heslo, SMTP app password).
- Log nesmí blokovat tak, aby ohrozil WDT; při zahlcení raději zahoď řádek než blokuj.
- Stejný `event_log` plní i příznaky pro historii a web (jeden zdroj událostí).

## Host
Fake `LogSink` zachytává řádky → testy můžou tvrdit, že se zalogovala správná událost a úroveň.
