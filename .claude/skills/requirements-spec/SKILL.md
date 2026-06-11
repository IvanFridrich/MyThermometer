---
name: requirements-spec
description: Jak udržovat specifikaci a traceability tohoto projektu — formát číslovaných FR/NFR, tabulka rozhodnutí s 🔶 CONSULT OWNER gaty, traceability matice požadavek→modul→test, psaní testovatelných akceptačních kritérií a change control při změně rozsahu. Použij při úpravě SPECIFICATION.md, řešení nejasnosti rozsahu nebo přidávání/změně požadavku.
---
# Specifikace & traceability

## Struktura (SPECIFICATION.md)
- **Tabulka rozhodnutí** (D1…): téma → zvolené řešení → snadno změnitelné? Nejisté položky označ
  `🔶 CONSULT OWNER` a uveď doporučený default. Owner potvrdí/vetuje jednou větou.
- **FR-NN** (funkční) a **NFR-NN** (nefunkční): jeden požadavek = jedna číslovaná, testovatelná věta.
  Vyhni se „a/nebo" větám, co schovají dva požadavky.
- **Datové kontrakty (§6)** jsou závazné; mění se jen přes review.
- **Fázový plán (§9)** s gaty (DoD + review) a **akceptační kritéria (§10/§11)** mapovaná na FR.

## Traceability matice
Tabulka: FR/NFR → modul(y) → test(y) / manuální HW kontrola. Cokoliv nenamapované je díra → eskaluj.
Aktualizuj při každé fázi; finální sign-off dělá `integration-qa`.

## Nejasnost → otázka, ne dohad
Když implementace narazí na neurčité chování, **nevymýšlej** ho. Sepiš krátkou konkrétní otázku
(s doporučeným defaultem) jako `🔶` a počkej na ownera. Lepší pauza než nesprávně postavená funkce.

## Change control
Změna požadavku: zaznamenej, posuď dopad napříč fázemi a agenty, aktualizuj FR/NFR + matici + dotčené skilly.
