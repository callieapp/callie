# Contributing to Callie

## Commit messages

Callie uses [Conventional Commits](https://www.conventionalcommits.org), enforced by
commitlint in CI and optionally by a local hook.

```text
<type>(<scope>): <subject>

<body>

<footer>
```

Types: `build`, `chore`, `ci`, `docs`, `feat`, `fix`, `perf`, `refactor`, `revert`,
`style`, `test`.

Scopes: `core`, `gui`, `qml`, `theme`, `cli`, `google`, `caldav`, `packaging`, `data`,
`site`, `ci`, `deps`. Omit the scope for genuinely cross-cutting changes.

Rules the linter enforces:

- Subject is lower case, imperative mood, no trailing period.
- Header is at most 72 characters.
- Body lines wrap at 100 characters, with a blank line before the body.

Good:

```text
feat(qml): add drag-to-move on week view events
fix(core): correct lane assignment for zero-length events
docs(theme): explain why tokens live in a singleton
```

Bad:

```text
Updated stuff.
feat(qml): Added drag to move events on the week view, which required changes to EventBlock
fix: bug
```

Write the body when the change needs a reason. Explain why, not what. The diff already
says what.

### Local hook

```sh
npm install
git config core.hooksPath .githooks
```

The hook exits silently if `node_modules` is missing, so contributors who do not want a
Node toolchain can rely on CI instead.

## Comments

Comments are for the reader who understands the language but not the decision. Keep them
short and explain intent.

- Use `///` on public types and functions in headers, and on the root of every QML
  component. Say what it is for and what it deliberately does not do.
- Use `//` inline, sparingly, to explain why something non-obvious is the way it is.
- One or two sentences. If a comment needs a paragraph, either the code should be
  restructured or the explanation belongs in a document.
- Sentence case, ending with a period.
- No em dashes. Use a colon, a comma, or a second sentence.
- Do not restate the code. `// increment i` earns nothing.
- Never commit commented-out code. Git remembers it.
- Wrap comments at the same 100 column limit as code.

`TODO` format, so they can be found and retired:

```cpp
// TODO(google): drop once the real source registry lands.
```

Good:

```cpp
/// Occurrences overlapping [from, to), expanded from recurrence rules.
/// Must not block on the network.

// Anchor the seed week to the Monday on or before `from`.
```

Bad:

```cpp
// This function returns the events between two dates - it is very important
// for the calendar to work correctly and is called by the EventModel class
// whenever the range changes.

// loop over the events
```

## Code style

`clang-format`, `qmlformat` and `.editorconfig` handle formatting. One script drives the first
two so that CMake, CI and a local run agree:

```sh
scripts/format.sh fix      # rewrite
scripts/format.sh check    # verify, as CI does
```

Markdown, JSON and YAML are formatted by prettier and linted by markdownlint:

```sh
npm run format:md
npm run lint:md
```

## Tests

Qt Test cases live in `tests/`, one executable per file, registered with ctest.

```sh
ctest --preset dev
```

New logic in `src/core/` needs a test. Follow the fake-source pattern in `tst_eventmodel.cpp`
rather than depending on `SampleSource`, which is time-dependent. Anything asserting on wall-clock
values must pin `TZ`, as `tests/CMakeLists.txt` does.

Beyond formatting:

- Every visual value in QML reads from the `Theme` singleton, whose values come from the
  theme files in `themes/`. Do not hard-code colors, spacing, radii, or durations.
  `DESIGN.md` explains what each token is for and when it may change.
- Visual and CLI-output decisions live in `DESIGN.md`. Read it before changing how
  anything looks or prints, and record what you settle there.
- To work on a theme, run `callie-gallery --theme path/to/theme.toml`. It shows every token
  and component, and updates as you save the file.
- Keep date and layout arithmetic in C++ models. QML positions things, it does not
  compute them.
- Do not add Kirigami or any dependency that imports another desktop's visual language.

## Debugging

- `make logs` follows the app's and the CLI's logs; `callie logs --open` opens the folder.
- `callie status` shows each calendar's last sync and error. `callie --verbose sync` prints progress.
- `callie doctor` prints what a bug report needs, with email addresses masked. `--report` opens a
  new issue with it filled in, as does "Report a bug" in the app's ? menu.
- `make screenshots` renders the app with sample data at a fixed time, and the gallery, for every
  built-in theme. CI attaches the same images to each pull request as the `screenshots` artifact.
- Fedora keeps crash dumps through systemd, as does Debian once `systemd-coredump` is installed.
  `coredumpctl list callie-gui` finds them, and `coredumpctl debug callie-gui` opens the latest in
  gdb. A `make` build has the symbols it needs.
