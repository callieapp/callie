# Callie design philosophy

> How to use this document: Every line should be specific enough that a diff could
> violate it. If something here is only an adjective, it's not finished.
> Meant for both human and bot eyes.

---

Token _values_ live in theme files under `themes/`, with `themes/callie.toml` as the default.
This document holds the reasoning behind them.

Things marked `TODO(lara)` items are the ones only Lara should answer as the primary
maintainer and designer.

## When in doubt

Do the plainest thing. Callie is meant to feel intentional, elegant, and considered.
It is not compelling because it has more features or bells and whistles.

## North star

Callie should feel friendly, cute and personal: warm without being busy or messy, the way
Things is. It is never cold, and never so clean that it loses its personality. It looks like
Callie on every desktop rather than blending into GNOME or KDE. The bar for craft is
[Morgen](https://www.morgen.so).

Callie is for Linux enthusiasts. Choices that only make sense for macOS or Windows users do not
apply.

## Non-goals

These are settled decisions; please do not re-propose them!

- No tasks, scheduling links, availability sharing, or workflows.
- No AI or "smart" features, beyond natural language parsing for Quick Add events.
- No accounts, no servers, no telemetry.
- No adopting another desktop's visual language (e.g. Kirigami, libadwaita).
- No theme-supplied code. Themes are data; see Theming.

## Important design choices

| Tension                                 | Callie picks                  | Because                                                          |
| --------------------------------------- | ----------------------------- | ---------------------------------------------------------------- |
| Density vs breathing room               | About 8 working hours visible | A day reads at a glance without Fantastical density.             |
| Discoverability vs minimalism           | TODO(lara)                    |                                                                  |
| Consistency vs best answer for one view | Consistency                   | A design system only pays off if it is obeyed when inconvenient. |
| Animation vs immediacy                  | Quick, with a little bounce   | Motion should feel alive without making anyone wait.             |
| Personality vs cohesion                 | Cohesion                      | One personality applied everywhere, not pockets of whimsy.       |
| Blending in vs looking like Callie      | Looking like Callie           | Its own chrome and identity on any desktop.                      |

## Theming

Theming is a first-class feature, in the spirit of Winamp skins but safer.

1. Every look is a theme file, including the default. No visual value lives anywhere else.
2. Themes are TOML data: colors, fonts, corner radii and motion. They never contain code,
   so a theme shared by a stranger cannot run anything.
3. A theme may set only the values it cares about. Everything it omits comes from the default.
4. Themes can be exported, imported, and edited while Callie is running. base16 color schemes can
   be imported, so a terminal or editor setup carries over.
5. Built-in themes: Callie, Callie Light, Paper, Minimal, Minimal Dark, and Tomorrow Night.
6. Contrast is checked. Body text needs 4.5:1 against its surface, and Callie warns when a user
   theme falls short instead of refusing it.
7. The CLI uses the active theme's colors.

## GUI principles

1. Every visual value reads from the active theme through `Theme`. A literal color, radius or
   duration in a `.qml` file is a bug.
2. Pink appears in a fixed set of places: today, the now line, selection, focus, and the primary
   action. Anywhere else it is decoration and does not belong.
3. Calendar colors belong to the user's calendars, so chrome is plum-tinted neutrals plus pink
   and does not compete with them.
4. Motion explains a change in state: quick, with a slight bounce when something arrives. Never on
   scrolling or text.
5. Text is left-aligned. Times and numbers use tabular figures.
6. Hairlines separate, borders enclose. Do not use a border where a hairline will do.
7. Depth comes from soft shadows on things above the surface: menus, popovers, a dragged event.
   Not on every card. The theme's `[shadow]` sets one shadow for all of them.
8. Things you can press or drag are stickers: rounded (11px on buttons and events, 14 on cards,
   16 on popovers) and raised on a 3px edge in a darker shade. Lists, labels and lines stay flat,
   so the stickers are what draws the eye.
9. Empty states may carry a small, quiet illustration. Nothing else is illustrated. No grain, no
   pixel art.
10. The window works down to small sizes. Layouts collapse; they do not clip.

## Window chrome

Callie draws its own title bar, which doubles as the toolbar: month, navigation and view
switcher in one strip. The window controls are Callie's too, and follow the desktop's
left-or-right button layout setting.

## Color

- Dark first. The default theme is a plum-tinted charcoal, and the accent is the logo's pink
  hue lifted so it reads on dark.
- Three surfaces (`background`, `surface`, `surface-alt`) and two lines (`hairline`, `border`).
  If a layout needs a sixth neutral, it is too deep.
- Three text weights (`text`, `text-muted`, `text-faint`). Nothing gets a fourth.
- One accent, one danger. The now line uses the accent, because nothing is wrong at the current
  time.
- Two edges: `edge` under neutral raised buttons, `accent-edge` under pink ones.
- Light and dark are equal citizens, not a theme and its inversion. Both get checked.

### Calendar colors

Calendar colors are harmonized by default. Each calendar keeps its hue, so calendars stay
distinct, while the theme sets their lightness and saturation so they belong to it. An event is a
pastel sticker: the fill, dark ink of the same hue for its text, and a deeper shade as its edge.
The theme sets the OKLCH lightness and chroma of all three, and warns when the ink falls below
4.5:1 on the fill at any hue. Calendars too
close in hue are nudged apart. Each calendar can instead use its original color or a custom one,
and each theme can turn harmonizing off.

## Type

Nunito for everything you read, and Fraunces for display text: the month heading, day numbers
and the greeting. Both are bundled under the SIL Open Font License, and a theme may name any
installed family instead.

Sizes run from 11 to 28, each a `Theme` value. Intent:

- 11 for metadata that should recede, 12 for event titles, 13 for body and UI, 14 for list
  items such as calendar names, 15 for card titles, 18 for day numbers, 22 for the month
  heading and the greeting.
- Weight carries hierarchy before size does. Prefer a heavier weight at the same size over
  jumping a step. Event titles are ExtraBold, so a busy week still reads at a glance.
- Never below 11. Nothing in a calendar is that unimportant.

## Spacing and layout

An 8px rhythm with 2, 4 and 12 available for tight cases. `space5` (16) is the default
gap between unrelated things, `space3` (8) between related ones.

The week view should show about 8 working hours without scrolling, with the hour height scaling to
the window down to a minimum. The current fixed `hourHeight` of 56 is a placeholder until that
lands. Grid metrics set the feel of the whole product, so treat them as product decisions, not
tweaks. Spacing and grid metrics are not themeable.

## CLI principles

The CLI is a first-class surface, not a debug tool. It follows the conventions in
[clig.dev](https://clig.dev).

1. Human-readable by default, machine-readable on request. Every command that prints data
   supports `--json`.
2. Output that is data goes to stdout. Everything else, including errors and progress,
   goes to stderr. This is what makes `callie agenda --json | jq` work.
3. Colors come from the active theme, only when stdout is a TTY, and never when `NO_COLOR` is set.
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

| Reference                                | Take this                                                   | Not this                         |
| ---------------------------------------- | ----------------------------------------------------------- | -------------------------------- |
| Things                                   | Warmth without busyness, restraint, generous spacing        |                                  |
| [larakelley.com](https://larakelley.com) | Warm palettes, switchable personalities, monospace details  |                                  |
| Logitech MX Keys                         | Soft rounded forms, matte surfaces, a quiet glow, precision |                                  |
| Winamp skins                             | Themes as shareable files that change the whole personality | Theme-supplied code              |
| [Morgen](https://www.morgen.so)          | The bar for craft                                           | The subscription, the AI surface |

## Worked examples

The most useful section in this document once it exists: one correct artifact and one
wrong one, annotated.

> **TODO(lara):** when you next look at the week view and something feels off, screenshot
> it and say why. That becomes an entry here and generalises further than a rule would.
