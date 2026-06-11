---
name: hd44780-lcd-4bit
description: Jak řídit znakový LCD HD44780 2x8 ve 4-bit režimu přes Arduino LiquidCrystal — formátování teplot do 8 sloupců, ošetření ZÁPORNÝCH teplot a zaokrouhlení, custom glyf stupně, a stránkování displeje (teploty trvale; hostname/IP krátce po připojení k WiFi). Použij pro LCD HAL a doménový modul displeje.
---
# HD44780 2x8 LCD (4-bit)

## Driver
- Arduino `LiquidCrystal` ve 4-bit režimu; piny z `cfg::pin::kLcd*`. RW na GND (jen zápis).
- Zaregistruj custom glyf pro `°` ve slotu `cfg::lcd::kDegreeGlyph`.

## Formátování do 8 sloupců (zrádná část)
- 8 znaků/řádek, 2 řádky. Plánuj layouty, co se vejdou, např. `I:23.4°` / `O:-12.3°`
  (znaménko hraje roli — teploty mohou být záporné).
- Zaokrouhli na 1 desetinné místo; zajisti, že znaménko + číslice + `°` nepřeteče 8 znaků
  (defenzivní clamp/formát). Formátovač unit-testuj.

## Stránkování (DisplayController, FR-11/FR-12)
- Default: obě teploty (řádek 1 přepíná I/O, řádek 2 rotuje stránky: IP, uptime, stav/chyby ~3 s/strana).
- Po WiFi (re)connectu: zobraz `teplomer.local` a IP po `cfg::lcd::kShowAddressMs` (~1 min). Žádný
  z řetězců se nevejde do 8 znaků → scroll po řádku / střídej řádky; pak zpět na teploty.
- Redraw na `cfg::lcd::kRefreshMs`. Při výpadku WiFi indikuj `WiFi DN`.

## Host fake
Zachyť přesné znaky zapsané do každého řádku, ať testy tvrdí vykreslené řetězce (vč. záporných hodnot a glyfu °).
