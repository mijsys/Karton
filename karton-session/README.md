<!-- SPDX-FileCopyrightText: 2026 mijsys -->

# karton-session

Oddzielny projekt usług sesyjnych dla Karton.

Zawiera skrypty:
- karton-launcher
- karton-notifyd
- karton-screenshot
- karton-settingsd
- karton-sessiond

`karton-screenshot` jest kompilowanym programem (C), nie skryptem powloki.

## Install

```bash
meson setup builddir
meson install -C builddir
```

## Translations (.po)

Project uses gettext domain `karton-session` with catalogs in [po](po).

- Active languages: `pl`, `de`
- Add a language: create `po/<lang>.po` and append `<lang>` in `po/LINGUAS`

Runtime examples:

```bash
KARTON_LOCALEDIR="$HOME/.local-karton/share/locale" LANG=pl_PL.UTF-8 karton-sessiond
KARTON_LOCALEDIR="$HOME/.local-karton/share/locale" LANG=pl_PL.UTF-8 LANGUAGE=de karton-settingsd
```
