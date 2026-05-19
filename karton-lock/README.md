<!-- SPDX-FileCopyrightText: 2026 mijsys -->

# karton-lock

`karton-lock` is a native GTK/Wayland lock screen.

It creates one fullscreen `gtk4-layer-shell` overlay surface per monitor.

## Configuration

Copy style template:

```bash
mkdir -p ~/.config/karton
cp /usr/local/share/karton/lock-style.example ~/.config/karton/lock-style
```

(or from your user prefix path, for example `~/.local-karton/share/karton/lock-style.example`).

Background image is read from:

- `~/.config/karton/lockscreen-path`

Unlock authentication (first successful method wins):

- PAM (`karton-lock` service by default)
- `KARTON_LOCK_PASSWORD`
- `~/.config/karton/lock-password` (first line)

If PAM is enabled in build, `karton-lock` PAM profile is installed to `/etc/pam.d/karton-lock`.
When no explicit `KARTON_LOCK_PAM_SERVICE` is set and `karton-lock` is unavailable, runtime falls back to `login`.

Password format:

- plain text (default)
- `sha256:<hex>` for hashed password comparison

Bruteforce guard:

- after 5 wrong attempts, unlock is delayed for 3 seconds

## Runtime overrides

Supported runtime variables:

- `KARTON_LOCK_PASSWORD`
- `KARTON_LOCK_PAM_SERVICE`
- `KARTON_LOCK_PAM_USER`
