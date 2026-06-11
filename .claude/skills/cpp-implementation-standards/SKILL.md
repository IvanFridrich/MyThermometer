---
name: cpp-implementation-standards
description: Implementační standardy embedded C++ pro tento projekt — jak psát produkční kód bez výjimek/RTTI, vzor Result<T> a status enumů, statická paměť a ETL kontejnery, jednotky (centi-°C), strukturu modulu (hlavička + .cpp, HAL hranice), konvence pojmenování a použití Config.h. Použij PŘED psaním jakéhokoliv firmware/doménového kódu a kdykoliv řešíš „jak to napsat správně".
---
# Implementační standardy (embedded C++17)

## Chyby bez výjimek
- `-fno-exceptions -fno-rtti`. Chyby vracej jako hodnoty: `Result<T>` (hodnota | status enum) nebo
  prosté `Status`. Žádný `throw`. Volající **musí** status zpracovat (žádné tiché ignorování).
```cpp
enum class Status : uint8_t { Ok, CrcError, NoDevice, OutOfRange, Timeout, NvsError };
template <class T> struct Result { Status status; T value; bool ok() const { return status==Status::Ok; } };
```

## Paměť
- Static-first: statické pole, `std::array`, **ETL** kontejnery (`etl::vector`, `etl::queue`...). Žádný
  `new`/`malloc` v ustáleném stavu; žádná alokace v ISR/RMT/timer callbacku. Nevyhnutelná alokace má
  ošetřené selhání. Velikosti z `Config.h`.

## Jednotky & typy
- Ukládej teploty jako `int16` **centi-°C**; °C jen na hranici UI. `INT16_MIN` = neplatné. `enum class`
  pro stavy/příznaky. `constexpr`/`cfg::*` místo literálů (**žádné magické konstanty**).

## Struktura modulu
- Veřejná hlavička v `include/` (`#pragma once`, soběstačná, IWYU-čistá) + `.cpp` v `src/`. HW jen přes
  HAL; doména přeložitelná na hostu. Malé funkce, malé moduly, jednosměrné závislosti, žádný „god object".

## Tasky & čas
- Žádný blokující `delay()` v rámci WDT okna; dlouhé čekání děl na non-blocking stavový automat tikaný
  proti HAL hodinám. Sdílení mezi vlákny přes statické FreeRTOS fronty/štíty, ne syrové globály.

## Hotovo, když
Kompiluje se v obou env relevantně, žádné nové warningy, pokryto host testy, čte konstanty z `Config.h`.
