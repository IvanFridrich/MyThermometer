---
name: cpp-review-checklist
description: Embedded C++ review checklist (styl AUTOSAR/MISRA-C++ light) pro tento projekt — vynucuje bez výjimek/RTTI, žádnou dynamickou alokaci mimo rozpočet, compile-time před runtime polymorfismem, zákaz magických konstant (vše v Config.h), čisté includy (IWYU), pojmenování, jednotky a HAL hranice. Použij při KAŽDÉM code review a před uzavřením jakékoliv fáze; toto je tvrdý gate, na review uživatel klade důraz.
---
# C++ review checklist (embedded, MISRA-light)

Reviewuj proti tomuto; reportuj nálezy jako **Critical / Warning / Suggestion** s file:line a opravou
(read-only — opravuje engineer agent).

## Jazyk & paměť
- [ ] Žádný `throw` / `try` / `catch` / `dynamic_cast`; buildí s `-fno-exceptions -fno-rtti`.
- [ ] Žádná dynamická alokace mimo zdokumentovaný rozpočet; **žádná** v ustálené smyčce ani ISR/RMT callbacku.
- [ ] Každá nevyhnutelná alokace má ošetřené selhání (návratový kód + log).
- [ ] Statické task stacky, statické buffery, ETL/`std::array`; žádné skryté STL alokace.

## Polymorfismus & moduly
- [ ] Žádný nový `virtual` bez zdůvodnění; preferuj link-time seam / compile-time policy.
- [ ] HW jen za HAL; doména přeložitelná na hostu (žádné přímé Arduino/ESP-IDF volání).
- [ ] Jednosměrné závislosti; soběstačné hlavičky (`#pragma once`); žádný „god object".

## Konstanty, jednotky, korektnost
- [ ] **Žádné magické konstanty** — vše konfigurovatelné je `cfg::*` v `Config.h`.
- [ ] Jednotky konzistentní (úložiště centi-°C, logika °C); ošetřené **záporné** teploty.
- [ ] Hystereze tam, kde práh kmitá; hrany detekované korektně; chybové cesty jako hodnoty, ne pády.
- [ ] Chování odpovídá konkrétnímu FR; žádný blokující `delay()` v rámci WDT okna.

## Kvalita & testy
- [ ] IWYU-čisté; clang-tidy/clang-format/cppcheck bez nových warningů (potlačení jen se zdůvodněním).
- [ ] Nová logika má host testy, které **tvrdí chování**; doménová coverage neklesla.
- [ ] Nic se neumlčuje, aby „prošel gate".
