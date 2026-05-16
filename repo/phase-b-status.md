# Faza B - status i blockery

## Zakres
- integracja systemowa: lockscreen + idle + polityki po wybudzeniu
- polkit agent i realne akcje administracyjne
- xdg-desktop-portal + backend sesji i testy D-Bus
- spiecie zachowan z karton-settings

## Co wdrozono
- dodano osobny smoke test Fazy B: `repo/smoke-stage2.sh`
- wszystkie skrypty build uruchamiaja Faze B po Fazie A:
  - `repo/build-arch.sh`
  - `repo/build-debian.sh`
  - `repo/build-ubuntu.sh`
  - `repo/build-opensuse.sh`
- `repo/build-common.sh` zawiera `run_phase_b_smoke_tests` z trybem strict:
  - `KARTON_SMOKE_STAGE2_STRICT=0` (domyslnie: ostrzezenia)
  - `KARTON_SMOKE_STAGE2_STRICT=1` (blokujacy fail)
- smoke Fazy B weryfikuje:
  - binarki lock/idle/settings
  - obecność binarek polkit/portal
  - opcjonalny lock probe (`KARTON_SMOKE_STAGE2_NO_LOCK_TEST`)
  - runtime procesy portalu i agenta polkit
  - endpoint D-Bus portalu (`org.freedesktop.portal.Desktop`)
- `karton-sessiond` ma rozszerzone wykrywanie binarek polkit agenta (sciezki + nazwy binarne), co poprawia autostart miedzy dystrybucjami.
- Kolejnosc preferencji agenta jest neutralna desktopowo: `lxqt-policykit` -> `mate-polkit` -> `polkit-gnome` (build scripts + runtime autostart).
- metapakiet Arch `kartonde` wymaga teraz `lxqt-policykit` (neutralny agent, bez zaleznosci od GNOME) (`repo/archlinux/PKGBUILD`, `.SRCINFO`).
- metapakiet openSUSE `kartonde` wymaga agenta przez zaleznosc alternatywna: `(polkit-gnome or lxqt-policykit or mate-polkit)`.
- `repo/check-session-integrity.sh` pilnuje regresji tych zaleznosci pakietowych.

## Aktualny wynik smoke-stage2 (lokalnie)
- FAIL: brak kandydata polkit agenta
- FAIL: brak uruchomionego procesu polkit agenta
- PASS: portal + backend + D-Bus endpoint dzialaja
- PASS: interfejsy portalu dostepne (`FileChooser`, `Screenshot`, `ScreenCast`)
- strict runner (`KARTON_SMOKE_STAGE2_STRICT=1`): FAIL zgodnie z oczekiwaniem (blokada na polkit)
- proba instalacji `polkit-gnome` na CachyOS/Arch zatrzymana na interaktywnym haśle sudo

## Blockery done Fazy B
1. brak dzialajacego polkit agenta w sesji (binarka + runtime)
2. brak potwierdzonych testow realnej akcji admin z dialogiem autoryzacji
3. brak zamknietej procedury testow lock po resume w QA

## Najblizsze kroki
1. zainstalowac pakiet polkit agenta dla aktywnej dystrybucji i potwierdzic autostart przez `karton-sessiond`
2. uruchomic ponownie `repo/smoke-stage2.sh` do `FAIL=0`
3. wykonac test realnej akcji admin (np. `pkexec`) i potwierdzic pojawienie sie promptu
4. odpalic test lock po resume i dopisac wynik do statusu

## Komenda domkniecia punktu 1 (lokalnie)
- `sudo pacman -S --needed lxqt-policykit`
- po instalacji: `KARTON_SMOKE_STAGE2_NO_LOCK_TEST=1 bash repo/smoke-stage2.sh`
- finalnie strict: `KARTON_SMOKE_STAGE2_NO_LOCK_TEST=1 KARTON_SMOKE_STAGE2_STRICT=1 bash -lc 'source repo/build-common.sh; run_phase_b_smoke_tests'`
