# ESP32 Teplomer — Specifikace a fázový plán

> Zadání pro Claude Code. Jeden zdroj pravdy pro požadavky, architekturu, datové
> struktury a akceptační kritéria. Agenti v `.claude/agents/` a skilly v
> `.claude/skills/` na tento dokument odkazují. Globální pravidla jsou v `CLAUDE.md`.

---

## 0. Rozhodnutí a předpoklady (potvrď / vetuj)

Rozhodnutí aktualizovaná podle tvých detailních odpovědí. Položky 🔶 čekají na potvrzení/veto. Cokoliv změň jednou větou. Všechny laditelné konstanty žijí v `include/Config.h` (jeden zdroj pravdy — „žádné magické konstanty").

| # | Téma | Zvolené řešení | Lze snadno změnit? |
|---|------|----------------|--------------------|
| D1 | MCU | **ESP32-S3** (dual-core, **bez PSRAM**) | — |
| D2 | Napájení | 5 V z USB síťového zdroje → 3,3 V LDO, bez zálohy, poklesy očekávané | jen práh BOD |
| D3 | DS18B20 | dvě čidla, **každé na vlastním GPIO** (čistší detekce utržení) | ano |
| D4 | Role čidel | `inner` = požár (45 °C), `outer` = okolí; rozdíl mezi nimi | — |
| D5 | Rozlišení DS18B20 | 12-bit (0,0625 °C) | ano |
| D6 | LCD | HD44780, 2×8, 4-bit, lib `LiquidCrystal` | — |
| D7 | Bzučák | **pasivní**, LEDC PWM přes NPN tranzistor (umožní vzory) | ano (aktivní = 1 řádek) |
| D8 | Podsvícení LCD | natvrdo zapnuté | volitelně řízené |
| D9 | Rozdílový alarm | `abs(avg_in − avg_out) ≥ 2,0 °C`, z 10min průměru | prahy v configu |
| D10 | Hystereze rozdílu | set 2,0 °C / clear 1,5 °C | config |
| D11 | Požár | z **okamžité** hodnoty inner ≥ 45 °C, clear 43 °C | config |
| D12 | Web protokol | **plain HTTP** (LAN-trusted, **bez autentizace** — akceptované riziko) | HTTPS + Basic Auth jako rozšíření |
| D13 | Graf knihovna | **uPlot** přes CDN (lehké, řeší mezery) | ano |
| D14 | CSS knihovna | **Pico.css** přes CDN | ano |
| D15 | mDNS | `teplomer.local` (kromě IP na LCD) | ano |
| D16 | BLE | non-connectable beacon, manufacturer data, 5 burstů/min | — |
| D17 | E-mail | Gmail SMTP (`ESP-Mail-Client`), creds v `secrets.h` (gitignored) | provider |
| D18 | E-mail logika | jen na náběžnou hranu, max 1×/h/typ, perzistence neposílá | viz §7 |
| D19 | Test framework | **doctest** (header-only, kompatibilní s `-fno-exceptions`) | GoogleTest/Unity |
| D20 | HAL | **link-time seam** (cíl `*_target.cpp` vs host `*_fake.cpp`), bez virtuálů | — |
| D21 | Chyby | bez výjimek, `-fno-exceptions -fno-rtti`, návratový `Result<T>` | — |
| D22 | Kontejnery | ETL (Embedded Template Library), statická alokace | — |
| D23 | Fyzická tlačítka | žádná kromě napájení/EN; „reset" je SW restart z webu | ano |
| D24 | DMA | viz §8.4 — pro tuto periferní sadu reálně N/A, místo toho RMT+LEDC | — |
| D25 | Python monitor | `bleak`, CLI (volitelný živý graf jako rozšíření) | ano |

---

## 1. Přehled systému

Orientační teploměr se dvěma čidly DS18B20 na bázi ESP32. Měří 1×/min, drží 10min
plovoucí průměr, ~24 h historie v RAM (1 vzorek / 10 min) s příznaky událostí.
Hlásí akustické alarmy (rozdíl teplot, požár, porucha čidla), vysílá teploty přes
BLE advertising, poskytuje HTTP webovou stránku s grafem a konfigurací, loguje vše
po UART a posílá e-maily při kritických událostech. Důraz na robustnost (výpadky
napájení/WiFi, utržení čidla, šum) a na minimální dynamickou alokaci.

Čas je čistě orientační (bez RTC). Zařízení drží jen **relativní** čas od bootu;
reálné timestampy dopočítává webová stránka v prohlížeči z času prohlížeče.

### 1.1 Bloky
```
                 ┌──────────────── ESP32 (dual-core) ────────────────┐
  DS18B20 inner ─┤ OneWire(RMT) ┐                                     │
  DS18B20 outer ─┤ OneWire(RMT) ┘→ Sensors → Measurement → History    │
                 │                                  │                  │
                 │                          AlarmStateMachine          │
                 │                          /    │       \             │
                 │                     Beeper   LCD    Notifier(mail)   │
   LCD 2x8 4bit ─┤ HD44780                                             │
   Buzzer       ─┤ LEDC PWM                                            │
   Contrast V0  ─┤ LEDC PWM → RC                                       │
                 │   Core0: WiFi + HTTP server + BLE adv + mail        │
                 │   Core1: measurement + alarms + LCD + history       │
   USB UART ─────┤ Log 115200                                          │
                 └────────────────────────────────────────────────────┘
```

---

## 2. Funkční požadavky (FR)

**Měření**
- **FR-01** Měřit teplotu obou čidel 1×/min (perioda fixní 60 s).
- **FR-02** Vést 10min plovoucí průměr (okno = 10 vzorků) zvlášť pro každé čidlo. Okno se kryje s krokem historie — každý záznam je průměr přesně posledních 10 měření.
- **FR-03** Detekovat „divné" hodnoty: mimo platný rozsah **−30…+80 °C** → vzorek označit `INVALID` a vyřadit z průměru (flag `WEIRD_VALUE`). (Volitelně sekundárně i nepravděpodobný skok > 10 °C/vzorek — viz rozšíření.)
- **FR-04** Detekovat poruchu OneWire: CRC chyba, žádné zařízení, sentinel +85/−127 °C → po N=3 po sobě jdoucích chybách flag `SENSOR_OPEN`/`ONEWIRE_ERR`.

**Historie (RAM)**
- **FR-05** Kruhový buffer 144 záznamů (24 h × 1/10 min) ve **statické** paměti; po ztrátě napájení smazání je OK.
- **FR-06** Jeden záznam = obě teploty (10min průměr v okamžiku vzorku) a OR všech příznaků událostí v daném 10min okně. Timestamp se nepersistuje (§6.1). Struktura viz §6.1.

**Alarmy**
- **FR-07** Rozdíl: když `abs(avg_in − avg_out) ≥ diff_threshold` → krátké dvojité pípnutí (jednorázové při náběžné hraně) + flag `DIFF_EXCEEDED`; clear s hysterezí.
- **FR-08** Požár: když okamžité `inner ≥ fire_threshold` (default 45 °C) → opakovaný alarm dokud trvá; clear s hysterezí; flag `FIRE`; spustí e-mail.
- **FR-09** Porucha čidla → distinktivní zvukový vzor + flag + e-mail.
- **FR-10** Detekce požáru jen u `inner`.

**LCD (2×8 = 16 znaků, stránkování)**
- **FR-11** Řádek 1 trvale: `I:23.4` / přepínat na `O:21.1`; řádek 2 rotuje stránkami: IP, uptime, stav/chyby. Rotace ~3 s/strana (konfig. konstanta).
- **FR-12** Po bootu zobrazit přidělenou IP adresu (a `teplomer.local`).
- **FR-13** Kontrast řízen PWM (LEDC) → RC filtr → V0; hodnota v configu.

**WiFi**
- **FR-14** Po startu připojení k hardcoded SSID/heslu (v `secrets.h`). DHCP, zobrazit IP.
- **FR-15** Když WiFi není dostupná, opakovat pokusy (exponenciální backoff, strop 30 s). Při ztrátě spojení reconnect (totéž). Indikace na LCD (`WiFi DN`).
- **FR-16** mDNS hostname `teplomer.local`.

**Web (HTTP)**
- **FR-17** Jedna stránka: aktuální teploty, graf historie. Timestampy a osu času počítá JS v prohlížeči z `Date.now()` a relativních offsetů z API.
- **FR-18** Graf přes `uPlot` (CDN `<link>`/`<script>`), styl přes `Pico.css` (CDN). V grafu **zobrazit chybějící body** (přerušení čáry tam, kde vzorek chybí nebo je `INVALID`).
- **FR-19** Zobrazit také: uptime, chyby čidel, požár, log událostí (z historie/flagů), free heap, minimum free heap, RSSI WiFi, ID (ROM) obou čidel.
- **FR-20** Konfigurace přes web: bzučák on/off, prahy (rozdíl/hystereze/požár), kontrast, e-mail on/off. Uložit do flash (NVS).
- **FR-21** Akce přes web: SW restart, test bzučáku, set contrast (živě), poslat test e-mail, vyžádat status e-mail (obejde rate-limit).
- **FR-22** Web **bez autentizace** (LAN-trusted, akceptované riziko — kdokoliv na síti může spouštět akce). Basic Auth / HTTPS jsou volitelné rozšíření (viz §12).
- **FR-23** API endpoint(y) vrací JSON: aktuální stav, historie, diagnostika. (Stránka je statická z PROGMEM, data tahá fetchem.)

**BLE**
- **FR-24** Každou minutu vyslat advertising s teplotami a příznakem překonání rozdílu; každý paket vyslat 5× (burst ~5× á 100 ms), pak klid do další minuty. Non-connectable. Formát manufacturer data §6.2.

**UART**
- **FR-25** USB UART 115200 Bd, ukecaný strukturovaný log všech událostí: `[+uptime_ms][LEVEL][MODULE] zpráva`. Úrovně TRACE/DEBUG/INFO/WARN/ERROR.

**E-mail**
- **FR-26** Poslat e-mail při požáru a při utržení/poruše čidla; creds hardcoded v `secrets.h`. Logika rate-limitu viz §7.

**Konfigurace / perzistence**
- **FR-27** Všechny konfig. položky persistovat v NVS (flash). Default hodnoty při prázdné NVS.

**Python monitor**
- **FR-28** Skript pro Windows (knihovna `bleak`) — scanuje a dekóduje advertising pakety, vypisuje teploty + příznaky + RSSI s časem prohlížeče/PC.

---

## 3. Nefunkční požadavky (NFR)

- **NFR-01 Robustnost:** zařízení přežije bez restartu výpadek WiFi, utržení čidla, šum a krátkodobý pokles napětí (nad BOD práh). Žádný stav nesmí vést k uváznutí.
- **NFR-02 WDT (povinné):** task WDT s registrací všech aplikačních tasků; timeout 8 s; při zaseknutí reset. Interrupt WDT zapnutý.
- **NFR-03 Brownout (povinné):** zapnutý hardwarový brownout detektor s nastaveným prahem (`CONFIG_ESP_BROWNOUT_DET_LVL`), čistý reset při poklesu.
- **NFR-04 Paměť:** minimální dynamická alokace; v ustáleném stavu **žádná** alokace na heapu. Statické task stacky, statické buffery, ETL kontejnery. Každá nevyhnutelná alokace má ošetřené selhání (návratový kód + log).
- **NFR-05 Bez výjimek a bez RTTI:** `-fno-exceptions -fno-rtti`. Chyby návratovým typem `Result<T>` / status enum.
- **NFR-06 Minimum polymorfismu:** žádné runtime virtuální rozhraní v produkčním kódu; HW abstrakce přes link-time seam (D20) a compile-time (šablony/policy) tam, kde je třeba.
- **NFR-07 Dvě jádra:** WiFi+BLE a jejich servisní tasky na Core 0; měření/alarmy/LCD/historie na Core 1 (izolace časově kritické logiky od síťových stacků).
- **NFR-08 HW periferie:** OneWire přes RMT, PWM přes LEDC (offload CPU). Viz §8.4 k DMA.
- **NFR-09 Bezpečnost:** základní MPU/ochrany (stack-smashing protection, nespustitelný stack, oddělení IRAM/DRAM) — v rozsahu, který ESP32 reálně umožňuje. Web auth na akce. Žádné secrets v gitu.
- **NFR-10 Testovatelnost:** veškerý hardware za HAL rozhraním; doménová logika 100% spustitelná a testovatelná na hostu (Windows) bez ESP32.
- **NFR-11 Kvalita:** clang-format, clang-tidy bez warningů (politika v `.clang-tidy`), IWYU čisté includy, ASan/UBSan na host testech, code coverage (cíl ≥ 85 % řádků doménové logiky), volitelně SonarQube. Viz README a skill `static-analysis-coverage`.
- **NFR-12 Modularita:** raději více menších modulů s úzkým rozhraním (konzultace s `architect`). Žádný „god object".

---

## 4. Architektura a moduly

Návrh modulů (raději jemnější dělení; finální podobu schvaluje `architect`). Každý modul
= veřejná hlavička v `include/` + implementace v `src/`. HW moduly mají dvě implementace
(`*_target.cpp` pro ESP32, `*_fake.cpp` pro host).

**Doménová vrstva (čistá, bez HW, plně testovatelná na hostu):**
- `core/result` — `Result<T>`, status enumy, `Temperature` typ (centi-°C, int16).
- `core/moving_average` — fixní 10-prvkové okno, integer.
- `core/history_buffer` — statický ring buffer 144 záznamů, append + dump.
- `core/alarm_state` — stavový automat rozdíl/požár/porucha + hystereze + edge detekce.
- `core/anomaly` — detekce divných hodnot / poruch.
- `core/config_model` — datový model configu + default + validace.
- `core/clock` — relativní čas (abstrakce nad zdrojem tiku).
- `core/event_log` — fronta/formátování logových událostí.

**HAL (rozhraní + dvě implementace):**
- `hal/onewire_bus` — čtení DS18B20 (target: RMT; host: fake).
- `hal/display` — LCD primitiva (target: LiquidCrystal; host: fake/RAM buffer).
- `hal/pwm` — kanály pro kontrast a bzučák (target: LEDC; host: fake).
- `hal/wifi` — connect/stav/RSSI/IP (target: Arduino WiFi; host: fake).
- `hal/http_server` — registrace handlerů (target: esp_http_server / WebServer, plain HTTP; host: fake).
- `hal/ble_advertiser` — nastavení/odeslání adv (target: NimBLE; host: fake).
- `hal/mailer` — odeslání e-mailu (target: ESP-Mail-Client; host: fake).
- `hal/nvs_store` — perzistence (target: Preferences; host: in-memory/soubor).
- `hal/system` — restart, free heap, min heap, WDT feed, brownout info.

**Aplikační vrstva (kompozice, tasky):**
- `app/measurement_task`, `app/lcd_task`, `app/ble_task`, `app/web_task`, `app/mail_task`, `app/supervisor` (WDT), `app/wiring` (sestavení závislostí, GPIO mapa, konstanty).

**Web/Python:**
- `web/` — `index.html` + `app.js` (uPlot, fetch, výpočet času) — embedováno do firmware (PROGMEM) build skriptem.
- `tools/ble_monitor/` — Python `bleak` skript.

---

## 5. Hardware — výchozí zapojení (návrh, doladit s uživatelem)

> GPIO mapa je **návrh / placeholder** — finální piny potvrď podle své desky a zapiš
> je do `include/Config.h` (`cfg::pin::*`, jediný zdroj pravdy). ESP32-S3 pozor:
> nepoužívej strapping piny (0, 3, 45, 46), piny nativního USB-JTAG (19, 20),
> ani piny SPI flash. GPIO 22–25 na S3 **neexistují**.

| Funkce | GPIO (cfg::pin) | Pozn. |
|--------|------|-------|
| DS18B20 inner (data) | 4 | vlastní OneWire sběrnice; pull-up 4,7 kΩ na 3V3, externí VDD |
| DS18B20 outer (data) | 5 | vlastní OneWire sběrnice; pull-up 4,7 kΩ na 3V3, externí VDD |
| LCD RS | 15 | |
| LCD EN | 16 | |
| LCD D4 | 17 | |
| LCD D5 | 18 | |
| LCD D6 | 8 | |
| LCD D7 | 6 | GPIO 3 je strapping pin na ESP32-S3 — přesunuto na 6 |
| LCD V0 (kontrast) | 10 | LEDC → RC filtr (~1 kΩ + 10 µF) → V0 |
| Bzučák | 9 | LEDC → NPN (báze přes ~1 kΩ) → pasivní bzučák |
| LCD podsvícení | 3V3 | natvrdo (volitelně GPIO 11 + tranzistor, `cfg::kBacklightControlled`) |

LCD RW na GND (jen zápis). Každé DS18B20 na vlastní data pinu → spolehlivá detekce „čidlo pryč" a snadná identifikace.

---

## 6. Datové struktury (kontrakty — neměnit bez review)

### 6.1 Záznam historie (packed, 6 B; 144× = 864 B static)

Timestamp se nepersistuje — web frontend ho dopočítá jako
`now − (count−1−i) × kHistoryStrideMs` z pozice záznamu v ring bufferu.
API vrátí `uptime_s` a `count` pro ukotvení osy.

```c
struct __attribute__((packed)) HistoryRecord {
    int16_t  t_inner_c100;   // centi-°C; INT16_MIN = neplatné
    int16_t  t_outer_c100;   // centi-°C; INT16_MIN = neplatné
    uint16_t flags;          // OR příznaků v 10min okně (viz EventFlag)
};
```

### 6.2 BLE manufacturer data (9 B, vejde se do 31B adv)
```
[0..1] company ID 0xFFFF (test/dev, little-endian)
[2]    protocol version = 0x01
[3..4] T_inner  int16 centi-°C (LE)
[5..6] T_outer  int16 centi-°C (LE)
[7]    flags: bit0 DIFF_EXCEEDED, bit1 FIRE, bit2 SENSOR_OPEN,
              bit3 ONEWIRE_ERR, bit4 INNER_VALID, bit5 OUTER_VALID
[8]    seq (0..255, wrap)
```

### 6.3 Příznaky událostí (uint16 bitfield)
`BOOT, FIRE, SENSOR_OPEN, ONEWIRE_ERR, DIFF_EXCEEDED, WEIRD_VALUE, WIFI_DOWN,
WIFI_UP, EMAIL_SENT, EMAIL_FAILED, INNER_INVALID, OUTER_INVALID,
BROWNOUT_RECOVER, CONFIG_CHANGED.`

Poznámka k DIFF_EXCEEDED: jeden bit, bez směru. Směr (which sensor is hotter)
je derivovatelný z hodnot teplot (avg_inner vs avg_outer) a předává se v JSON
API jako samostatné pole, nikoli jako příznak. BLE payload §6.2 je nezměněn
(receiver si směr dopočítá z T_inner/T_outer v paketu).

### 6.4 Konfigurace (NVS)
`beeper_enabled(bool=true), diff_threshold_c100(int16=200),
diff_hysteresis_c100(int16=50), fire_threshold_c100(int16=4500),
fire_hysteresis_c100(int16=200), lcd_contrast_pwm(uint8=128),
email_enabled(bool=true), window_goal(uint8=0 → CoolRoom).`
Perioda měření (60 s) a krok historie (600 s) jsou fixní konstanty (mimo NVS).

---

## 7. Logika e-mailových alarmů (přesná definice — D18)

E-mail-worthy alarmy: **FIRE** a **SENSOR_FAULT** (`SENSOR_OPEN`/`ONEWIRE_ERR`).
Pro každý typ se drží `last_auto_sent_ms` a předchozí stav (pro detekci hrany).

- Automatický e-mail se pošle **pouze** když nastane *náběžná hrana* alarmu (alarm
  vznikl) **a zároveň** `now − last_auto_sent[typ] ≥ EMAIL_MIN_INTERVAL (3600 s)`.
- **Perzistence sama o sobě e-mail nikdy neposílá.** Pokud alarm trvá a neodezněl,
  další automatický e-mail nepřijde, i kdyby uplynula hodina.
- Pokud alarm odezní a vznikne znovu, je to nová hrana → e-mail (stále max 1×/h/typ).
- Uživatel si může přes web kdykoliv vyžádat **status e-mail** (manuální, obejde
  rate-limit). To je jediná cesta, jak dostávat e-maily „častěji".
- Když `email_enabled = false`, neposílá se nic (manuální požadavek z webu vrátí chybu/hlášku).
- Každý pokus o odeslání loguje výsledek; úspěch → flag `EMAIL_SENT`, neúspěch → `EMAIL_FAILED`.

---

## 8. Robustnost, bezpečnost, jádra, DMA

### 8.1 Watchdog
`esp_task_wdt`, timeout 8 s, `panic=true`. Každý aplikační task se registruje a
periodicky resetuje WDT přes `supervisor`/`hal/system`. Žádný blokující call bez feedu.

### 8.2 Brownout
Hardwarový BOD zapnutý, práh přes `sdkconfig` build flag (`CONFIG_ESP_BROWNOUT_DET_LVL`,
návrh úroveň odpovídající ~2,9–3,0 V). Po brownoutu čistý reset; po startu flag
`BROWNOUT_RECOVER` pokud reset reason odpovídá.

### 8.3 MPU / bezpečnost (základní — D24/NFR-09)
ESP32 nemá plnohodnotné MPU; reálně dostupné: `-fstack-protector-strong`, nespustitelný
stack, ponechané default region permissions. Web je **bez autentizace** (LAN-trusted,
akceptované riziko); Basic Auth / HTTPS jsou rozšíření. Secrets jen v `secrets.h`
(gitignored). Žádná ambice na víc — v dokumentaci jasně uvést limity.

### 8.4 DMA — poznámka (D24)
Pro tuto periferní sadu **DMA reálně nemá uplatnění**: LCD je 4-bit paralelní GPIO
(bez DMA), OneWire běží přes **RMT** (HW časování, offload CPU, ne DMA), kontrast a
bzučák přes **LEDC** (ne DMA). DMA by dávalo smysl jen u SPI/I2S/ADC continuous, které
zde nejsou. Požadavek „maximálně DMA" tedy naplníme maximem **HW periferií (RMT/LEDC)**,
nikoliv vynuceným DMA. Pokud chceš přidat SPI displej/SD/I2S, DMA tam zapojíme.

### 8.5 Rozložení tasků na jádra
- **Core 0:** WiFi stack, BLE/NimBLE stack, `web_task` (HTTP handlery), `ble_task` (adv bursty), `mail_task`.
- **Core 1:** `measurement_task`, `alarm` vyhodnocení, `lcd_task`, `history`, `supervisor`.
- Tasky vytvořené `xTaskCreateStaticPinnedToCore` (statické stacky). Mezivláknová
  komunikace přes statické fronty/štíty (FreeRTOS queue se statickým bufferem) — žádný heap.

---

## 9. Fázový plán implementace

Každá fáze má **gate**: bez splnění Definition of Done a bez review se nepokračuje.
Pořadí je záměrně „doménová logika dřív než hardware", ať je co testovat na hostu.

### Fáze 0 — Bootstrap a nástroje
- **Cíl:** prázdný, ale plně vybavený projekt.
- **Práce:** PlatformIO `platformio.ini` se dvěma env (`esp32dev`, `native`); struktura adresářů; `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore` (vč. `secrets.h`); `secrets.h.example`; doctest skeleton; GitHub Actions CI (build obou env + native testy + clang-tidy + coverage); skripty pro IWYU a coverage; `CLAUDE.md` převzato.
- **Agenti:** `build-ci-engineer`, `architect`.
- **Skilly:** `esp32-platformio-dual-env`, `static-analysis-coverage`, `host-unit-testing`.
- **DoD:** `pio run -e esp32dev` i `pio test -e native` projde (na prázdném skeletonu), CI zelená, žádné secrets v gitu.

### Fáze 1 — Architektura a HAL seam
- **Cíl:** kompletní rozhraní + host fakes, prázdné target stuby.
- **Práce:** ADR s modulovým rozpadem (§4), všechny HAL hlavičky, `*_fake.cpp` implementace, `Result<T>`, typy, `wiring` s GPIO mapou a konstantami.
- **Agenti:** `architect` (vlastní), `hal-engineer`, `requirements-analyst` (traceability matice FR/NFR → moduly).
- **Skilly:** `hal-link-time-seam`.
- **DoD:** native build linkuje proti fakes; review architektury OK; traceability matice existuje.

### Fáze 2 — Doménová logika (bez HW)
- **Cíl:** veškerá byznys logika hotová a otestovaná na hostu.
- **Práce:** `moving_average`, `history_buffer`, `alarm_state` (rozdíl/požár/porucha + hystereze + hrany), `anomaly`, `config_model`, `clock`, `event_log`. Plné unit testy (vč. hraničních a fault případů).
- **Agenti:** `firmware-engineer`, `test-engineer`, `code-reviewer`, `memory-performance-reviewer`.
- **Skilly:** `static-ring-buffer-history`, `host-unit-testing`, `cpp-review-checklist`.
- **DoD:** coverage doménové vrstvy ≥ 85 %, ASan/UBSan čisté, clang-tidy bez warningů, review OK.

### Fáze 3 — HAL target drivery
- **Cíl:** čtení čidel + LCD + PWM na reálné desce.
- **Práce:** `onewire_bus_target` (RMT, oba piny, CRC, detekce poruch), `display_target` (LiquidCrystal, 4-bit, stránkování), `pwm_target` (LEDC kontrast+bzučák, vzory). Smoke test na HW.
- **Agenti:** `hal-engineer`, `firmware-engineer`, `code-reviewer`, `safety-security-reviewer`.
- **Skilly:** `onewire-ds18b20-rmt`, `hd44780-lcd-4bit`, `pwm-contrast-buzzer`.
- **DoD:** na desce: měření obou čidel, LCD zobrazuje, kontrast a pípnutí fungují; utržení čidla detekováno; review OK.

### Fáze 4 — Konektivita (WiFi + HTTP + BLE)
- **Cíl:** připojení, web stránka s daty, BLE beacon.
- **Práce:** `wifi_target` (connect, backoff, reconnect, RSSI, IP, mDNS), `nvs_store_target`, `http_server_target` (**plain HTTP**, JSON API, statická stránka z PROGMEM, bez auth), `ble_advertiser_target` (NimBLE, formát §6.2, 5 burstů/min).
- **Agenti:** `firmware-engineer`, `hal-engineer`, `safety-security-reviewer`, `code-reviewer`.
- **Skilly:** `wifi-https-server-nvs`, `ble-advertising-nimble`.
- **DoD:** stránka dostupná přes HTTP (`teplomer.local`), zobrazuje data, config se ukládá do NVS a přežije restart; BLE pakety viditelné scannerem; reconnect funguje (test odpojením AP); review OK.

### Fáze 5 — UART log + e-mail
- **Cíl:** kompletní logování a notifikace.
- **Práce:** `event_log` napojen na UART (formát §FR-25), `mailer_target` (SMTP/TLS), implementace logiky §7, test e-mail z webu.
- **Agenti:** `firmware-engineer`, `test-engineer`, `safety-security-reviewer`, `code-reviewer`.
- **Skilly:** `uart-logging`, `esp-mail-smtp`.
- **DoD:** log čitelný a úplný; e-mail dorazí při simulovaném požáru a utržení; rate-limit a „perzistence neposílá" ověřeno (na hostu unit testem nad logikou §7, na HW manuálně); review OK.

### Fáze 6 — Safety hardening
- **Cíl:** WDT, brownout, jádra, OOM, fault injection.
- **Práce:** registrace WDT všech tasků; BOD konfigurace; pinning tasků na jádra; revize všech alokací; fault-injection testy (fakes simulují selhání NVS/WiFi/mail/sensor) na hostu.
- **Agenti:** `safety-security-reviewer` (vlastní), `memory-performance-reviewer`, `test-engineer`.
- **Skilly:** `wdt-brownout-mpu`.
- **DoD:** WDT resetuje uměle zaseknutý task; brownout dělá čistý reset; min-free-heap stabilní (žádný leak přes 24 h soak); fault-injection testy zelené.

### Fáze 7 — Web frontend + Python monitor
- **Cíl:** finální UI a nástroj pro BLE.
- **Práce:** `index.html`+`app.js` (uPlot s **missing points**, Pico.css, výpočet času v prohlížeči, ovládací prvky a formuláře, diagnostika); `tools/ble_monitor` (`bleak` CLI dekodér).
- **Agenti:** `web-frontend-engineer`, `python-tooling-engineer`, `code-reviewer`.
- **Skilly:** `wifi-https-server-nvs` (API kontrakt), `python-bleak-monitor`.
- **DoD:** graf vykresluje historii včetně mezer; ovládání a config z webu funguje; Python skript na Windows dekóduje pakety; review OK.

### Fáze 8 — Integrace, traceability, finální review, akceptace
- **Cíl:** hotový, ověřený produkt.
- **Práce:** end-to-end scénáře, doplnění traceability (FR/NFR → kód → test), kompletní review (kvalita + safety + security + memory), README a runbook, seznam rozšíření.
- **Agenti:** `integration-qa` (vlastní), všichni revieweři.
- **Skilly:** všechny review skilly.
- **DoD:** všechny FR/NFR pokryté a ověřené; CI zelená; všechny review gaty uzavřené; akceptační checklist (§11) splněn.

---

## 10. Akceptační kritéria — vybrané (každý FR má test)

- A1: Při `inner ≥ 45 °C` (okamžitě) zazní požární vzor, nastaví se `FIRE`, do 1 e-mailu/h dorazí zpráva. (FR-08, FR-26, §7)
- A2: Při `abs(avg_in − avg_out) ≥ 2 °C` jedno dvojité pípnutí; pod 1,5 °C se stav zruší. (FR-07)
- A3: Odpojení čidla → `SENSOR_OPEN` do ≤ 3 min, e-mail, vzor; po připojení návrat. (FR-04, FR-09)
- A4: Restart → historie smazaná (OK), config z NVS přežije. (FR-05, FR-27)
- A5: Odpojení AP → `WiFi DN`, opakované reconnect; po obnově web znovu dostupný bez restartu. (FR-15)
- A6: Web graf ukazuje mezeru tam, kde chybí/`INVALID` vzorek. (FR-18)
- A7: BLE paket dekódovaný Python skriptem nese obě teploty + správné příznaky. (FR-24, FR-28)
- A8: Uměle zaseknutý task → WDT reset do ≤ 8 s. (NFR-02)
- A9: 24 h soak → `min_free_heap` neklesá monotónně (žádný leak). (NFR-04)

## 11. Akceptační checklist (Fáze 8)
- [ ] Všechny FR-01…FR-28 implementované a otestované.
- [ ] Všechny NFR ověřené (WDT, brownout, žádný steady-state heap, no-except/no-rtti, dvě jádra, testovatelnost).
- [ ] clang-tidy/clang-format/IWYU čisté; ASan/UBSan čisté; coverage ≥ 85 % doménové vrstvy.
- [ ] Žádné secrets v repu; `secrets.h.example` aktuální.
- [ ] Traceability matice kompletní.
- [ ] README + runbook hotové.

---

## 12. Navrhovaná rozšíření (nice-to-have, mimo MVP)
- **OTA update** (ArduinoOTA / HTTPS OTA) — aktualizace bez USB.
- **NTP fallback** po připojení WiFi → přesný čas i bez RTC (degraduje na orientační při offline).
- **Perzistence historie do flash** (LittleFS, kruhově) — přežije restart (řekl jsi, že nemusí, proto jako opce).
- **Captive portal / WiFiManager** pro konfiguraci SSID bez rekompilace (kolize s „hardcoded" — proto opce).
- **Prometheus/`/metrics` endpoint** nebo MQTT publikace pro domácí monitoring.
- **Druhý práh „pre-alarm"** (varování dřív než požár).
- **Hardwarové ACK tlačítko** pro ztišení alarmu (přidalo by fyzický vstup).
- **Eddystone/iBeacon** varianta BLE pro kompatibilitu s generickými scannery.
- **Kalibrační offset** per čidlo v configu.
