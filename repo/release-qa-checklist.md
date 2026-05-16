# Release QA Checklist (Faza D)

## Build i instalacja
- [ ] build przechodzi na Arch
- [ ] build przechodzi na Debian
- [ ] build przechodzi na Ubuntu
- [ ] build przechodzi na openSUSE
- [ ] instalacja metapakietu domyka zaleznosci runtime

## Smoke i integralnosc
- [ ] `repo/check-session-integrity.sh` przechodzi bez FAIL
- [ ] `repo/smoke-stage1.sh` przechodzi
- [ ] `repo/smoke-stage2.sh` przechodzi
- [ ] `repo/smoke-stage3.sh --strict` przechodzi

## UX i aplikacje bazowe
- [ ] `karton-images` otwiera obraz i dziala zoom/fit
- [ ] `karton-media` otwiera media i dziala play/pause/stop
- [ ] `karton-text` otwiera/zapisuje plik tekstowy
- [ ] `karton-pdf` uruchamia backend PDF
- [ ] przełącznik jasny/ciemny dziala we wszystkich nowych apkach
- [ ] MIME defaults wskazuja na `io.karton.*`

## i18n
- [ ] `msgfmt -c` przechodzi dla `karton-session/po/pl.po`
- [ ] `msgfmt -c` przechodzi dla `karton-session/po/de.po`
- [ ] nowe napisy UI z Fazy C maja wpisy w katalogach `.po`

## Dokumentacja
- [ ] README zawiera aktualny status Fazy C/D
- [ ] roadmapa odzwierciedla aktywna faze
- [ ] changelog zawiera zmiany user-visible
