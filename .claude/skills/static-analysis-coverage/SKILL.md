---
name: static-analysis-coverage
description: Jak nastavit a spustit free quality nástroje tohoto projektu na Windows — clang-tidy, clang-format, cppcheck, include-what-you-use, AddressSanitizer/UBSan, code coverage (preferován LLVM llvm-profdata/llvm-cov; MSYS2 gcov jako alternativa) a SonarQube/SonarCloud. Pokrývá pre-commit hooky a CI bránu. Použij ve Fázi 0 a kdykoliv řešíš quality nástroje, coverage nebo CI gaty.
---
# Statická analýza + sanitizery + coverage (free)

## Jeden toolchain (doporučeno)
Použij **LLVM/Clang** na hostu, ať jedna sada pokryje clang-tidy, clang-format, ASan/UBSan i coverage.
MSYS2 (`gcc`+`gcov`+`gcovr`/`lcov`) funguje, ale **netahej dva toolchainy** zbytečně.

## Nástroje
- **clang-format** (`.clang-format`, LLVM base) — formátování závazné, CI ověřuje.
- **clang-tidy** (`.clang-tidy`): `bugprone-*, cert-*, cppcoreguidelines-*` (bez exception/RTTI checků,
  protože `-fno-exceptions -fno-rtti`), `performance-*, readability-*, modernize-*, misc-*`. **Žádné nové
  warningy**; potlačení jen s písemným zdůvodněním.
- **cppcheck** jako druhý názor.
- **IWYU** — každý soubor includuje právě to, co používá; hlavičky soběstačné.
- **ASan/UBSan** na native testech (`-fsanitize=address,undefined`).
- **Coverage:** instrumentuj native build, sbírej `llvm-profdata merge` → `llvm-cov report/show`.
  Cíl ≥ 85 % řádků **doménové vrstvy**.
- **SonarQube:** SonarQube Community (self-host) nebo SonarQube Cloud (zdarma pro veřejné repo).
  **Ověř aktuální podmínky/limity free verze při setupu** (mění se) a reportuj zpět.

## Automatizace
- `pre-commit`: clang-format + clang-tidy (+ rychlý cppcheck) na změněných souborech.
- CI: build obou env → native testy pod ASan/UBSan → coverage gate → Sonar. Build padá při porušení gate.

## Pozor
Některé clang-tidy/cppcoreguidelines checky předpokládají výjimky/STL kontejnery — vyřaď ty, co kolidují
se stylem (no-except, statická paměť, ETL), aby gate nebyla šum.
