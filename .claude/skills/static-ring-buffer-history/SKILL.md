---
name: static-ring-buffer-history
description: Jak implementovat statický kruhový buffer historie tohoto teploměru — pevné pole 144 záznamů (24 h × 1/10 min) bez alokace, ukládání 15min průměru každých 10 min, OR příznaků událostí v okně, wrap-around a dump pro web. Použij pro modul historie a kdykoliv řešíš ukládání časové řady v RAM bez heapu.
---
# Statický ring buffer historie

## Kontrakt (SPECIFICATION.md §6.1)
```c
struct __attribute__((packed)) HistoryRecord {   // 6 B; 144× = 864 B static
    int16_t  t_inner_c100;    // centi-°C; INT16_MIN = neplatné
    int16_t  t_outer_c100;    // centi-°C; INT16_MIN = neplatné
    uint16_t flags;           // OR příznaků v 10min okně
};
```
Timestamp se nepersistuje — web dopočítá z pozice: `now − (count−1−i) × 10 min`.
API vrátí `uptime_s` a `count` pro ukotvení osy. Hloubka `cfg::sample::kHistoryDepth`
(=144 @ 24h). **Statické pole**, žádný heap.

## Logika
- Vzorkuj 1×/min; drž 10min plovoucí průměr (10 vzorků) per čidlo (skill `core/moving_average`).
- Každých `kHistoryStrideMs` (10 min) zapiš **aktuální 10min průměr** + OR všech příznaků nasbíraných
  v tom okně. Okno průměru (10 min) = krok historie (10 min) — záměrné zarovnání, každý záznam
  je průměr přesně posledních 10 měření.
- Ring: index `head`, `count`; při zaplnění přepiš nejstarší. Dump pro web vrací v pořadí od
  nejstaršího, prohlížeč dopočítá čas z indexu × 10 min.
- Po výpadku napájení se buffer ztratí — dle spec OK (RAM-only).

## Pozor / testy
- Hraniční stavy: prázdný buffer, částečně zaplněný (warm-up), přesný wrap.
- `INT16_MIN` = neplatný vzorek → web vykreslí mezeru (null), nikoliv interpolaci.
- Žádná alokace; velikost ověří `memory-performance-reviewer`.
