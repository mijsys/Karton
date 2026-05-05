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

Każdy skrypt uruchamiaj z katalogu głównego projektu albo bezpośrednio z katalogu `repo` — sam wykrywa root projektu.
