<img src="https://raw.githubusercontent.com/callieapp/callie/main/assets/logo.svg"
     width="48" height="48" alt="Callie logo" />

# callie

> the missing linux calendar 📆

---

Callie is an elegant and easy-to-use calendar program for Linux. I started it in the fall of 2026 because I was a
[Morgen](https://www.morgen.so) user that got tired of paying a subscription fee and having
AI features pushed on me. Though I decided to leave
and build my own, I want the thank the Morgen team
for teaching me that Linux calendars don't have to stink!

## Installation

Callie is available to install via the following sources:

1. FlatHub: `flatpak install org.callieapp.Callie`
2. Fedora: `dnf install callie`
3. Debian/Ubuntu: `apt install callie`
4. [GitHub releases](https://github.com/callieapp/callie/releases)
5. Build from source (see below)

Installation will provide you the `Callie` desktop app and
a `callie` CLI for those who prefer to live in the terminal 🖥️

### Building from source

Requires Qt 6.8+, QtKeychain, CMake and Ninja. On Fedora:

```sh
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtnetworkauth-devel \
    qtkeychain-qt6-devel tomlplusplus-devel cmake ninja-build
```

Then build and run:

```sh
cmake --preset dev
cmake --build --preset dev

./build/dev/src/gui/callie-gui  # desktop app
./build/dev/src/cli/callie      # CLI, prints the agenda
```

To connect Google accounts, download a Desktop app OAuth client from Google Cloud and save it as
`google-oauth-credentials.json` in the repository root, where it is git-ignored. CMake reads it at
configure time. Without it the build still works, and `CALLIE_GOOGLE_CLIENT_ID` and
`CALLIE_GOOGLE_CLIENT_SECRET` can supply a client at runtime instead.

```sh
./build/dev/src/cli/callie accounts add google
```

Editing a `.qml` file needs a rebuild, since QML is compiled into the binary. To run everything
CI runs in one command:

```sh
cmake --workflow --preset ci
```

## The desktop app

TBD

## The `callie` CLI

TBD

## For contributors

TBD

## Roadmap

Nothing here is released yet. Roughly in the order it needs to happen:

### Calendar backends

- [ ] Google Cloud project and OAuth client, loopback redirect with PKCE
- [ ] Google Calendar API v3 sync, using sync tokens for incremental updates
- [ ] Submit the OAuth consent screen for verification (unverified apps are capped at
      100 users, and review takes weeks, so this wants starting early)
- [ ] CalDAV accounts, credentials stored in the system keyring
- [ ] SQLite cache so the app works offline
- [ ] Replace the placeholder sample data

### The app

- [ ] Day, month and agenda views (only week exists)
- [ ] Write path for drag-to-move, so edits actually persist
- [ ] Natural language quick add
- [ ] Desktop notifications, with one click to join a video call
- [ ] Correct handling of recurring events and cross-timezone meetings

### Design

- [ ] Settle the accent colour: brand `#cc6699` or interface blue, see `DESIGN.md`
- [ ] Fill in the open questions in `DESIGN.md` (density, inspiration, font fallback)
- [ ] A component gallery target for reviewing tokens and states in one place

### Project

- [ ] Tests. There are none yet; recurrence and overlap layout need them most
- [ ] CI that builds and runs qmllint, not just commitlint
- [ ] Publish to a Fedora COPR and a Debian repo, then submit to the official archives

### Email and domain

- [ ] Verify callieapp.org in Resend (DNS records are live)
- [ ] Decide how `hello@` and `privacy@` forward, and wire it up
- [ ] Send-as from those addresses, so replies come from the right place

Copyright (c) 2026 Lara Kelley (larakelley.com). MIT License.
