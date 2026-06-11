---
name: esp-mail-smtp
description: Jak posílat alarmové e-maily z ESP32-S3 přes ESP-Mail-Client přes SSL s Gmail app password drženým v secrets.h, a jak implementovat POLITIKU alarmových e-mailů — nejvýše jeden automatický e-mail za hodinu na typ, neposílat při trvání alarmu, stejné zacházení pro požár i utržené čidlo, s manuálním test/status e-mailem obcházejícím limiter. Použij pro SMTP HAL a modul e-mailových alarmů.
---
# SMTP alarmový e-mail (ESP-Mail-Client)

## Odesílání
- ESP-Mail-Client na Gmail SMTP přes SSL (`cfg::email::kSmtpPort` 465) s **app password**.
  Host/user/app-password/příjemci žijí v `secrets.h` (git-ignored) — nikdy neloguj. Timeout
  `kSendTimeoutMs`; při selhání nastav `EMAIL_FAILED` a neuvázni (respektuj WDT).

## Politika (stavový automat, host-testovaná — SPECIFICATION.md §7)
- **Nová náběžná hrana** požáru NEBO utrženého/poruchového čidla pošle 1 e-mail. Požár i čidlo
  mají **stejnou** logiku.
- Dokud alarm trvá → **žádný** další automatický e-mail (perzistence sama o sobě neposílá nic).
- Rate-limit per typ: ≥ `cfg::email::kMinIntervalMs` (1 h) mezi automatickými e-maily daného typu.
- Alarm odezní a vznikne znovu → nová hrana → e-mail (stále max 1×/h/typ).
- `email_enabled = false` → neposílá nic (manuální požadavek z webu vrátí hlášku).
- **Manuální** „test e-mail" / „status teď" z webu **obejde** limiter.

## Obsah
Subject + tělo shrnou typ alarmu, aktuální teploty, uptime, IP/hostname. Drž plain a malé.

## Host testy
Proěene proti fake hodinám + fake SMTP: nová hrana → 1 mail; perzistence → ticho; odeznění+znovuvznik
→ mail; rate-limit dodržen; manuální obejití funguje; selhání → `EMAIL_FAILED`.
