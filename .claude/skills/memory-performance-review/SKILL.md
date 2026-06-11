---
name: memory-performance-review
description: Review checklist paměti a výkonu pro tento modul ESP32-S3 BEZ PSRAM — tabulka statických bufferů, strop heapu a min-free-heap, dimenzování task stacků proti high-water markům, balanc jader, fragmentace a správné použití RMT/LEDC offloadu (a potvrzení, že chybějící DMA je správně, ne opomenutí). Použij při review Fáze 2 a 6 a jakékoliv změny přidávající buffery nebo tasky.
---
# Memory / performance review checklist

Read-only; nálezy dle priority. Páruje se s agentem `memory-performance-reviewer`. Cíl: zůstat
v rozpočtu na S3 **bez PSRAM**.

## Statická paměť
- [ ] Tabulka bufferů (komponenta → bajty): ring historie (144×6 = 864 B), průměrovací okna, JSON
      scratch, BLE payload, log buffery — vše z `Config.h`/§6.
- [ ] Žádná alokace v ustáleném stavu (cíl 0); žádná v ISR/RMT/timer callbacku.

## Heap
- [ ] Strop heapu potvrzen; rezerva **min-free-heap** pod souběžnou zátěží (web + BLE + měření) dostatečná.
- [ ] Min-free-heap monitorován a vystaven na web; žádný monotónní pokles přes 24h soak (žádný leak).

## Stacky & jádra
- [ ] Velikosti tasků (`cfg::task::kStack*`) ověřené proti high-water markům — ne malé, ne plýtravé.
- [ ] Core 0 = WiFi+BLE+web+mail, Core 1 = měření/alarmy/LCD/bzučák/historie; kadence 1/min nehladoví;
      ISR/RMT callbacky malé.

## Offload / DMA
- [ ] OneWire na RMT, tóny/kontrast na LEDC.
- [ ] Potvrzeno, že **není reálný DMA sink** (žádné SPI/I2S/ADC-continuous) a DMA se nevynucovalo tam,
      kam nepatří (D24). Rationale zdokumentován.

## Fragmentace
- [ ] Jakékoliv nevyhnutelné dynamické použití je krátkožijící a ohraničené.
