---
name: pwm-contrast-buzzer
description: Jak použít LEDC ESP32-S3 na dvě úlohy — generování více frekvencí na PASIVNÍM bzučáku (distinktivní tón per událost + opakovaný interval kvarty pro požár) a řízení kontrastu LCD jako PWM signál přes R+C dolní propust do V0. Pokrývá non-blocking pattern engine. Použij pro Tone/PWM HAL, modul bzučáku a kontrast controller.
---
# LEDC tóny (pasivní bzučák) + PWM kontrast

## Kontrast
- LEDC kanál s pevnou frekvencí, vysoké rozlišení (`cfg::ledc::kContrastFreqHz`, `kContrastResBits`=8)
  na `cfg::pin::kLcdContrastV0`. R+C dolní propust udělá z PWM hladké DC na V0. Hodnota persistovaná
  v NVS (uint8 0..255), default `cfg::ledc::kContrastDefault`. RC zvol tak, aby zvlnění bylo pod
  jeden krok kontrastu na PWM frekvenci.

## Pasivní bzučák
- Pasivní bzučák potřebuje střídavý buzení — generuj obdélník na cílové frekvenci přes LEDC na
  `cfg::pin::kBuzzer` (kanál `kBuzzerChannel`). Změna výšky = změna LEDC frekvence; střída ~50 %.
  Ticho = střída 0.
- Distinktivní tóny událostí z `cfg::beep::*` (rozdíl, čidlo, divná hodnota, boot, test).
- **Požární vzor:** střídej `kFireToneLowHz` a `kFireToneHighHz` (interval kvarty, poměr 4:3) každých
  `kFireToneStepMs`, opakuj po `kFireBurstMs` (~1 min).
- Navrhni pro **více tónů do budoucna** — malý data-driven formát vzoru (sekvence {freq, ms}) dělá nové melodie triviální.

## Non-blocking engine
- Pattern engine je stavový automat tikaný z bzučákového tasku proti HAL hodinám — **nikdy** `delay()`.
  Jednorázová pípnutí i požární vzor jsou jen různé sekvence. Globální on/off z configu.

## Host fake
Zaznamenej sekvenci (frekvence, trvání), kterou engine vydá, ať testy ověří tóny událostí i časování požárního vzoru proti fake hodinám.
