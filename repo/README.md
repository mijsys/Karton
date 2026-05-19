# Repozytorium Tektura i Karton

Autor: mijsys

Ten katalog zawiera pomocnicze pliki do przygotowania repozytorium Git oraz budowania środowiska KartON/Tektura na popularnych dystrybucjach.

Docelowy remote GitHub:

https://github.com/mijsys/Tektura-i-Karton.git

## Przygotowanie Git

Z katalogu głównego projektu:

```sh
git init
git remote add origin https://github.com/mijsys/Tektura-i-Karton.git
git add .
git commit -m "Initial KartON/Tektura workspace"
git branch -M main
git push -u origin main
```

Push wymaga zalogowanego GitHuba albo tokena z uprawnieniami do repozytorium.

## Budowanie

Skrypty w tym katalogu instalują zależności i budują:

- `tektura`
- `karton-shell`
- `karton-settings`
- `karton-files`
- `karton-session`

Dodatkowo skrypty próbują zainstalować i skonfigurować stack logowania `greetd` + GTK greeter
(`greetd-gtkgreet` albo odpowiednik nazwy na danej dystrybucji) z motywem KartON.
Pliki motywu trafiają do:

- `/etc/greetd/gtkgreet-karton.css`
- `/etc/greetd/gtkgreet.toml`

Jeżeli istnieje niestandardowy `/etc/greetd/config.toml`, skrypt robi kopię zapasową i
podmienia sekcję `default_session` na konfigurację KartON.

Dostępne skrypty:

- `build-arch.sh`
- `archlinux/build-repo.sh`
- `build-ubuntu.sh`
- `build-debian.sh`
- `install-arch.sh`
- `install-ubuntu.sh`
- `install-debian.sh`

Każdy skrypt uruchamiaj z katalogu głównego projektu albo bezpośrednio z katalogu `repo` — sam wykrywa root projektu.

## Uruchamianie z GitHuba

Skrypty `install-*.sh` można uruchomić bezpośrednio z konsoli przez `curl`:

```sh
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-arch.sh | bash
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-debian.sh | bash
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-ubuntu.sh | bash
```

Każdy z nich klonuje aktualne repozytorium do `~/.cache/karton-src` i uruchamia odpowiedni skrypt budowania dla danej dystrybucji.

## Arch Linux: pacman repo

W katalogu `repo/archlinux` znajduje się split package dla Arch Linux z metapakietem `kartonde`.

Pakiety docelowe:

- `tektura`
- `karton-shell`
- `karton-settings`
- `karton-files`
- `karton-session`
- `kartonde` (metapakiet instalujący całe środowisko)

Uwagi dot. zaleznosci runtime (Faza B):

- Arch (`kartonde`): metapakiet wymaga `lxqt-policykit` (lekki, niezalezny od GNOME), wiec po `pacman -S kartonde` agent polkit jest instalowany razem ze srodowiskiem.
- Debian/Ubuntu: w tym repo nie ma jeszcze natywnego pakietu `.deb` metapakietu `kartonde`; skrypty `build-debian.sh` i `build-ubuntu.sh` instaluja wymagany pakiet agenta (`policykit-1-gnome`/alternatywy) podczas instalacji zaleznosci.

Preferencja agenta polkit w automatyce KartON jest neutralna desktopowo:
- `lxqt-policykit` (preferowany)
- `mate-polkit` (fallback)
- `polkit-gnome` (fallback)

Budowanie binarnych paczek i bazy repozytorium:

```sh
cd repo/archlinux
./build-repo.sh
```

Po tym w `repo/archlinux/x86_64` znajdziesz:

- pakiety `*.pkg.tar.zst`
- bazę repozytorium `kartonde.db`
- listę plików `kartonde.files`

Aby dodać repozytorium do Arch Linux przez `pacman`, dodaj do `/etc/pacman.conf`:

```ini
[kartonde]
SigLevel = Optional TrustAll
Server = https://cdn.jsdelivr.net/gh/mijsys/Tektura-i-Karton@main/repo/archlinux/$arch
```

Następnie:

```sh
sudo pacman -Sy kartonde
```

Paczka `kartonde` instaluje też domyślne pliki sesji do `/etc/xdg/karton` i korzysta z pliku sesji Wayland `karton.desktop` instalowanego przez `tektura`.

## Roadmapa Arch Linux

Pełny plan działań dla Arch Linux znajduje się w `repo/ROADMAP-ARCH.md`.
