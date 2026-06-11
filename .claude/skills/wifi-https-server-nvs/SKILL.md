---
name: wifi-https-server-nvs
description: Jak implementovat životní cyklus WiFi a webový server tohoto zařízení — připojení k hardcoded creds, mDNS (teplomer.local), nekonečný reconnect s backoffem 1s→30s, RSSI/IP, a servírování jedné stránky (uPlot + Pico.css z CDN, výpočet času v prohlížeči, vykreslené chybějící body) + JSON API + akce, s konfigurací persistovanou v NVS. Použij pro WiFi/HTTP HAL a konektivitní moduly. POZN: default je plain HTTP bez auth; HTTPS v názvu je historický a je jen volitelné rozšíření.
---
# WiFi + mDNS + HTTP server + NVS

> **Protokol = plain HTTP** (LAN-trusted, akceptované riziko, **bez auth**). HTTPS + Basic Auth
> jsou volitelná rozšíření (název skillu je legacy).

## WiFi životní cyklus
- Připoj se přes creds ze `secrets.h`. Po úspěchu zaregistruj mDNS `cfg::net::kMdnsHostname` → `teplomer.local`.
- Dohlížej každých `cfg::net::kWifiCheckMs`; při ztrátě nastav `WIFI_DOWN` a reconnectuj s exponenciálním
  backoffem `kReconnectMinMs`→`kReconnectMaxMs` strop, nekonečně. Vystav IP + RSSI (LCD ukáže adresu krátce po connectu).

## Web API (plain HTTP, port `cfg::net::kHttpPort`)
- `GET /` → jedna HTML stránka z PROGMEM (CDN `<link>`/`<script>` pro uPlot + Pico.css).
- `GET /api/current` → JSON: teploty inner/outer, znaménkový rozdíl, doporučení okna, stav požáru +
  čidel, uptime, freeHeap, minFreeHeap, rssi, ROM ID, beepEnabled, prahy, kontrast, cíl.
- `GET /api/history` → JSON objekt: `uptime_s` (pro ukotvení osy) + `count` + pole `{inC100, outC100, flags}`
  seřazené od nejstaršího; **prohlížeč** dopočítá wall-clock jako `now − (count−1−i) × 600 s` (zařízení nemá RTC).
- `POST /api/config` → bzučák on/off, prahy, kontrast, cíl (validuj!).
- `POST /api/action/{restart,test-beep,test-email,status-email,set-contrast}`.

## Frontend (na stejné stránce)
- uPlot dvě série; **chybějící body jako null** (přerušení, ne interpolace). Dekóduj bity příznaků na log.
- Stavový panel: uptime, chyby čidel, požár, log, free/min-free heap, RSSI, ROM ID. Prominentní
  **doporučení okna** (OTEVŘÍT/ZAVŘÍT/BEZE ZMĚNY). Ovládací prvky + potvrzení destruktivních akcí.

## Perzistence & robustnost
- `nvs_store` přes HAL (Preferences); validuj vstupy; malformed request odmítni gracefully. JSON stav
  do ohraničeného statického scratch; při tlaku na paměť request zahoď (měř dál). Nikdy neblokuj přes WDT.

## Host testy
Round-trip ConfigStore (fake NVS) a všechny serializery (tvar + hodnoty).
