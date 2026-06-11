---
name: esp32-platformio-dual-env
description: Jak strukturovat PlatformIO projekt tohoto firmwaru — dvě prostředí (esp32-s3 target + native host testy), Arduino framework, build flagy (-fno-exceptions -fno-rtti -Os, sanitizery + coverage na native), partition layout pro modul bez PSRAM a secrets.h vzor. Použij při zakládání nebo změně buildu, layoutu zdrojů či prostředí.
---
# PlatformIO projekt ESP32-S3 (dual-env)

## Layout (návrh)
```
platformio.ini
include/Config.h            # jediný zdroj pravdy (cfg::*)
include/secrets.h           # git-ignored
include/secrets.h.example
src/hal/*.h  *_target.cpp  *_fake.cpp
src/core/*.h *.cpp          # doménová logika (bez HW)
src/app/*.cpp               # tasky + wiring (kompozice)
test/native/*               # doctest
web/                        # index.html + app.js (embed do PROGMEM)
tools/ble_monitor/          # Python bleak
```

## Prostředí
- `[env:esp32-s3]`: `platform = espressif32`, `board = <tvá S3 deska>`, `framework = arduino`.
  Flagy: `-fno-exceptions -fno-rtti -Os -Wall -Wextra -Wpedantic`. Buildí jen `*_target.cpp`.
  Bez PSRAM: partition table dle flashe; PSRAM nezapínat.
- `[env:native]`: `platform = native`. Buildí jen `*_fake.cpp`. Přidej
  `-fsanitize=address,undefined` + coverage instrumentaci. `test_framework` doctest (nebo unity/gtest).

## Výběr zdrojů
Použij `build_src_filter` (nebo oddělené `src_dir`/`lib`), ať každé env kompiluje jen svou HAL
implementaci. Doména + `Config.h` se kompilují v obou.

## secrets.h vzor
`secrets.h.example` dokumentuje povinné symboly (WiFi SSID/heslo, SMTP host/user/app-password,
příjemci). Reálný `secrets.h` je v `.gitignore`. Nikdy ho neloguj.

## Pozor
- Bez PSRAM: hlídej velké statické pole + heap WiFi/BLE; drž rozpočet.
- Native build drž bez Arduino hlaviček (HAL fakes jsou čisté C++).
