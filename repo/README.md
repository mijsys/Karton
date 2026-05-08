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
- `karton-session`

Dostępne skrypty:

- `build-arch.sh`
- `build-opensuse.sh`
- `build-ubuntu.sh`
- `build-debian.sh`
- `install-arch.sh`
- `install-opensuse.sh`
- `install-ubuntu.sh`
- `install-debian.sh`

Każdy skrypt uruchamiaj z katalogu głównego projektu albo bezpośrednio z katalogu `repo` — sam wykrywa root projektu.

## Uruchamianie z GitHuba

Skrypty `install-*.sh` można uruchomić bezpośrednio z konsoli przez `curl`:

```sh
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-arch.sh | bash
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-opensuse.sh | bash
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-debian.sh | bash
curl -fsSL https://raw.githubusercontent.com/mijsys/Tektura-i-Karton/main/repo/install-ubuntu.sh | bash
```

Każdy z nich klonuje aktualne repozytorium do `~/.cache/karton-src` i uruchamia odpowiedni skrypt budowania dla danej dystrybucji.
