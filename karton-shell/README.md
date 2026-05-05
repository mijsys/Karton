<!-- SPDX-FileCopyrightText: 2026 mijsys -->

# karton-shell

Oddzielny projekt paneli shell-layer dla Karton.

## Build

```bash
meson setup builddir
meson compile -C builddir
```

## Run

```bash
./builddir/karton-shell
./builddir/karton-shell --top-only
./builddir/karton-shell --side-only
```

## Translations (.po)

Project uses gettext domain `karton-shell` with catalogs in [po](po).

- Active languages: `pl`, `de`
- Add a language: create `po/<lang>.po` and append `<lang>` in `po/LINGUAS`

Runtime examples:

```bash
KARTON_LOCALEDIR="$HOME/.local-karton/share/locale" LANG=pl_PL.UTF-8 ./builddir/karton-shell --help
KARTON_LOCALEDIR="$HOME/.local-karton/share/locale" LANG=pl_PL.UTF-8 LANGUAGE=de ./builddir/karton-shell --help
```

## Styling (CSS-like)

Shell supports a lightweight CSS-like variables file.

- Default path: `~/.config/tekstura/shell.css`
- Override path: `KARTON_SHELL_CSS=/path/to/shell.css`
- Example variables file: [shell.css.example](shell.css.example)

Example run:

```bash
KARTON_SHELL_CSS="$HOME/.config/tekstura/shell.css" ./builddir/karton-shell
```
