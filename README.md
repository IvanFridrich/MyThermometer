# ESP32 Teplomer — Dev prostředí a workflow

Tento repozitář obsahuje fázové zadání (`SPECIFICATION.md`), globální pravidla
(`CLAUDE.md`) a kompletní sadu 12 agentů (`.claude/agents/`) a 20 skillů (`.claude/skills/`)
pro vývoj s **Claude Code**. Tady je, co si nainstalovat a jak to celé spolu hraje.

---

## 1. Co nainstalovat (Windows)

### Základ
- **VS Code** (poslední verze).
- **Python 3.11+** (pro PlatformIO core a pro `bleak` monitor).
- **Git**.
- **Node.js LTS** + **Claude Code** (`npm i -g @anthropic-ai/claude-code`).

### Toolchain pro firmware
- **PlatformIO IDE** (VS Code rozšíření) — stáhne si vlastní ESP32 Arduino toolchain, nemusíš řešit ručně. Cílový manufacturer/flash řeší PlatformIO.

### Host toolchain pro unit testy + statickou analýzu + coverage (doporučení)
Tady je tvoje otázka „MSYS2 pro code coverage?". Odpověď: **můžeš, ale nedoporučuju
tahat druhý toolchain.** Nejčistší je **jeden LLVM/Clang toolchain pro host**, kterým
pokryješ úplně všechno:

- **LLVM for Windows** (clang, clang-tidy, clang-format, llvm-cov, llvm-profdata) — `winget install LLVM.LLVM`.
  - unit testy: `clang++`
  - statická analýza: `clang-tidy`
  - sanitizery: `-fsanitize=address,undefined` (ASan/UBSan jdou s clang na Windows)
  - **coverage:** `-fprofile-instr-generate -fcoverage-mapping` → `llvm-profdata merge` → `llvm-cov export -format=lcov` (lcov čte Coverage Gutters ve VS Code)
- **include-what-you-use (IWYU)** — buď z LLVM distribuce, nebo build ze zdrojů proti nainstalovanému LLVM.
- (volitelně) **cppcheck** — `winget install Cppcheck.Cppcheck`, doplňkový lint, který umí `pio check`.

**MSYS2 cesta (alternativa):** `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gcc-libs lcov` → build s `--coverage` (gcov) → `gcovr`/`lcov` → HTML/lcov report. Funguje, ale duplikuje toolchain (clang-tidy/ASan stejně poběží přes clang). Pokud netrváš na gcov, **zůstaň u LLVM** a MSYS2 vůbec neinstaluj.

> Shrnutí: **LLVM = jeden toolchain pro testy + tidy + ASan + coverage.** MSYS2 jen
> když chceš gcov-based coverage, což tady není potřeba.

---

## 2. VS Code rozšíření (doporučená sada)

| Rozšíření | K čemu |
|-----------|--------|
| **PlatformIO IDE** | build/upload/monitor/test firmware, `pio check` |
| **clangd** (llvm-vs-code-extensions.vscode-clangd) | IntelliSense + clang-tidy inline |
| **C/C++** (ms-vscode.cpptools) | debugger; pozn. nech zapnutý jen debug, IntelliSense řeš přes clangd ať si nelezou do zelí |
| **Coverage Gutters** | barevné pokrytí v editoru z lcov |
| **CMake Tools** | volitelně pro native test build mimo PlatformIO |
| **Even Better TOML** | `platformio.ini` |
| **Error Lens** | inline chyby/warningy |
| **GitLens** | historie/blame |
| **EditorConfig for VS Code** | `.editorconfig` |
| **markdownlint** | konzistentní docs |
| (volitelně) **Doxygen Documentation Generator** | komentáře |

Tip: ať se neperou dva IntelliSense enginy, v nastavení nech `C_Cpp.intelliSenseEngine: disabled` a používej clangd.

---

## 3. SonarQube — „pokud free" (ověřený stav, 2026)

- **SonarQube Community Build** (dřív Community Edition) je zdarma a open-source, ale **neanalyzuje C/C++** — C-family analyzer je až v komerční Developer Edition+. Pro tenhle projekt tedy self-hosted free build C++ nepokryje.
- **SonarQube Cloud** (dřív SonarCloud) je **zdarma pro open-source / veřejné repo** a C/C++ umí; free plán pokrývá i privátní repo do ~50k řádků. Pro free C++ analýzu je tohle realistická cesta: dej repo na GitHub jako public a napoj SonarQube Cloud.
- Pokud nechceš veřejné repo a chceš zdarma, použij místo SonaruQube kombinaci **clang-tidy + cppcheck + clang-analyzer**, která pro embedded C++ pokryje drtivou většinu toho, co by Sonar hlásil.

> Stav free tierů a jazykové pokrytí se občas mění — před nastavením si ověř aktuální
> podmínky na webu Sonaru. Skill `static-analysis-coverage` popisuje napojení.

---

## 4. Bezpečnostní poznámka k secrets

WiFi heslo a SMTP login jsou „hardcoded v firmware", ale **nikdy
necommittuj** — patří do `secrets.h`, který je v `.gitignore`. V repu je jen
`secrets.h.example` se zástupnými hodnotami. Sken `git status` před každým commitem.

---

## 5. Jak spolu hrají agenti a skilly

- **Skilly** = znalostní balíčky „jak na to" (postupy, API, gotchas). Aktivují se podle popisu, když je úkol relevantní. Nepíšou kód samy — řídí, jak ho psát správně.
- **Subagenti** = izolovaní pracovníci s vlastním kontextem a omezenými nástroji. Hlavní session jim deleguje úzké úkoly a dostane zpět jen souhrn.
- **CLAUDE.md** = invarianty, které platí všude.
- **SPECIFICATION.md** = co se staví a v jakých fázích.

Mentálně: `architect` navrhne → engineer agenti (s pomocí skillů) implementují →
reviewer agenti (read-only) najdou nálezy → engineer opraví → gate fáze se uzavře.

---

## 6. Doporučený workflow s Claude Code

1. Spusť hlavní session na silném modelu (Opus) pro koordinaci a syntézu.
2. Jeď **fázi po fázi** dle `SPECIFICATION.md §9`. Pro každou fázi:
   - „Use the `architect` subagent to …" (jen pokud fáze mění rozhraní),
   - „Use the `<engineer>` subagent to implement … per SPECIFICATION §X using skill `<skill>`",
   - „Use the `code-reviewer` and `<specialized-reviewer>` subagents on the diff",
   - oprav nálezy, pak teprve další fáze.
3. Reviewery drž **read-only** (žádné Write/Edit) — oddělení autora a recenzenta zvyšuje kvalitu.
4. Před uzavřením fáze zkontroluj její Definition of Done.

### Příklady promptů
- `Use the architect subagent to produce the module ADR for Phase 1 per SPECIFICATION.md §4.`
- `Use the firmware-engineer subagent to implement core/alarm_state per SPECIFICATION.md §FR-07,FR-08 and the cpp-review-checklist skill. Then use the test-engineer subagent to write doctest unit tests targeting ≥85% coverage.`
- `Use the safety-security-reviewer subagent to audit the WDT and brownout setup against NFR-02/NFR-03.`

---

## 7. Struktura repozitáře (cílová)
```
.
├── CLAUDE.md
├── SPECIFICATION.md
├── README.md
├── platformio.ini
├── .clang-format / .clang-tidy / .editorconfig / .gitignore
├── secrets.h.example
├── include/            (veřejné hlavičky modulů)
├── src/                (implementace; HW moduly mají *_target.cpp a *_fake.cpp)
├── test/               (host unit testy, doctest)
├── web/                (index.html, app.js — embedováno do firmware)
├── tools/ble_monitor/  (Python bleak skript)
└── .claude/
    ├── agents/         (12 subagentů)
    └── skills/         (20 skillů)
```
