<img src="https://raw.githubusercontent.com/callieapp/callie/main/assets/logo.svg"
     width="48" height="48" alt="Callie logo" />

# callie

> the missing linux calendar 📆

---

Callie is an elegant and easy-to-use calendar program for Linux. I started it in the fall of 2026 because I was a
[Morgen](https://www.morgen.so) user that got tired of paying a subscription fee and having
AI features pushed on me. Though I decided to leave
and build my own, I want to thank the Morgen team
for teaching me that Linux calendars don't have to stink!

## Installation

Callie is not released yet. Once it is, it will be available from:

1. FlatHub: `flatpak install org.callieapp.Callie`
2. Fedora: `dnf install callie`
3. Debian/Ubuntu: `apt install callie`
4. [GitHub releases](https://github.com/callieapp/callie/releases)

Installation will provide you the `Callie` desktop app and
a `callie` CLI for those who prefer to live in the terminal 🖥️

Until then, build it from source as described under [Development](#development).

## Using Callie

### The desktop app

`callie-gui` opens the view you used last (day, week, month or agenda; the week at first) with the
calendars you show in Google Calendar. It syncs when it starts and every five minutes, and shows
what it has cached while offline. The title bar says when it last synced, or that a sync failed;
hovering it lists each account's last sync and any calendar that failed, and the button beside it
syncs now. `--sample` shows a made-up week instead, and `--screenshot file.png` saves the window
once it has rendered, then exits. `--now 2026-03-18T10:40` stops the clock at that time and
`--size 1280x840` sets the window size, so a screenshot comes out the same every time.

Pick a theme in Settings, under Appearance. Customize colors copies the theme into
`$XDG_CONFIG_HOME/callie/themes` and edits that copy; built-in themes are never changed. Theme files
can be imported and exported there too, and Callie remembers the choice.

`--theme`, given a built-in theme's name or the path to a theme file, overrides that choice for
one run, as does the `CALLIE_THEME` environment variable. A theme loaded from a file updates the
running app as soon as you save it.

### The `callie` CLI

```sh
callie                                   # upcoming events, same as `callie agenda`
callie agenda --days 3                   # reads the cache; run `callie sync` to refresh it
callie agenda --sample                   # a made-up week, for trying things out
callie accounts                          # connected calendar accounts
callie accounts add google               # sign in with Google in your browser
callie accounts remove google you@example.com
callie calendars                         # calendars in each account, tab-separated
callie sync                              # fetch changes from every account
callie logs                              # log file paths; -f follows them, --open opens the folder
callie status                            # last sync and errors per calendar, keyring and setup
callie doctor                            # details for a bug report; --report opens a new issue
callie --verbose sync                    # any command, printing progress as well as warnings
```

Output meant for scripts goes to stdout, and everything else to stderr. Exit codes are 0 for
success, 1 for an error, and 2 for a usage mistake.

### Logs

The app and the CLI each keep a log in `~/.local/state/callie/logs/`, `callie-gui.log` and
`callie.log`, with the three previous files beside them. Logs record what Callie did, such as
syncs and their errors, but never tokens or event details. The terminal only shows warnings unless
`QT_LOGGING_RULES` is set. To report a bug, use "Report a bug" in the app's ? menu or
`callie doctor --report`, which fill in the details with email addresses masked.

## Development

Callie is C++20 and Qt 6, with the interface in QML. Everyday tasks go through `make`. Run it on
its own to list every command.

```sh
make deps     # install build dependencies on Fedora or Debian (uses sudo)
make setup    # dev tooling, git hooks, and the first configure
make run      # build and launch the app, with logs in the terminal
```

Day to day:

| Command                                 | What it does                                          |
| --------------------------------------- | ----------------------------------------------------- |
| `make watch`                            | Reload QML on save, rebuild and restart on C++ edits  |
| `make cli ARGS="accounts"`              | Run the CLI with arguments                            |
| `make logs`                             | Follow the app's and the CLI's log files              |
| `make gallery THEME=themes/callie.toml` | Preview a theme; edits to the file appear immediately |
| `make screenshots`                      | Render the app and gallery for every built-in theme   |
| `make test`                             | Run the test suite                                    |
| `make format`                           | Fix formatting                                        |
| `make check`                            | Run everything CI runs, before you push               |

`make run` and `make watch` print the app's logs with timestamps. That includes Callie's own
`callie.auth`, `callie.accounts`, `callie.sync`, `callie.theme` and `callie.ui` categories, and
`console.log` from QML.

QML is compiled into the binary, but development builds also accept `--live-qml`, which loads it
from `src/` and reloads the window in place when a `.qml` file is saved. `make watch` uses it, so
only C++ and CMake changes rebuild and restart the app. A QML mistake is logged and the app waits
for the fix. Theme files reload on save in any build.

### Google accounts

Download a Desktop app OAuth client from Google Cloud, save it as `google-oauth-credentials.json`
in the repository root (it is git-ignored), then run `make configure` so CMake picks it up. Without
it the build still works, and `CALLIE_GOOGLE_CLIENT_ID` and `CALLIE_GOOGLE_CLIENT_SECRET` can
supply a client at runtime instead.

```sh
make cli ARGS="accounts add google"
```

### Editors

After a first `make build`, clangd and qmlls work without extra setup: the repository ships a
`.clangd`, and the dev build writes the `.qmlls.ini` files qmlls reads.

### Without make

`make` wraps CMake presets, which packagers and IDEs can use directly:

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --workflow --preset ci   # what CI runs, with warnings as errors
```

## Contributing

Read [GUIDELINES.md](GUIDELINES.md) for the layout and conventions,
[CONTRIBUTING.md](CONTRIBUTING.md) for commits, comments and tests, and [DESIGN.md](DESIGN.md)
before changing how anything looks or prints. Commits follow Conventional Commits, CI must pass
before merging, and pull requests are merged by rebase.

## Roadmap

Nothing here is released yet. Roughly in the order it needs to happen:

### Calendar backends

- [x] Google sign-in with PKCE and a loopback redirect, tokens in the system keyring
- [x] Google Calendar API v3 sync, using sync tokens for incremental updates
- [ ] Submit the OAuth consent screen for verification (unverified apps are capped at
      100 users, and review takes weeks, so this wants starting early)
- [ ] CalDAV accounts, credentials stored in the system keyring
- [x] SQLite cache so the app works offline
- [x] Replace the placeholder sample data

### The app

- [x] Day, month and agenda views
- [ ] Write path for drag-to-move, so edits actually persist
- [x] Natural language quick add
- [ ] Desktop notifications, with one click to join a video call
- [x] Correct handling of recurring events and cross-timezone meetings
- [ ] Custom window chrome, and a week grid that fits about 8 working hours

### Design

- [x] Settle the accent colour: pink, see `DESIGN.md`
- [x] A component gallery for reviewing tokens and states in one place
- [x] Themes as TOML files, with live reload
- [ ] The other built-in themes, base16 import, and a theme picker
- [ ] Keep close calendar colours apart, and vary their lightness by hue
- [ ] Settle the typefaces and the rest of the open questions in `DESIGN.md`
- [ ] Hand-tuned 16px and 32px icons

### Project

- [x] Tests, starting with overlap layout, accounts and sign-in
- [x] CI that builds, lints and tests on Fedora and Debian
- [ ] Publish to a Fedora COPR and a Debian repo, then submit to the official archives

### Email and domain

- [ ] Verify callieapp.org in Resend (DNS records are live)
- [ ] Decide how `hello@` and `privacy@` forward, and wire it up
- [ ] Send-as from those addresses, so replies come from the right place

Copyright (c) 2026 Lara Kelley (larakelley.com). MIT License.
