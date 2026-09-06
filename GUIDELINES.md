# Callie

An elegant, open source calendar for Linux. Qt6 + QML for the UI, C++ for the core.
MIT licensed. App ID `org.callieapp.Callie`, homepage `callieapp.org`.

## Layout

| Path         | What                                                                              |
| ------------ | --------------------------------------------------------------------------------- |
| `src/core/`  | `libcalliecore`: `Event`, `CalendarSource`, `EventModel`. No UI.                  |
| `src/ui/`    | The QML module `Callie.Ui`: views, components, and `Theme.qml`.                   |
| `src/gui/`   | `callie-gui` entry point. `main.cpp` only.                                        |
| `src/cli/`   | `callie`, the command-line interface. Links QtCore only, so it stays fast.        |
| `data/`      | Desktop entry and AppStream metainfo.                                             |
| `assets/`    | Logo, also installed as the application icon.                                     |
| `packaging/` | Hand-written `rpm/callie.spec` and `debian/`, at distro-review quality.           |
| `www/`       | The callieapp.org site, deployed to GitHub Pages. Excluded from release tarballs. |

## Conventions

Read `CONTRIBUTING.md` before writing commits or comments, and `DESIGN.md` before
changing anything visual or any CLI output. In short:

- Conventional Commits, enforced by commitlint. Lower-case subject, no trailing period,
  72 character header.
- Comments are one or two sentences and explain why, not what. No em dashes anywhere in
  comments, docs, or commit messages.
- Every visual value in QML reads a token from `src/ui/Theme.qml`. `DESIGN.md` says
  what each token is for.
- Date and layout arithmetic belongs in C++ models, not QML.

## Existing decisions

Do not re-litigate these without being asked:

- Qt6 + QML, not Electron, Tauri, or GTK4/libadwaita.
- No Kirigami. Callie has its own visual identity and does not adopt KDE's.
- Scope is a calendar only. No tasks, scheduling links, or AI features.
- Google Calendar uses the v3 REST API, not CalDAV, for sync tokens and conference data.
- Distribution is an own repo first, official Fedora and Debian later.

## Build

```
cmake -B build -GNinja
cmake --build build
./build/src/gui/callie-gui
./build/src/cli/callie agenda
```
