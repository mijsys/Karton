# Faza A - status i blockery

## Zakres
- stabilnosc i bezpieczenstwo sesji
- automatyczne smoke testy po instalacji
- lista blockerow release

## Co zostalo wdrozone
- build scripts (arch/debian/ubuntu) uruchamiaja `run_phase_a_smoke_tests` po instalacji.
- smoke test sprawdza kluczowe binarki KartON: `karton-sessiond`, `karton-settingsd`, `karton-shell`, `karton-settings`, `karton-files`, `karton-terminal`, `karton-lock`, `karton-idle`.
- smoke test ma tryb bezpieczny: `KARTON_SMOKE_NO_LOCK_TEST=1` (pomija inwazyjny `--lock-now`).
- build scripts uruchamiaja smoke w trybie bezpiecznym domyslnie (`KARTON_SMOKE_NO_LOCK_TEST=1`), a pelny test lock mozna wymusic recznie.
- build scripts probuja instalowac zaleznosci Fazy A:
  - polkit agent (pierwszy dostepny z listy)
  - `cliphist`

## Aktualny wynik smoke (lokalnie)
- FAIL: brak polkit agenta
- FAIL: brak `cliphist`

## Blockery release Fazy A
1. Crash `karton-shell` lub `karton-sessiond` podczas normalnej pracy sesji.
2. Brak lockscreen po resume.
3. Brak dzialajacego polkit agenta (brak okien autoryzacji admin).
4. Brak trwalego schowka (`cliphist`) i regresje clipboard.
5. Niezaliczony `repo/smoke-stage1.sh` w trybie strict.

## Plan domkniecia (najblizsze kroki)
1. Przeinstalowac/uruchomic build dla aktywnej dystrybucji i potwierdzic instalacje polkit agenta + cliphist.
2. Odpalic `KARTON_SMOKE_NO_LOCK_TEST=1 repo/smoke-stage1.sh` i doprowadzic do `FAIL=0`.
3. Odpalic pełny smoke z testem lock (`KARTON_SMOKE_NO_LOCK_TEST=0`) w kontrolowanej sesji.
4. Doliczyc scenariusz suspend/resume do checklisty QA i zamknac blocker nr 2.
