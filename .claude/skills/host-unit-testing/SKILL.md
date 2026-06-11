---
name: host-unit-testing
description: Jak psát a spouštět Windows/native unit testy doménové vrstvy tohoto projektu — výběr frameworku (preferován doctest), stavba programovatelných HAL fakes, table/property testy, hraniční případy (hystereze, wrap, záporné teploty, fault injection, časování e-mailové logiky, byte-exact BLE payload) a držení coverage na cíli pod AddressSanitizer/UBSan. Použij v každé implementační fázi; testy nejsou volitelné.
---
# Host unit testing

## Setup
- PlatformIO env `native`; framework **doctest** (header-only, rychlý, bez nutnosti výjimek) — GoogleTest přijatelný.
- Kompiluj s `-fsanitize=address,undefined` + coverage instrumentací (LLVM). Linkuj doménové moduly proti **fake** HAL.

## Programovatelné fakes
Fakes musí dát testu plnou kontrolu nad světem:
- vstřik hodnot a poruch čidel (open, CRC, divná hodnota),
- posun fake hodin po minutách / 10min markách,
- zachycení textu LCD, log řádků, frekvence/střídy PWM,
- simulace WiFi up/down a úspěch/selhání/timeout SMTP,
- NVS backovaná in-memory mapou.

## Co testovat (minimum)
- Plovoucí průměr přes okno 10 vzorků; warm-up.
- Plausibilita (−30…80); sentinel + CRC → SENSOR_OPEN/ONEWIRE_ERR.
- Stavový automat požáru (okamžitá hodnota, hystereze — **dokázat, že nekmitá**).
- Stavový automat rozdílu (z průměru, dva směry, hystereze).
- Pravdivostní tabulka doporučení okna (oba cíle × oba směry × pásmo).
- Ring historie: kadence, wrap, OR příznaků per slot.
- Formátování displeje: vejde do 2×8, záporné `-12.3`, glyf °.
- Časování vzoru tónu vč. požárního intervalu kvarty proti fake hodinám.
- Round-trip ConfigStore (fake NVS).
- **E-mailová politika §7:** nová hrana → 1 mail; trvání → ticho; odeznění+znovuvznik → mail; 1×/h/typ; manuální obejití.
- **BLE payload** byte-exact.
- Web JSON serializery (tvar + hodnoty).

## Laťka
Doménová řádková coverage ≥ 85 %; testy tvrdí chování, nejen běží; zelené pod ASan/UBSan v CI.
