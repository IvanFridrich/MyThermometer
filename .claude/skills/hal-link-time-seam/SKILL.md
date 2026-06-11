---
name: hal-link-time-seam
description: Architektonický vzor pro abstrakci hardwaru tohoto projektu BEZ virtuálů — jedno HAL rozhraní v hlavičce, dvě implementace vybírané při linkování (cíl *_target.cpp vs host *_fake.cpp), plus alternativa compile-time policy/šablon. Použij při návrhu jakéhokoliv HAL rozhraní, řešení testovatelnosti, nebo rozhodování compile-time vs runtime polymorfismus.
---
# HAL link-time seam (bez virtuálů)

## Proč
Požadavek: „minimum polymorfismu, vše compile-time" + „všechen HW za HAL, testovatelné na hostu".
Klasická abstrakce přes virtuální rozhraní = runtime polymorfismus (zakázán). Řešení: **link-time seam**.

## Vzor (default)
- Každé HAL je **konkrétní třída** deklarovaná v hlavičce s pevným rozhraním (žádné `virtual`).
- Dvě definice: `<unit>_target.cpp` (jen v env `esp32-s3`) a `<unit>_fake.cpp` (jen v env `native`).
  Build vybere jednu → plná testovatelnost na hostu, **nula vtable**.
- Doménový kód includuje jen hlavičku.
- Fake je **programovatelný**: settery pro vstřik hodnot/poruch/času, gettery pro zachycení výstupu.

```cpp
// hal/onewire_bus.h
class OneWireBus {
 public:
  explicit OneWireBus(uint8_t pin);
  Result<int16_t> readCentiC();   // implementace v target/fake .cpp
};
```

## Alternativa (dokumentuj, nepoužívej jako default)
Compile-time **policy / šablony**: modul bere typ HAL jako template parametr. Použij jen, pokud modul
opravdu potřebuje být instancovaný proti více HAL v jednom binárce. Jinak link-time seam (jednodušší linker, čitelnější).

## Checklist
- [ ] Žádný `virtual` v HAL ani doméně.
- [ ] Každé HAL má `*_target.cpp` i `*_fake.cpp`.
- [ ] Doména nezná Arduino/ESP-IDF (jde přeložit na hostu).
- [ ] Fake umí vstřiknout selhání i posunout čas.
