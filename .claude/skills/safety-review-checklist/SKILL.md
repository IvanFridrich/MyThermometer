---
name: safety-review-checklist
description: Safety/robustnost/security review checklist pro toto síťově napájené zařízení bez zálohy — watchdog krmení a recovery, brownout, základní MPU, fail-safe chování při fault injection (utržení čidla, výpadek WiFi, selhání SMTP, OOM), zacházení se secrets, vystavené web akce a korektnost požárního/e-mailového řetězce. Použij při review jakékoliv fáze sahající na robustnost, síť, secrets nebo alarmy; tvrdý gate.
---
# Safety / security review checklist

Read-only review; nálezy **Critical / Warning / Suggestion** s file:line. Zařízení musí **selhávat
bezpečně** (alarm raději signalizuje). Páruje se s agentem `safety-security-reviewer`.

## Watchdog & recovery
- [ ] Každý dlouhožijící task registrován u Task WDT; žádné nekrmené blokující volání > 8 s.
- [ ] Umělé zaseknutí → reset (ověřeno na HW), ne tiché uváznutí. `panic = true`.

## Brownout & reset
- [ ] HW BOD zapnutý na konzervativní úrovni; čistý reset; bezpečná reinicializace stavu.
- [ ] `BROWNOUT_RECOVER` při odpovídajícím reset reason; ztráta RAM historie je dle spec OK.

## MPU/PMP (základní)
- [ ] `-fstack-protector-strong`, nespustitelný stack; tvrzení odpovídají realitě (žádné přehánění).

## Fault injection (nesmí spadnout/uváznout)
- [ ] Utržení čidla → `SENSOR_OPEN`, běží dál, signalizuje.
- [ ] CRC bouře → flag + rozumný retry.
- [ ] Ztráta WiFi/špatné RF → nekonečný backoff reconnect, `WIFI_DOWN`.
- [ ] Selhání/timeout SMTP → `EMAIL_FAILED`, žádné uváznutí.
- [ ] OOM → degraduj (zahoď web request, měř dál); žádný neohraničený růst.

## Secrets & web
- [ ] `secrets.h` v `.gitignore`; creds nikdy v logu/webu/chybách.
- [ ] Web akce (restart, e-mail, kontrast, config) validují vstup, nejdou dohnat k pádu.
- [ ] Zaznamenáno, že „bez auth" je **akceptované riziko**; doporučeno Basic Auth/HTTPS jako mitigace.

## Alarmy & e-mail
- [ ] Požár = inner, okamžitá hodnota, hystereze. E-mailová politika §7 přesně (1×/h/typ, neposílá při
      perzistenci, manuální obejití).
