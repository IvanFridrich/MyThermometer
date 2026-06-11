# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Globální pravidla projektu ESP32 Teplomer. Tato pravidla platí pro hlavní session
i pro všechny subagenty. Detailní požadavky jsou v `SPECIFICATION.md`. Procedurální
postupy jsou ve skillech (`.claude/skills/`).

---

## Architektura (přehled)

Třívrstvý design — každá vrstva smí záviset jen na vrstvách níže:

```
app/          – kompozice tasků, wiring, FreeRTOS tasky
core/         – čistá doménová logika (bez HW; plně testovatelná na hostu)
hal/          – rozhraní HW abstrakce; dvě implementace linkované staticky:
                  *_target.cpp  (ESP32-S3, Arduino/ESP-IDF)
                  *_fake.cpp    (host, pro unit testy)
```

**HAL link-time seam** (skill `hal-link-time-seam`): žádné virtuály, žádné `#ifdef`.
PlatformIO volí správnou implementaci přes `build_src_filter` per env:
- `esp32-s3` env: vše kromě `hal/*_fake.cpp` (přidá `-DESP32`)
- `native` env: jen `core/**` + `hal/*_fake.cpp` (kompiluje se s `-DNATIVE_BUILD`)

Pokud HAL main include potřebuje podmínit platformu (Arduino API vs. stdint), použij
`#ifdef NATIVE_BUILD` jako stráž. Doménový kód v `core/` tuto stráž nesmí obsahovat.

**Dual-core pinning** (NFR-07):
- Core 0 — WiFi stack, NimBLE stack, `web_task`, `ble_task`, `mail_task`
- Core 1 — `measurement_task`, alarmy, `lcd_task`, `history`, `supervisor`

**Jediný zdroj pravdy pro konstanty:** `src/Config.h` (namespace `cfg::`).
Žádné magické konstanty nikde jinde.

**Cílová struktura adresářů:**
```
include/            veřejné hlavičky modulů
src/
  core/             doménová logika (Result<T>, moving_average, history_buffer, …)
  hal/              HAL rozhraní + *_target.cpp + *_fake.cpp
  app/              tasky, wiring
  Config.h          SINGLE SOURCE OF TRUTH
test/               host unit testy (doctest)
web/                index.html + app.js (embedováno do PROGMEM)
tools/ble_monitor/  Python bleak skript
.claude/
  agents/           12 subagentů
  skills/           20 skillů
```

---

## Klíčová rozhodnutí (potvrzená — neměnit bez souhlasu)

| Oblast | Rozhodnutí |
|--------|-----------|
| LCD D7 | GPIO **21**; RS=42, EN=41, D4=5, D5=18, D6=**38**, D7=21 (GPIO 19 = USB D− na WROOM — zakázán) |
| HistoryRecord | **6 B/record** (bez `t_rel_s`); timestampy z pozice: `now − (count−1−i) × 600 s`; `__attribute__((packed))` |
| Diff alarm flags | Jeden bit **DIFF_EXCEEDED**; směr se nepersistuje jako flag — JSON API ho vrátí jako pole |
| BLE §6.2 | Beze změny; receiver si směr dopočítá z T_inner/T_outer |
| window_goal | Persistuje do NVS (`uint8`, 0=CoolRoom, 1=WarmRoom); konfigurovatelné z webu |
| HTTP server | Arduino **WebServer.h** (ne esp_http_server) |
| WDT pattern | Každý task si sám feeduje svůj WDT handle |
| Native env | Clang (LLVM 21.1.0, Windows) přes CC/CXX; ASan+UBSan; llvm-cov coverage |
| CI | GitHub Actions (public repo); 4 jobs: build-firmware, native-tests (clang++-18 + ASan/UBSan + llvm-cov), static-analysis (pre-commit + clang-tidy + cppcheck), sonar (podmíněný `SONAR_TOKEN`) |
| Pre-commit hooks | Ano: clang-format, clang-tidy na staged soubory, secret scan |
| ETL | PlatformIO registry (`ETLCPP/etl`), latest stable |
| Coverage | Lokální HTML (llvm-cov), bez externího uploadu |
| Python monitor | Python 3.14.5, `bleak`, Windows |

---

## Zásadní invarianty (nikdy neporušit bez explicitního souhlasu uživatele)

1. **Bez výjimek, bez RTTI.** Produkční kód se kompiluje s `-fno-exceptions -fno-rtti`. Chyby přes `Result<T>` / status enum, nikdy `throw`. (NFR-05)
2. **Minimální dynamická alokace.** V ustáleném stavu žádný `new`/`malloc`. Statické task stacky, statické buffery, ETL kontejnery. Každá nevyhnutelná alokace má ošetřené selhání. (NFR-04)
3. **Minimum polymorfismu.** Žádná runtime virtuální rozhraní v produkčním kódu. HW se abstrahuje **link-time seamem** (target vs fake `.cpp`), případně compile-time šablonami. (NFR-06, skill `hal-link-time-seam`)
4. **Všechen HW za HAL.** Doménová logika nezná Arduino/ESP-IDF API a musí jít přeložit a otestovat na hostu. (NFR-10)
5. **WDT a brownout jsou povinné** a nesmí být vypnuté „kvůli pohodlí". (NFR-02, NFR-03)
6. **Žádné secrets v gitu.** WiFi/SMTP/web creds jen v `secrets.h` (v `.gitignore`); v repu je `secrets.h.example`.
7. **Kontrakty z `SPECIFICATION.md` §6** (datové struktury, BLE formát, NVS klíče) se nemění bez review.

---

## Coding standard

- C++17, jazyk komentářů a identifikátorů: **anglicky**; dokumentace pro uživatele může být česky.
- Styl řídí `.clang-format` (LLVM-based). Formátování je závazné (CI ověřuje).
- `.clang-tidy` profil: `bugprone-*, cert-*, cppcoreguidelines-*, performance-*, readability-*, misc-*, modernize-*`. **Žádné nové warningy.**
- IWYU: každý soubor includuje právě to, co používá.
- Žádné magické konstanty — vše do `cfg::` v `Config.h`. Žádné blokující `delay()` v taskách bez WDT feedu.
- Preferuj `constexpr`, `enum class`, `etl::` kontejnery, `std::array`.
- Test framework: **doctest** (header-only, `-fno-exceptions` kompatibilní).
- Test soubory: `test/test_<module_name>/test_main.cpp`; každý soubor má právě jedno
  `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` a `#include <doctest/doctest.h>`.
  Přidání nového testu = nový podadresář + zápis do `platformio.ini` native include path.

---

## Build & test příkazy

```sh
# Firmware (ESP32-S3)
pio run -e esp32-s3-devkitc-1

# Upload + monitor
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor -b 115200

# Host unit testy (native) — bez ASan/UBSan (viz pozn. níže)
pio test -e native

# Host unit testy s ASan/UBSan + coverage report (otevře HTML v prohlížeči)
.\scripts\coverage.ps1

# Statická analýza (clang-tidy + cppcheck)
pio check

# Jeden konkrétní test soubor (native)
pio test -e native -f test_alarm_state

# Pre-commit hooks — jednorázová instalace (pak běží automaticky před každým commitem)
pip install pre-commit && pre-commit install

# Manuální spuštění na všech souborech
pre-commit run --all-files
```

> **Pozn. — ASan/UBSan:** `platformio.ini` native env záměrně neobsahuje
> `-fsanitize=address,undefined`. `pio test -e native` proto nepoběží se sanitizery.
> Sanitizery + coverage poběží přes `scripts/coverage.ps1` (Windows) nebo přímým
> voláním `clang++` (tak to dělá i CI). Skript `scripts/use_clang.py` je PlatformIO
> `extra_scripts` entry, který vynutí clang na Windows pro native env.

IWYU a coverage viz skill `static-analysis-coverage`.

---

## Workflow s agenty (viz README §Workflow)

- `architect` schvaluje rozhraní a modulový rozpad **před** implementací.
- Implementace dané fáze → `code-reviewer` + relevantní specializovaný reviewer (`safety-security-reviewer`, `memory-performance-reviewer`) → teprve pak další fáze.
- Reviewer agenti jsou **read-only** (Read/Grep/Glob/Bash) — neopravují, jen reportují nálezy s prioritou (Critical / Warning / Suggestion). Opravy dělá příslušný engineer agent.
- Nikdy nepřeskakuj gate fáze definovaný v `SPECIFICATION.md §9`.

---

## Definition of Done (každá změna)

- Kompiluje se v obou env relevantně; host testy zelené; clang-tidy/format/IWYU čisté; ASan/UBSan čisté (host); doménová coverage neklesla; traceability aktualizovaná.
