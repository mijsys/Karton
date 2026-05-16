Name:           kartonde
Version:        0.8.0
Release:        3
Summary:        Complete KartONDE desktop environment metapackage
License:        GPL-2.0-only
URL:            https://github.com/mijsys/Tektura-i-Karton
Source0:        %{name}-%{version}.tar.gz
Source1:        config.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  meson
BuildRequires:  ninja
BuildRequires:  pkgconf-pkg-config
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-protocols)
BuildRequires:  pkgconfig(wlroots-0.20)
BuildRequires:  pkgconfig(cairo)
BuildRequires:  pkgconfig(pango)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(gtk4)
BuildRequires:  pkgconfig(gdk-pixbuf-2.0)
BuildRequires:  pkgconfig(libpulse)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(libinput)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(vte-2.91-gtk4)
BuildRequires:  pkgconfig(gtk4-layer-shell-0)
BuildRequires:  pkgconfig(systemd)
BuildRequires:  scdoc
BuildRequires:  pam-devel

Requires:       tektura
Requires:       karton-shell
Requires:       karton-settings
Requires:       karton-files
Requires:       karton-terminal
Requires:       karton-session
Requires:       karton-idle
Requires:       karton-lock
Requires:       (polkit-gnome or lxqt-policykit or mate-polkit)
Requires:       swayidle
Requires:       swaylock
Requires:       wlopm
Requires:       xdg-desktop-portal
Requires:       xdg-desktop-portal-gtk
Requires:       wl-clipboard
Requires:       evince
Recommends:     cliphist

%description
Complete KartONDE desktop environment metapackage.

%package -n tektura
Summary:  KartonDE Wayland compositor based on Tektura
%description -n tektura
Compositor

%package -n karton-shell
Summary: KartonDE shell
%description -n karton-shell
Shell

%package -n karton-settings
Summary: KartonDE settings
%description -n karton-settings
Settings

%package -n karton-files
Summary: KartonDE GTK file manager
%description -n karton-files
Files

%package -n karton-terminal
Summary: KartonDE terminal emulator
%description -n karton-terminal
Terminal

%package -n karton-session
Summary: Session services and desktop helpers
%description -n karton-session
Session

%package -n karton-idle
Summary: Wayland desktop idle manager for KartONDE
%description -n karton-idle
Idle manager

%package -n karton-lock
Summary: Wayland screen locker for KartONDE
%description -n karton-lock
Locker

%prep
%autosetup -n %{name}-%{version} -b 1

%build
for comp in tektura karton-shell karton-settings karton-files karton-terminal karton-idle karton-lock karton-session; do
    if [ "$comp" = "tektura" ]; then
        meson setup "$comp/builddir" "$comp" --prefix=/usr -Dsystemd-session=enabled -Dicon=disabled
    else
        meson setup "$comp/builddir" "$comp" --prefix=/usr
    fi
    meson compile -C "$comp/builddir"
done

%install
for comp in tektura karton-shell karton-settings karton-files karton-terminal karton-idle karton-lock karton-session; do
    DESTDIR=%{buildroot} meson install -C "$comp/builddir"
done

install -d %{buildroot}/etc/xdg/karton
install -Dm755 repo/archlinux/config/autostart %{buildroot}/etc/xdg/karton/autostart
install -Dm644 repo/archlinux/config/environment %{buildroot}/etc/xdg/karton/environment
install -Dm644 repo/archlinux/config/rc.xml %{buildroot}/etc/xdg/karton/rc.xml
install -Dm644 repo/archlinux/config/theme.toml %{buildroot}/etc/xdg/karton/theme.toml

%find_lang karton-files
%find_lang karton
%find_lang karton-lock
%find_lang karton-session
%find_lang karton-settings
%find_lang karton-shell
%find_lang karton-terminal

%files
%dir /etc/xdg/karton
/etc/xdg/karton/*

%files -n tektura -f karton.lang
%{_bindir}/karton
%{_bindir}/karton-session-start
%{_bindir}/labnag
%{_bindir}/lab-sensible-terminal
%{_datadir}/wayland-sessions/karton.desktop
%{_datadir}/xdg-desktop-portal/karton-portals.conf
%{_datadir}/themes/KartONFlat/
%{_datadir}/icons/hicolor/scalable/apps/karton.svg
%{_datadir}/icons/hicolor/scalable/apps/karton-symbolic.svg
%{_userunitdir}/karton-session.target

%files -n karton-shell -f karton-shell.lang
%{_bindir}/karton-shell
%{_bindir}/karton-system-status
%{_bindir}/karton-top-panel
%{_bindir}/karton-side-dock

%files -n karton-settings -f karton-settings.lang
%{_bindir}/karton-settings
%{_datadir}/applications/io.karton.Settings.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.karton.Settings.svg

%files -n karton-files -f karton-files.lang
%{_bindir}/karton-files
%{_datadir}/applications/io.karton.Files.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.karton.Files.svg

%files -n karton-terminal -f karton-terminal.lang
%{_bindir}/karton-terminal
%{_datadir}/applications/io.karton.Terminal.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.karton.Terminal.svg

%files -n karton-session -f karton-session.lang
%{_bindir}/karton-screenshot
%{_bindir}/karton-password-dialog
%{_bindir}/karton-bootstrap-user
%{_bindir}/karton-images
%{_bindir}/karton-launcher
%{_bindir}/karton-login-manager
%{_bindir}/karton-media
%{_bindir}/karton-notifyd
%{_bindir}/karton-notify-log
%{_bindir}/karton-pdf
%{_bindir}/karton-apply-theme
%{_bindir}/karton-settingsd
%{_bindir}/karton-sessiond
%{_bindir}/karton-text
%{_datadir}/applications/io.karton.Images.desktop
%{_datadir}/applications/io.karton.Media.desktop
%{_datadir}/applications/io.karton.Text.desktop
%{_datadir}/applications/io.karton.PDF.desktop
%{_datadir}/karton/theme.toml.example

%files -n karton-idle
%{_bindir}/karton-idle

%files -n karton-lock -f karton-lock.lang
%{_bindir}/karton-lock
%config(noreplace) /etc/pam.d/karton-lock
%{_datadir}/karton/lock-style.example

%changelog
* Fri May 15 2026 Patryk <patryk@example.com> 0.7.7-3
- Bump packaging release to 3 after karton-files 0.7.7-3 update.

* Thu May 14 2026 Patryk <patryk@example.com> 0.7.7-1
- Add automatic per-user bootstrap on session start (defaults, desktop entries, icons).

* Thu May 14 2026 Patryk <patryk@example.com> 0.7.7-0
- Version bump to 0.7.7.

* Thu May 14 2026 Patryk <patryk@example.com> 0.7.6-0
- Initial openSUSE package layout
