---
name: wdt-brownout-mpu
description: Jak zhardenovat tento síťově napájený teploměr bez zálohy — Task WDT s registrací všech tasků (timeout 8 s, panic reset), hardwarový brownout detektor s konzervativním prahem, základní MPU/PMP ochrany (stack-protector, nespustitelný stack), pinning tasků na jádra a fault injection. Použij ve Fázi 6 safety hardening a kdykoliv řešíš robustnost/recovery.
---
# WDT + brownout + základní MPU (safety hardening)

## Watchdog (NFR-02, §8.1)
- `esp_task_wdt`, timeout `cfg::safety::kWdtTimeoutMs` (8 s), `panic = true` (reset).
- **Každý** aplikační task se registruje a periodicky krmí WDT (přes `supervisor`/`hal/system`).
- Žádné blokující volání bez feedu uvnitř WDT okna. Umělé zaseknutí tasku musí vést k resetu (ověř na HW).

## Brownout (NFR-03, §8.2)
- HW BOD zapnutý, práh přes `sdkconfig` (`CONFIG_ESP_BROWNOUT_DET_LVL`, ~2,9–3,0 V; viz `cfg::safety::kBrownoutLevel`).
- Po brownoutu čistý reset; po startu nastav `BROWNOUT_RECOVER`, pokud reset reason odpovídá.
- Stav se po resetu bezpečně reinicializuje; ztráta RAM historie je dle spec OK.

## MPU / PMP (základní, NFR-09/§8.3)
- ESP32-S3 nemá plnohodnotné MPU; reálně: `-fstack-protector-strong`, nespustitelný stack, ponechané
  default region permissions. **Žádná ambice na víc** — v dokumentaci jasně uveď limity.

## Jádra & OOM
- Tasky `xTaskCreateStaticPinnedToCore` (statické stacky): WiFi+BLE+web+mail Core 0; měření/alarmy/LCD/historie Core 1.
- OOM degraduje (zahoď web request, měř dál); žádný steady-state heap; min-free-heap sleduj a surfacuj.

## Fault injection (na hostu přes fakes)
Simuluj selhání NVS/WiFi/mail/čidla a ověř, že nic neuvázne a alarmy raději signalizují.
