# Callie

An elegant, open source calendar for Linux. Qt6 + QML for the UI, C++ for the core.
MIT licensed. App ID `org.callieapp.Callie`, homepage `callieapp.org`.

## Layout

| Path           | What                                                                                  |
| -------------- | ------------------------------------------------------------------------------------- |
| `src/core/`    | `libcalliecore`: event model, accounts, Google OAuth and API. No UI and no QML types. |
| `src/ui/`      | The QML module `Callie.Ui`: views, components, and the `Theme` singleton.             |
| `src/gui/`     | `callie-gui` entry point. `main.cpp` only.                                            |
| `src/cli/`     | `callie`, the command-line interface over the same core.                              |
| `src/gallery/` | `callie-gallery`, a development tool that renders a theme's tokens and components.    |
| `tests/`       | Qt Test cases run by ctest. See `tst_eventmodel.cpp` for the fake-source pattern.     |
| `scripts/`     | `format.sh`, the single formatter entry point used by CMake and CI.                   |
| `themes/`      | Built-in theme files. `callie.toml` is the default every other theme inherits from.   |
| `data/`        | Desktop entry and AppStream metainfo.                                                 |
| `assets/`      | Logo, also installed as the application icon.                                         |
| `packaging/`   | Hand-written `rpm/callie.spec` and `debian/`, at distro-review quality.               |
| `www/`         | The callieapp.org site, deployed to GitHub Pages. Excluded from release tarballs.     |

## Conventions

Read `CONTRIBUTING.md` before writing commits or comments, and `DESIGN.md` before
changing anything visual or any CLI output. In short:

- Conventional Commits, enforced by commitlint. Lower-case subject, no trailing period,
  72 character header.
- Comments are one or two sentences and explain why, not what. No em dashes anywhere in
  comments, docs, or commit messages.
- Every visual value in QML reads from the `Theme` singleton, whose values come from
  `themes/*.toml`. `DESIGN.md` says what each token is for.
- Date and layout arithmetic belongs in C++ models, not QML.
- Qt 6.8 (Debian trixie) cannot add a bare `u"..."` literal to a `QString`; use `QStringLiteral` or
  the `_s` suffix. Newer Qt accepts it, so only the Debian build catches it.

## Existing decisions

Do not re-litigate these without being asked:

- Qt6 + QML, not Electron, Tauri, or GTK4/libadwaita.
- No Kirigami. Callie has its own visual identity and does not adopt KDE's.
- Scope is a calendar only. No tasks, scheduling links, or AI features.
- Google Calendar uses the v3 REST API, not CalDAV, for sync tokens and conference data.
- Distribution is an own repo first, official Fedora and Debian later.

## Build

Run `make` to list the commands; `make check` runs what CI runs. The Makefile wraps CMake presets,
which work directly too: `cmake --workflow --preset ci` is CI's build, lint and test.

`make run` and `make watch` enable Callie's logging categories and QML `console.log`. Log through
`lcAuth`, `lcAccounts`, `lcSync`, `lcTheme` or `lcUi` from `callie/Logging.h`, never `qDebug()`, and never log tokens.
