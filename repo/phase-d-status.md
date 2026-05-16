# Faza D - status i blockery

## Zakres
- UX polish i spojnosc komunikatow
- pomiary wydajnosci (czas startu, RAM, CPU)
- checklista QA/release i regresja sprintowa
- dokumentacja: instalacja, debugowanie, changelog

## Aktualny status
- Faza D wystartowana po domknieciu Fazy C.
- Gettext dla nowych aplikacji Fazy C jest wdrozony i zweryfikowany (`msgfmt -c`, build, strict smoke).
- Przygotowano pierwszy szkielet checklisty release.

## Blockery
1. brak ujednoliconego raportu wydajnosci sesji (baseline)
2. brak automatycznej regresji `xdg-open` dla probek plikow

## Najblizsze kroki
1. wdrozyc baseline performance: czas startu sesji + pomiar RAM/CPU
2. rozszerzyc smoke-stage3 o scenariusze `xdg-open` na probkach plikow
3. domknac i zweryfikowac checklisty release dla Arch/Debian/Ubuntu/openSUSE
4. zaktualizowac README/CHANGELOG o status MVP i znane ograniczenia
