# ESP32 Teplomer — Dvoukanálový teploměr s WiFi/BLE/e-mailem

Embedded firmware pro ESP32-S3. Měří teplotu na dvou DS18B20 čidlech,
zobrazuje ji na 2×8 LCD, hlídá požár a klimatický rozdíl, posílá e-mail při
alarmu, vysílá BLE beacon a poskytuje HTTP webové rozhraní s historickým grafem.

---

## Obsah

1. [Hardware — zapojení](#1-hardware--zapojení)
2. [První spuštění — nastavení secrets a build](#2-první-spuštění--nastavení-secrets-a-build)
3. [Upload firmware a UART monitor](#3-upload-firmware-a-uart-monitor)
4. [Webové rozhraní](#4-webové-rozhraní)
5. [BLE monitor (Python, Windows)](#5-ble-monitor-python-windows)
6. [UART log](#6-uart-log)
7. [Konfigurace a alarmy](#7-konfigurace-a-alarmy)
8. [Vývojové prostředí a workflow](#8-vývojové-prostředí-a-workflow)

---

## 1. Hardware — zapojení

### Komponenty

| Komponenta | Poznámka |
|---|---|
| ESP32-S3 DevKitC-1 (nebo kompatibilní) | bez PSRAM |
| 2× DS18B20 teploměr | každé na vlastním GPIO |
| 2× pull-up 4,7 kΩ (3,3 V → data) | jeden na každou OneWire sběrnici |
| HD44780 kompatibilní LCD 2×8 znaků | 4-bit mód |
| Pasivní bzučák + NPN tranzistor (BC547 apod.) | báze přes ~1 kΩ |
| RC filtr pro LCD kontrast | ~1 kΩ + 10 µF na GPIO 13 → V0 |

### Schéma zapojení

```
ESP32-S3 DevKitC-1          DS18B20 (inner — fire sensor)
   GPIO 15 ────────────────── DATA ─── 4,7 kΩ ─── 3V3
                                        VDD ─────── 3V3
                                        GND ─────── GND

                             DS18B20 (outer — ambient sensor)
   GPIO 14 ────────────────── DATA ─── 4,7 kΩ ─── 3V3
                                        VDD ─────── 3V3
                                        GND ─────── GND

                             HD44780 LCD (4-bit)
   GPIO 42 ────────────────── RS
   GPIO 41 ────────────────── E
   GPIO  5 ────────────────── D4
   GPIO 18 ────────────────── D5
   GPIO 38 ────────────────── D6   (GPIO 19 = USB D- na WROOM — zakázán)
   GPIO 21 ────────────────── D7
   GND     ────────────────── RW   (jen zápis)
   3V3     ────────────────── VDD, A (backlight+)
   GND     ────────────────── VSS, K (backlight-)

                             LCD kontrast (RC low-pass → V0)
   GPIO 13 ── 1 kΩ ── V0 ─── 10 µF ─── GND   (LEDC PWM, 25 kHz, 8-bit)

                             Pasivní bzučák (přes NPN)
   GPIO  9 ── 1 kΩ ── báze NPN
              kolektor ──── bzučák ──── 3V3
              emitor   ──── GND
```

> Piny jsou definovány v [src/Config.h](src/Config.h) (namespace `cfg::pin`).
> Pokud měníš zapojení, uprav jen tamní konstanty.

---

## 2. První spuštění — nastavení secrets a build

### Krok 1 — Vytvořit `include/secrets.h`

Soubor `include/secrets.h` je v `.gitignore` a nikdy se nesmí commitovat.

```sh
cp include/secrets.h.example include/secrets.h
```

Vyplň reálné hodnoty:

```cpp
#define WIFI_SSID     "NazevSite"
#define WIFI_PASSWORD "HesloSite"
#define SMTP_HOST     "smtp.gmail.com"
#define SMTP_USER     "tvuj@gmail.com"
#define SMTP_PASS     "aplikacni-heslo-gmail"  // App Password, ne přihlašovací heslo!
#define RECIPIENT_EMAIL "prijemdce@example.com"
```

> **Gmail App Password:** Správa účtu Google → Bezpečnost → Dvoufázové ověření → Hesla aplikací.

### Krok 2 — Nainstalovat PlatformIO

```sh
# VS Code rozšíření "PlatformIO IDE" — stáhne toolchain automaticky
# nebo: pip install platformio
```

### Krok 3 — Sestavit firmware

```sh
pio run -e esp32-s3-devkitc-1
```

Výstup: `RAM: 20.x%  Flash: ~39%` — firmware se vejde i s rezervou.

---

## 3. Upload firmware a UART monitor

```sh
# Nahrát firmware
pio run -e esp32-s3-devkitc-1 -t upload

# Sledovat sériový výstup (115200 Bd)
pio device monitor -b 115200
```

Po bootu UART výpis vypadá přibližně takto:

```
[+   0][I][BOOT] thermometer starting
[+  12][I][NVS ] config loaded from NVS
[+  14][I][PWM ] contrast ch=4 duty=128
[+  16][I][LCD ] init ok
[+  18][I][WIFI] connecting to MySSID...
[+3422][I][WIFI] connected, IP=192.168.1.105, RSSI=-58
[+3423][I][WEB ] HTTP server started on port 80
[+3424][I][BLE ] NimBLE advertiser started
[+ 860][I][MEAS] inner=23.45 C  outer=21.00 C
```

---

## 4. Webové rozhraní

Po připojení k WiFi je zařízení dostupné na:

- **`http://teplomer.local`** — mDNS hostname (funguje bez znalosti IP)
- **`http://<IP>`** — IP adresa zobrazená na LCD a v UART logu po připojení

### Co stránka ukazuje

| Sekce | Obsah |
|---|---|
| Teploty | Vnitřní, venkovní, rozdíl; doporučení okna (OTEVŘÍT / ZAVŘÍT / BEZE ZMĚNY) |
| Alarmy | Požár, porucha čidla, překročení rozdílu |
| Graf | 24 h historie vzorků (1/10 min), přerušení čáry = chybějící vzorek |
| Diagnostika | Uptime, RSSI, free heap, min-free-heap, ROM ID čidel |
| Konfigurace | Práh požáru, práh rozdílu (+ hystereze), kontrast LCD, bzučák on/off, e-mail on/off |
| Akce | Restart, test bzučáku, nastavit kontrast živě, poslat testovací e-mail, vyžádat status e-mail |

### API endpointy (pro vlastní integraci)

```
GET  /api/current   → JSON aktuální stav + konfigurace
GET  /api/history   → JSON kruhový buffer (144 vzorků, stride_s=600)
POST /api/config    → uložit konfiguraci do NVS
POST /api/action/<akce>  → restart | test-beep | set-contrast | test-email | status-email
```

---

## 5. BLE monitor (Python, Windows)

Skript v [tools/ble_monitor/](tools/ble_monitor/) skenuje BLE advertising pakety
a dekóduje payload (obě teploty + příznaky) shodný s firmwarem (§6.2).

### Instalace

```sh
cd tools/ble_monitor
pip install -r requirements.txt   # nebo: pip install bleak
```

### Spuštění

```sh
python monitor.py                  # tisk na konzoli
python monitor.py --log data.csv   # + append do CSV souboru
python monitor.py --selftest       # ověření dekodéru bez rádia
```

### Výstup

```
Scanning for company ID 0xFFFF ... Ctrl-C to stop
[2026-06-21 10:04:11] inner=23.45°C outer=21.00°C flags=INNER_VALID,OUTER_VALID seq=42 rssi=-67dBm
[2026-06-21 10:04:11] inner=23.45°C outer=21.00°C flags=INNER_VALID,OUTER_VALID seq=43 rssi=-65dBm
```

Zařízení vysílá 5 burstů každou minutu. Každý burst = 5× stejný paket á 100 ms.

---

## 6. UART log

Format: `[+uptime_ms][LEVEL][MODULE] zpráva`

| Level | Kdy |
|---|---|
| `T` TRACE | podrobné ladění |
| `D` DEBUG | normální tok |
| `I` INFO  | klíčové události (připojení, teplota, alarm) |
| `W` WARN  | nestandardní stav (bez hardware chyby) |
| `E` ERROR | chyby (selhání e-mailu, NVS, OneWire) |

Minimální logovaný level je nastaven v [src/Config.h](src/Config.h) `cfg::log::kMinLevel` (default: DEBUG).

---

## 7. Konfigurace a alarmy

Všechna nastavení se ukládají do NVS flash a přežijí restart. Defaultní hodnoty:

| Parametr | Default | Poznámka |
|---|---|---|
| Práh požáru | 45,0 °C | okamžitá hodnota inner |
| Hystereze požáru | 2,0 °C | odezní při ≤ 43 °C |
| Práh rozdílu | 2,0 °C | 10min průměr \|out−in\| |
| Hystereze rozdílu | 0,5 °C | odezní při ≤ 1,5 °C |
| Cíl větrání | CoolRoom | OPEN = venku chladněji |
| Kontrast LCD | 128/255 | |
| Bzučák | zapnutý | |
| E-mail | zapnutý | |

### E-mailová logika

- Automatický e-mail se pošle při **náběžné hraně** požáru nebo poruchy čidla.
- Max 1 automatický e-mail / 1 h / typ alarmu.
- Perzistence alarmu sama o sobě žádný e-mail neposílá.
- Manuální „test e-mail" / „status e-mail" z webu obchází rate-limit.

### LCD displej

- **Řádek 1:** vnitřní teplota (`I: 23.4°C`)
- **Řádek 2:** venkovní teplota (`O: 21.0°C`) — přepíše se alarmem (`FIRE!`, `SENSOR`, `WiFi DN`)

---

## 8. Vývojové prostředí a workflow

### Co nainstalovat (Windows)

- **PlatformIO IDE** (VS Code rozšíření) — spravuje ESP32 toolchain automaticky
- **LLVM for Windows** — pro host testy, statickou analýzu a coverage:
  ```sh
  winget install LLVM.LLVM
  ```
- **Python 3.11+** — pro PlatformIO a BLE monitor
- (volitelně) **cppcheck** — `winget install Cppcheck.Cppcheck`

### Build příkazy

```sh
# Firmware (ESP32-S3)
pio run -e esp32-s3-devkitc-1

# Upload + monitor
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor -b 115200

# Host unit testy (native, bez ASan)
pio test -e native

# Host unit testy s ASan/UBSan + HTML coverage report
.\scripts\coverage.ps1

# Statická analýza (clang-tidy + cppcheck)
pio check

# Jeden konkrétní test soubor
pio test -e native -f test_alarm_state
```

### Struktura repozitáře

```
.
├── CLAUDE.md            pravidla pro AI agenty
├── SPECIFICATION.md     požadavky, architektura, fázový plán
├── platformio.ini       build konfigurace (esp32-s3 + native)
├── include/             veřejné hlavičky modulů
│   └── secrets.h.example  šablona pro WiFi/SMTP přihlašovací údaje
├── src/
│   ├── Config.h         JEDINÝ ZDROJ PRAVDY pro konstanty (cfg::*)
│   ├── core/            doménová logika (bez HW; testovatelná na hostu)
│   ├── hal/             HAL rozhraní + *_target.cpp + *_fake.cpp
│   └── main.cpp         kompozice tasků, wiring, FreeRTOS
├── test/                host unit testy (doctest, 13 sad)
├── web/                 index.html + app.js (embedováno do PROGMEM)
├── tools/ble_monitor/   Python bleak skript (FR-28)
├── docs/
│   ├── traceability.md  FR/NFR → modul → test
│   └── adr-phase*.md   architektonická rozhodnutí
└── scripts/
    └── coverage.ps1     ASan/UBSan + llvm-cov HTML report
```

### Architektura (třívrstvý design)

```
app/    — kompozice tasků, FreeRTOS (smí záviset na core/ a hal/)
core/   — čistá doménová logika (bez HW; plně testovatelná na hostu)
hal/    — HAL rozhraní; dvě implementace linkované staticky:
              *_target.cpp  (ESP32-S3, Arduino/ESP-IDF)
              *_fake.cpp    (host, pro unit testy)
```

Žádné virtuální funkce, žádné `#ifdef` v doménovém kódu.
PlatformIO volí správnou implementaci přes `build_src_filter`.

### Workflow s AI agenty

```
architect navrhne rozhraní
  → engineer agenti implementují (s pomocí skillů v .claude/skills/)
  → reviewer agenti auditují (read-only, hlásí nálezy)
  → engineer opraví → gate fáze se uzavře
```

Viz `SPECIFICATION.md §9` a `.claude/agents/` pro 12 specializovaných agentů.

### Bezpečnostní poznámka

WiFi heslo a SMTP údaje jsou v `include/secrets.h` (v `.gitignore`).
**Nikdy secrets.h necommituj.** V repu je jen `secrets.h.example`.
Web rozhraní je **bez autentizace** (LAN-trusted — akceptované riziko per FR-22).
