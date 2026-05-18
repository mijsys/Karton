# Faza C - status i blockery

## Zakres
- aplikacje podstawowe: zdjecia, multimedia, tekst/PDF
- integracja MIME i domyslnych aplikacji
- gotowosc codziennego workflow bez dodatkowych instalacji

## Co wdrozono
- uruchomiono etap operacyjny Fazy C: `repo/smoke-stage3.sh`
- smoke Fazy C sprawdza:
  - baseline KartON (`karton-files`, `karton-terminal`, `karton-settings`)
  - obecność aplikacji dla kategorii: image/media/text/pdf (KartON lub fallback systemowy)
  - bazowe domyslne skojarzenia MIME (`image/png`, `video/mp4`, `text/plain`, `application/pdf`)
- dodano runner `run_phase_c_smoke_tests` w `repo/build-common.sh`
- skrypty build/install (Arch, Debian, Ubuntu) probuja automatycznie instalowac fallback app dla image/media/text/pdf.
- wdrozono natywne aplikacje KartON (MVP launchery): `karton-images`, `karton-media`, `karton-text`, `karton-pdf` + desktop entries `io.karton.*`.

## Aktualny status
- Faza C domknieta operacyjnie.
- MVP app stack jest domkniety jako natywny zestaw KartON launcherow (`karton-images`, `karton-media`, `karton-text`, `karton-pdf`) z fallback backendami.
- strict smoke Fazy C przechodzi lokalnie po instalacji `karton-session`.
- MIME defaults sa ustawiane na `io.karton.*` podczas instalacji (`install.sh`) i bootstrapu usera (`karton-bootstrap-user`), co eliminuje fallback do przegladarki dla obrazow/PDF.
- Gettext jest podlaczony w nowych aplikacjach Fazy C i katalogi `po/pl.po`, `po/de.po` zawieraja tlumaczenia nowych komunikatow UI/status.

## Blockery done Fazy C
1. brak

## Najblizsze kroki
1. wejscie w Faze D: checklista release i QA regresji
2. rozszerzyc testy o otwarcie przykładowych plikow (image/video/txt/pdf) przez xdg-open
3. pomiary wydajnosci startu sesji (czas, RAM, CPU)
4. domknac dokumentacje release (instalacja, debug, changelog)
