<img src="https://raw.githubusercontent.com/callieapp/callie/main/assets/logo.svg"
     width="48" height="48" alt="Callie logo" />

# Callie

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
4. GitHub releases [(link)](https://github.com/callieapp/callie/releases)
5. Build from source (see below)

Installation will provide you the `Callie` desktop app and
a `callie` CLI for those who prefer to live in the terminal 🖥️

### Building from source

Requires Qt 6.6+, KCalendarCore, CMake and Ninja. On Fedora:

```sh
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kcalendarcore-devel \
    qtkeychain-qt6-devel libical-devel cmake ninja-build
```

Then build and run:

```sh
cmake -B build -GNinja
cmake --build build

./build/src/gui/callie-gui     # desktop app
./build/src/cli/callie agenda  # CLI
```

Editing a `.qml` file needs a rebuild, since QML is compiled into the binary. Lint it
before committing:

```sh
cmake --build build --target all_qmllint
```

## The desktop app

TBD

## The `callie` CLI

TBD

## For contributors

TBD

## Roadmap

TBD

Copyright (c) 2026 Lara Kelley (larakelley.com). MIT License.
