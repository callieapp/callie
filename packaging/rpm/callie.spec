Name:           callie
Version:        0.3.2
Release:        1%{?dist}
Summary:        An elegant calendar for Linux

# The bundled Nunito and Fraunces fonts are OFL-1.1.
License:        MIT AND OFL-1.1
URL:            https://callieapp.org
Source0:        https://github.com/callieapp/callie/releases/download/v%{version}/%{name}-%{version}.tar.xz

BuildRequires:  cmake >= 3.28
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6NetworkAuth)
BuildRequires:  cmake(Qt6Sql)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Test)
BuildRequires:  cmake(KF6CalendarCore)
BuildRequires:  cmake(Qt6Keychain)
BuildRequires:  cmake(tomlplusplus)
BuildRequires:  cmake(ftxui)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

Requires:       hicolor-icon-theme

%description
Callie is a calendar for people who live in their calendar. It connects to
Google Calendar and any CalDAV server, and gives you day, week, month and
agenda views that are quick to read and quick to change. A companion
command-line interface is included for power users.

%prep
%autosetup

%build
%cmake -GNinja -DCALLIE_TUI=ON
%cmake_build

%install
%cmake_install

%check
%ctest
desktop-file-validate %{buildroot}%{_datadir}/applications/org.callieapp.Callie.desktop
appstream-util validate-relax --nonet \
    %{buildroot}%{_datadir}/metainfo/org.callieapp.Callie.metainfo.xml

%files
%license LICENSE data/fonts/Nunito-OFL.txt data/fonts/Fraunces-OFL.txt
%doc README.md
%{_bindir}/callie
%{_bindir}/callie-gui
%{_datadir}/bash-completion/completions/callie
%{_datadir}/zsh/site-functions/_callie
%{_datadir}/fish/vendor_completions.d/callie.fish
%{_datadir}/applications/org.callieapp.Callie.desktop
%{_datadir}/metainfo/org.callieapp.Callie.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/org.callieapp.Callie.svg
%{_datadir}/callie/

%changelog
* Mon Oct 12 2026 Lara Kelley <larakelley@higharc.ai> - 0.3.2-1
- Update to 0.3.2

* Mon Oct 12 2026 Lara Kelley <larakelley@higharc.ai> - 0.3.1-1
- Update to 0.3.1

* Sun Oct 11 2026 Lara Kelley <larakelley@higharc.ai> - 0.3.0-1
- Update to 0.3.0

* Sun Oct 11 2026 Lara Kelley <larakelley@higharc.ai> - 0.2.3-1
- Update to 0.2.3

* Sat Oct 10 2026 Lara Kelley <larakelley@higharc.ai> - 0.2.2-1
- Update to 0.2.2

* Fri Oct 09 2026 Lara Kelley <larakelley@higharc.ai> - 0.2.1-1
- Update to 0.2.1

* Fri Oct 09 2026 Lara Kelley <larakelley@higharc.ai> - 0.2.0-1
- Update to 0.2.0

* Sat Sep 05 2026 Lara Kelley <larakelley@higharc.ai> - 0.1.0-1
- Initial package
