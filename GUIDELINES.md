# Callie

An elegant, open source calendar for Linux. Qt6 + QML for the UI, C++ for the core.
MIT licensed. App ID `org.callieapp.Callie`, homepage `callieapp.org`.

## Layout

| Path         | What                                                                                  |
| ------------ | ------------------------------------------------------------------------------------- |
| `src/core/`  | `libcalliecore`: event model, accounts, Google OAuth and API. No UI and no QML types. |
| `src/ui/`    | The QML module `Callie.Ui`: views, components, and `Theme.qml`.                       |
| `src/gui/`   | `callie-gui` entry point. `main.cpp` only.                                            |
| `src/cli/`   | `callie`, the command-line interface over the same core.                              |
| `tests/`     | Qt Test cases run by ctest. See `tst_eventmodel.cpp` for the fake-source pattern.     |
| `scripts/`   | `format.sh`, the single formatter entry point used by CMake and CI.                   |
| `data/`      | Desktop entry and AppStream metainfo.                                                 |
| `assets/`    | Logo, also installed as the application icon.                                         |
| `packaging/` | Hand-written `rpm/callie.spec` and `debian/`, at distro-review quality.               |
| `www/`       | The callieapp.org site, deployed to GitHub Pages. Excluded from release tarballs.     |

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

CMake presets drive everything. `cmake --workflow --preset ci` runs exactly what CI runs.

```sh
cmake --preset dev              # configure into build/dev
cmake --build --preset dev
ctest --preset dev

./build/dev/src/gui/callie-gui  # desktop app
./build/dev/src/cli/callie agenda

cmake --workflow --preset ci    # configure, build, qmllint and test, warnings as errors
cmake --build --preset dev --target format        # rewrite formatting
cmake --build --preset dev --target format-check  # verify formatting
```

`clang-format` comes from `clang-tools-extra` on Fedora and `clang-format` on Debian. Without it
the format target skips C++ and reports it.
