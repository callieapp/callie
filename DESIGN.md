# Callie design

How to use this document: it holds decisions, not aspirations. Every line should be
specific enough that a diff could violate it. If something here is only an adjective,
it is not finished.

Token *values* live in `src/ui/Theme.qml`. This file holds the reasoning behind them.
Do not duplicate values here, they will drift.

Marked `TODO(lara)` items are the ones only Lara can answer. Everything else is settled.

## When in doubt

Do the plainer thing. Callie competes on feeling considered, not on having more.

## North star

Callie should feel like a well-made 2026 desktop application, not a Linux utility.
The bar is Morgen: the reason to leave it was the subscription and the AI features being
pushed, never the craft.

## Anti-goals

These are settled. Do not re-propose them.

- No tasks, scheduling links, availability sharing, or workflows. Callie is a calendar.
- No AI features.
- No accounts, no servers, no telemetry.
- No adopting another desktop's visual language. Not Kirigami, not libadwaita.
- Nothing that looks like a 1990s Linux app: no beveled borders, no gradient buttons,
  no icon-only toolbars, no modal dialog for something that could happen inline.

## Resolved tensions

The point of this section is extrapolation. When a case is not covered elsewhere, reason
from here.

| Tension | Callie picks | Because |
| --- | --- | --- |
| Density vs breathing room | TODO(lara) | |
| Discoverability vs minimalism | TODO(lara) | |
| Consistency vs best answer for one view | Consistency | A design system only pays off if it is obeyed when inconvenient. |
| Animation vs immediacy | TODO(lara) | |

> **TODO(lara):** the first two matter most. A week view can be Fantastical-dense or
> Google-Calendar-airy and both are defensible. Which one, and does the answer change
> between the week grid and the agenda list?

## GUI principles

1. Every visual value reads a token from `Theme.qml`. A literal color, spacing, radius,
   or duration in a `.qml` file is a bug.
2. The accent color means "now" or "selected". It is never decoration.
3. Calendar colors belong to the user's calendars. Callie's own chrome does not compete
   with them, which is why chrome is greyscale plus one accent.
4. Motion explains a change in state. If nothing moved or changed meaning, do not animate.
5. Text is left-aligned. Times and numbers use tabular figures.
6. Hairlines separate, borders enclose. Do not use a border where a hairline will do.
7. No shadow unless something is genuinely floating above the surface, which currently
   means only a dragged event.

> **TODO(lara):** principles 4 and 7 are my defaults, not your decisions. Overrule freely.

## Color

The full palette is in `Theme.qml`. Intent:

- Three greys for surfaces (`bg`, `surface`, `surfaceAlt`) and two for lines
  (`hairline`, `border`). If a new surface needs a sixth grey, the layout is too deep.
- Three text weights (`text`, `textMuted`, `textFaint`). Nothing gets a fourth.
- One accent. One danger. `danger` currently marks the now-indicator, which is arguably
  wrong since nothing is wrong; see open questions.
- Light and dark are equal citizens, not a theme and its inversion. Both get checked.

### Open question: brand versus interface color

The logo is `#cc6699`, matching `git-stk` and `navi`. The app accent is `#3B72E8`, an
unrelated blue. Right now Callie's brand and its interface disagree.

> **TODO(lara):** pick one.
>
> 1. Accent becomes `#cc6699`. Coherent with your other projects, but pink competes with
>    user calendar colors more than blue does, and it reads less like "today".
> 2. Keep blue in the interface, keep pink for brand surfaces only (logo, website,
>    icon). Common and defensible, but the app will never look like the logo.
> 3. Pick a third accent that sits closer to the logo without being it.

## Type

`Inter` at six sizes, 11 through 28. Intent:

- 11 for metadata that should recede, 12 for event titles, 13 for body and UI, 15 for
  day numbers, 20 for the month heading.
- Weight carries hierarchy before size does. Prefer DemiBold at the same size over
  jumping a step.
- Never below 11. Nothing in a calendar is that unimportant.

> **TODO(lara):** `Inter` is not guaranteed present on a user's system and there is no
> fallback stack declared yet. Bundle it, or fall back to the system UI font?

## Spacing and layout

An 8px rhythm with 2, 4 and 12 available for tight cases. `space5` (16) is the default
gap between unrelated things, `space3` (8) between related ones.

Grid metrics (`hourHeight` 56, `gutterWidth` 64, `snapMinutes` 15) are load-bearing:
they set how much of a day is visible without scrolling. Changing `hourHeight` changes
the feel of the entire product, so treat it as a product decision and not a tweak.

## CLI principles

The CLI is a first-class surface, not a debug tool. It follows the conventions in
[clig.dev](https://clig.dev).

1. Human-readable by default, machine-readable on request. Every command that prints data
   supports `--json`.
2. Output that is data goes to stdout. Everything else, including errors and progress,
   goes to stderr. This is what makes `callie agenda --json | jq` work.
3. Colour only when stdout is a TTY, and never when `NO_COLOR` is set. Already honoured.
4. Quiet on success. No "done!" when the absence of an error says it.
5. Exit codes mean something: 0 success, 1 a real error, 2 usage error.
6. Never prompt when a flag could have been passed. Never prompt at all when not a TTY.
7. Times print in the user's local zone, in 24-hour format, with the zone shown only when
   it differs from local.
8. `callie` with no arguments prints the agenda. The most common thing is the default.

> **TODO(lara):** confirm 7 and 8. 24-hour is my assumption and it is a real preference
> question; `--days` defaulting to 7 with no subcommand is also a guess.

## Inspiration

A link alone does not transfer anything. Say what to take and what to ignore.

| Reference | Take this | Not this |
| --- | --- | --- |
| Morgen | TODO(lara) | The subscription, the AI surface |
| TODO(lara) | | |

> **TODO(lara):** three or four entries is plenty. Specific attributes beat a long list.
> Screenshots pasted into a session are more useful than links for anything visual.

## Worked examples

The most useful section in this document once it exists: one correct artifact and one
wrong one, annotated.

> **TODO(lara):** when you next look at the week view and something feels off, screenshot
> it and say why. That becomes an entry here and generalises further than a rule would.

## Decision log

Newest first. Records why, so it is not relitigated.

- **2026-09-06** Logo is a calendar page with binding rings and a six-dot date grid,
  today picked out. Tonal depth in the `#cc6699` family (`#f9e2ee` through `#8f3760`) at
  `navi`'s level of detail rather than `git-stk`'s flat simplicity. An earlier flat
  three-bar version was rejected as too abstract to read as a calendar. A wide outer
  halo was also dropped: `navi` glows because it is an orb, a calendar page does not.
- **2026-09-05** Own design system rather than Kirigami or libadwaita, accepting that QML
  ships no design opinion and Callie must supply all of it.
- **2026-09-05** Qt6 and QML over Electron, Tauri and GTK4. See `GUIDELINES.md`.
