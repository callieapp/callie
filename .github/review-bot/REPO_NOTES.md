# Repo notes - Callie

Facts the review bot cannot infer from one pass over the diff. The philosophy, the finding
categories, the workflow, the scoring, and the output format all live in the bot's base prompt; this
file only refines them for this repo.

## The stack

Get this right before judging anything. **This is not a web app and there is no JavaScript
runtime.**

| Layer     | What it is                                                                 | Where        |
| --------- | -------------------------------------------------------------------------- | ------------ |
| Core      | C++20, Qt 6. `Event`, `CalendarSource`, `EventModel`. No UI, no QML types. | `src/core/`  |
| UI        | QML module `Callie.Ui`. Declarative views plus a `Theme.qml` singleton.    | `src/ui/`    |
| App       | `callie-gui`, a `main.cpp` that loads `Callie.Ui`.                         | `src/gui/`   |
| CLI       | `callie`, a `QCommandLineParser` binary over the same core.                | `src/cli/`   |
| Packaging | Hand-written rpm spec and `debian/`, kept at distro-review quality.        | `packaging/` |
| Website   | Static HTML for callieapp.org, deployed to GitHub Pages.                   | `www/`       |

The JavaScript inside `.qml` files is QML's expression language, not browser JS. There is no DOM, no
`window`, no npm dependency in the shipped product. The only Node in the repo is developer tooling
(commitlint, prettier, markdownlint).

## The normative documents

**Read these before reviewing, and treat them as binding.** A diff that contradicts them is a
finding even when the code is otherwise correct, and the finding should quote the rule it breaks.

- `GUIDELINES.md` - repository layout, conventions, and the list of settled decisions.
- `CONTRIBUTING.md` - commit message rules and the comment standard.
- `DESIGN.md` - the design system, GUI and CLI principles, and the decision log.

The decisions in `GUIDELINES.md` under "Existing decisions" are closed. Do not suggest Electron,
Tauri, GTK, libadwaita, or Kirigami. Do not suggest tasks, scheduling links, or AI features: the
scope is a calendar. Do not propose adopting another desktop's visual language.

Two rules from those documents that are worth checking on every diff because they are easy to
violate and nothing automated catches them:

- **No em dashes** anywhere in comments, documentation, or commit messages. A colon, a comma, or a
  second sentence instead.
- **Comments are one or two sentences and explain why, not what.** A comment that restates the code,
  or runs to a paragraph, is a finding. `TODO` entries use the `TODO(scope):` form.

## This project is early, so calibrate

Callie is pre-release and deliberately incomplete. **Do not report missing features as findings.**
Specifically, these are known and intentional:

- `SampleSource` returns fabricated data. There are no real calendar backends yet.
- Only the week view exists. The day, month and agenda buttons are inert.
- Drag-to-move computes a delta and logs it. There is no write path yet.
- `EventModel` constructs a `SampleSource` in its constructor, marked `TODO(core)`.
- The `www/` legal pages are drafts.

Judge the code that is there. An absent subsystem is on the roadmap in `README.md`, not a defect.

## What CI already covers, so never mention it

On every PR: `clang-format` and `qmlformat` via `scripts/format.sh check`, `qmllint` over the whole
QML module, a Fedora and a Debian build with `-Wall -Wextra -Wpedantic -Werror`, `ctest`,
`markdownlint`, `prettier --check`, and `commitlint`. Formatting, compiler warnings, lint rules and
commit message shape are not your findings.

`qmllint` currently reports zero warnings, so treat any new QML type or property resolution problem
as something CI will catch. Do not duplicate it.

## What CI cannot cover, so it is yours

**Date, time and recurrence correctness is the highest-value dimension here.** Nothing in the
toolchain checks it, and a calendar that is subtly wrong about time is worthless. For every diff
touching `src/core/`:

- timezone handling: is a `QDateTime` constructed with an explicit `QTimeZone`, or does it silently
  take the local zone when it should not;
- DST transitions: does arithmetic add seconds where it should add days, or the reverse;
- all-day events: are they treated as date-only rather than midnight-to-midnight in some zone;
- half-open intervals: overlap tests must treat an event ending exactly when another begins as
  **not** overlapping, which `tests/tst_eventmodel.cpp` pins;
- recurrence expansion: bounds, exceptions, and whether an occurrence's own timezone is respected.

Also yours: model correctness and role plumbing, the C++ and QML boundary, and credential handling.

## Seams

- **`Theme.qml` is the only source of visual values.** A literal colour, spacing number, radius or
  animation duration in any other `.qml` file is a finding. Point at the token that should have been
  used, or say that a new token is needed.
- **Layering.** `src/core/` must not gain a QML or QtQuick dependency: the CLI links it, and
  `EventModel.h` was deliberately stripped of `QML_ELEMENT` for that reason. QML types belong in
  `src/ui/`, exposed through `QML_FOREIGN` as `EventModelForeign.h` does.
- **Date and layout arithmetic belongs in C++.** New date maths inside a `.qml` file is a finding;
  the model should expose a computed role instead. `dayIndex`, `startMinutes`, `durationMinutes`,
  `lane` and `laneCount` exist for exactly this reason.
- **Model roles.** A new role needs an entry in the `Role` enum, a `case` in `data()`, and a name in
  `roleNames()`. Missing any one of the three fails silently at runtime. Role names must not collide
  with a QML base type's own properties: a role named `color` shadowed `Rectangle.color` once
  already, which is why it is now `calendarColor`.
- **QML delegates.** Under `pragma ComponentBehavior: Bound`, every model role a delegate reads must
  be declared as a `required property`. Unqualified access is a real bug, not a style preference.
- **Credentials.** The Google OAuth client secret is injected at build time and must never appear in
  a committed file, a log line, or an error message. Tokens belong in the system keyring through
  QtKeychain, never in a config file or the SQLite cache. OAuth must use a loopback redirect with
  PKCE; the out-of-band flow is deprecated and must not be reintroduced.
- **Network code.** `QNetworkAccessManager` replies must handle the error case, and nothing on the
  sync path may block the UI thread.
- **Packaging.** `packaging/rpm/callie.spec` and `packaging/debian/` are reviewed by distro
  maintainers. A new runtime or build dependency in `CMakeLists.txt` needs a matching entry in both,
  and a new installed file needs a `%files` line. `.gitattributes` keeps `www/` and developer
  tooling out of release tarballs, so anything the build reads at configure or install time must not
  be `export-ignore`d.

## Coverage matrix

- New or changed logic in `src/core/` needs a Qt Test case in `tests/`, following the fake-source
  pattern in `tst_eventmodel.cpp`. Time-dependent tests must pin `TZ`, as `tests/CMakeLists.txt`
  does, rather than reading the host zone.
- A new QML component does not need a test, but it does need to survive `qmllint` and to read its
  values from `Theme.qml`.
- A bug fix in date, timezone or recurrence handling needs a regression test that would have failed
  before the fix. Say so explicitly when one is missing.
