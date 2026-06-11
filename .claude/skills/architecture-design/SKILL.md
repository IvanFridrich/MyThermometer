---
name: architecture-design
description: Architektonické principy a postup pro tento firmware — vrstvení doména/HAL/aplikace, link-time seam místo virtuálů, rozpad na mnoho malých modulů, paměťový a taskový rozpočet, pinning na dvě jádra (WiFi+BLE vs zbytek) a kdy psát ADR a konzultovat HW s ownerem. Použij na začátku každé fáze, při definici rozhraní modulu nebo průřezovém rozhodnutí (paměť, jádra, závislosti).
---
# Architektonický návrh

## Vrstvy (SPECIFICATION.md §4)
1. **Doména** — čistá logika (měření, průměr, historie, alarmy, config model, doporučení okna,
   event log). Bez HW; 100 % testovatelná na hostu.
2. **HAL** — konkrétní třídy v hlavičkách; `*_target.cpp` (ESP32-S3) vs `*_fake.cpp` (host), výběr při
   linkování (**bez virtuálů**). Viz skill `hal-link-time-seam`.
3. **Aplikace** — tasky + `wiring` (kompozice závislostí, GPIO mapa, konstanty).

## Principy
- **Mnoho malých modulů** s úzkým rozhraním (owner to chce). Jednosměrné závislosti. Žádný „god object".
- **Compile-time před runtime**: žádný runtime polymorfismus, kde stačí link-time/šablony.
- **Static-first paměť** (bez PSRAM!): vyjmenuj každý statický buffer s velikostí; strop heapu; ustálený
  stav = 0 alokací. Konzultuj s `memory-performance-reviewer`.
- **Dvě jádra**: Core 0 = WiFi + BLE + web + mail; Core 1 = měření + alarmy + LCD + bzučák + historie +
  supervisor. Statické stacky, mezivláknové fronty se statickým bufferem.

## Postup
- Na začátku fáze napiš/aktualizuj **ADR** (rozhodnutí + důvod + dopad). Schvaluj rozhraní **před**
  implementací. HW (piny, RC filtr, pull-upy, HAL seam, jádra) označ `🔶` a nech ownera potvrdit.
- Datové kontrakty (§6) jsou stabilní API mezi firmwarem, webem a Python nástrojem — měň jen vědomě s verzí.

## Výstup
ADR, HAL hlavičky, mapa závislostí modulů, paměťový/taskový rozpočet — vše schválené ownerem.
