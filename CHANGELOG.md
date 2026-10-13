# Changelog

What changed in each version of Callie, for the people using it. Each section is drafted with
`git cliff` and then written up by hand; see [RELEASING.md](RELEASING.md).

## 0.4.1 - 2026-10-13

### New

- Renaming or recoloring a CalDAV calendar changes it on the server too, so other apps see it.
- Guests added to an event in a CalDAV calendar get invitations from the server. Calendars on this
  computer say in the event editor that they cannot send any.
- `.ics` files can go into CalDAV and Google calendars as well as calendars on this computer, from
  the app or with `callie import --calendar`.

### Fixed

- Disabled buttons are flat and grey rather than a faded pink.

## 0.4.0 - 2026-10-12

### New

- CalDAV accounts: connect Fastmail, iCloud, Nextcloud or any CalDAV server in Settings, under
  Accounts, with an app password kept in the system keyring. Callie keeps a copy of each calendar so
  it works offline, sends your changes in the background, and keeps the server's version when an
  event changed in both places. `callie accounts add caldav <server> <user>` does the same in a
  terminal.
- Calendars on this computer: add one in Settings, under Accounts, or with `callie calendars new`.
  Each is a folder of `.ics` files that other apps such as khal can read, and changes made there
  show in Callie as they happen.
- Open an `.ics` file with Callie, or drop one on its window, to add its events to a calendar here;
  `callie import` does the same.
- Subscribe to calendars others publish as a link, such as holidays or a team's schedule, from
  Settings, by opening a `webcal://` link, or with `callie subscribe`. Callie fetches them again
  every hour.
- `callie sync` also syncs CalDAV accounts and subscriptions.

### Changed

- The About window keeps to the website, privacy, terms and source links; Support Callie stays in
  the ? menu.
- The event editor offers a video call only for calendars that can add one.

## 0.3.2 - 2026-10-12

### New

- The week and day views fit your working day in the window, with half an hour either side, and
  their hours grow and shrink as the window does. The time at the top stays put while you resize.
- Settings' Import button takes base16 color schemes (`.yaml`) as well as Callie themes, so a
  terminal or editor palette carries over. Colors are adjusted only where text would be hard to
  read.
- Settings picks where places open from a menu, and the time zone picker shows that it opens a list.

## 0.3.1 - 2026-10-12

### New

- Five more built-in themes, in Settings and in the terminal too:
  - Paper: warm cream paper, dark ink and a fountain-pen blue.
  - Minimal and Minimal Dark: in the manner of Google Calendar, with your desktop's font at lighter
    weights, flat buttons and no bounce.
  - Tomorrow and Tomorrow Night: the Tomorrow palette with its soft purple accent.
- Themes can set how heavy text is, with `weight-strong`, `weight-bold` and `weight-heavy` under
  `[type]`.
- Settings picks the theme, and the calendar new events start in, from a menu, so the list no
  longer grows with every calendar. Its Appearance tab is now called Theme.
- `CALLIE_THEME` picks the theme for the `callie` command and the terminal calendar too.

### Fixed

- In a light terminal, unanswered invitations and calendar dots are drawn dark enough to read.

## 0.3.0 - 2026-10-11

### New

- `callie` on its own in a terminal opens your calendar full screen, in your theme: a day, a week
  or the next two weeks, with the app's vi keys. Open an event with Enter to see its details,
  answer it with y, m or n, join its call with o or delete it with x, and take a change back with
  u. n adds an event the way `callie add` reads it, / searches, i lists invitations and c shows or
  hides calendars. Press ? for every key. Piped, or with `--json` or `--days`, `callie` lists the
  agenda as before.
- The agenda in a terminal takes its colors from your theme: today in the accent, a dot in each
  event's calendar color, and declined events struck through. Set `NO_COLOR` to turn colors off.
- `callie upgrade` installs the newest release, and `callie downgrade` goes back to an earlier
  one, with dnf or apt after checking the download.
- Callie says when a newer release is out: once a day after a command in a terminal, and as
  "Update to Callie X" in the ? menu. Turn it off in Settings, with
  `callie settings checkForUpdates false`, or with `CALLIE_NO_UPDATE_CHECK`.
- Tab completion for bash, zsh and fish, for commands, options, settings, calendar names and
  events. Packages install it; `callie completions bash|zsh|fish` prints it.
- The installer says when Callie is already up to date, reinstalls with `--force`, and installs an
  earlier release with `--version`.

### Fixed

- Callie opens much faster: the window shows in under half the time, and your events right after
  it, without the stalls that made the first moments and the "What's new" window choppy. The
  first start after updating upgrades the cache in place, without syncing again.
- Join opens a call only by a web link. A call link from an invitation that pointed at a file or
  a network share is ignored, and the link in the notes is used instead, if there is one.

## 0.2.3 - 2026-10-11

### New

- Drag an event to the left or right edge of the week, day or month view and hold it there: the
  view moves to the days before or after, and you can drop the event there. Escape puts it back.
- Buttons in the title bar and on an event's card say what they do when you hover them, with the
  keyboard shortcut for the same thing, and the vi key too in vi mode.
- Support Callie on Ko-fi from the ? menu, the About window or callieapp.org.

### Fixed

- The map app and the choice to look places up online are kept when Callie restarts. Callie
  forgot them before, so pick your map app again once after updating.
- An occurrence of a repeating event that was already changed on its own, such as one moved to
  another day in Google Calendar, is edited, moved, answered and deleted on its own, without
  asking about the rest of the series. Invitations and `callie invites` list it separately.
- The title bar folds its buttons away by what actually fits, so nothing overlaps as the window
  narrows, whatever the month's name or the language.
- Dragging an event you cannot change shows a not-allowed cursor.

## 0.2.2 - 2026-10-10

### Fixed

- Callie installed from a package can connect Google accounts. The packages for 0.1.0 to 0.2.1
  were built without the sign-in client Google needs, so Settings and `callie accounts add google`
  said no client was configured.

### New

- Install with one command on Fedora 44 or later and Debian 13 or later:
  `curl -fsSL https://callieapp.org/install.sh | sh`. It checks the download and installs it with
  your package manager, and running it again updates Callie.

## 0.2.1 - 2026-10-09

Fixes and polish from using 0.2.0 every day, and a tray entry.

### New

- While Callie keeps running after its window closes, it has an entry in the system tray: click it
  to open the window, or use its menu to sync or quit. GNOME needs the AppIndicator extension to
  show it.
- Where you work each day, as set in Google Calendar (Home, Office), shows under the day's date
  in the week and day views instead of as an all-day event.

### Fixed

- Overlapping events share the width in columns, and one that starts later sits over a single
  column whose titles already show, so every event's title stays readable. Very short events no
  longer sit on top of the next one, and back-to-back events have a clearer gap.
- The custom repeat form lines up its rows, picks the unit from a menu, and fits the weekdays on
  one line.
- Place and guest suggestions appear in a card under the field instead of pushing the editor
  around. While Callie looks places up on OpenStreetMap, the card says so and keeps the last places
  in view, and Escape closes the card before the editor.
- The refresh button's arrow is drawn whole, and Settings is wide enough for every tab on one row.
- Reminder notifications show Callie's logo even when it runs from a build that is not installed.

## 0.2.0 - 2026-10-09

Callie can now change your events, not just show them, and works the way you do while offline.

### New

- Edit an event from its card: title, times, all day, repeat, place, guests, video call and
  notes. For a repeating event, choose this event, this and the following, or all of them.
- Custom repeats, such as every 2 weeks on Monday and Thursday, ending on a day or after so
  many times.
- Drag events to new times or days, stretch them longer, and drag all-day events and month
  chips. Duplicate, copy and paste events.
- Every change shows at once, can be undone with the toast or Ctrl+Z, and waits to be sent
  while you are offline.
- Places suggest themselves as you type, from your own events and OpenStreetMap, and open in
  the map app of your choice. Turn the online search off in Settings, under General.
- Join a Zoom, Meet, Teams or Webex call even when its link is only in the place or notes.
- Guests are suggested from your events and your Google contacts as you type them, and each
  event lists its guests and how they answered.
- An invitations tray behind the bell, where you answer for one event or the whole series.
- Search events by title, place, notes and guests, from the title bar or with Ctrl+F.
- Keyboard shortcuts for everything, with a help sheet, an optional vi mode and a leader key.
- Rename and recolor calendars and accounts from the sidebar, and choose the calendar new
  events go in.
- Callie lists every calendar on your Google accounts, starting with the ones Google shows;
  turn any of them on or off in the sidebar.
- Week settings: the first day, hiding weekends, week numbers, and shading for working hours.
  Callie takes the week start and clock from Google when you connect.
- Start Callie when you log in, in the background, for reminders.
- Account photos in Settings, and a Reconnect button that marks accounts needing new
  permissions.
- Developer settings: event ids, events as JSON, the data folders, debug logs, and forgetting
  an account's cache to sync it afresh.
- The `callie` command can search, list invitations, add, edit, answer, delete and duplicate
  events, change settings and calendar looks, and suggest contacts.

### Fixed

- Overlapping events cascade instead of squeezing into narrow columns, and maybe answers are
  striped.
- Delete, remove and reset buttons are red, and ask for a second click.
- Clearer refresh and "maybe" icons, and Callie's own icon on its window.
- Narrow windows fold the title bar and open the sidebar as a drawer.
- Personal Google accounts no longer log errors reading a Workspace directory they do not
  have.

## 0.1.0 - 2026-10-06

The first release: a cozy calendar for Linux that keeps up with your Google calendars.

### New

- Connect your Google accounts from the app or the `callie` command, and see every calendar you
  choose, kept in sync in the background and readable offline.
- Day, week, month and agenda views, a sidebar with a greeting, what is up next and a mini month,
  and calendars grouped by account that you can show or hide with a click.
- Create events by typing them, such as "Lunch with Alex tomorrow 12-1pm at Cafe Sol", or by
  dragging across the week.
- Answer invitations, delete events and email the other guests from an event's details, and join
  video calls in one click.
- Settings for the clock (24-hour or AM/PM), the time zone, declined and past events, and a
  roomier today.
- The Callie and Callie Light themes, and your own: customize any color, import and export themes.
- Reminders as desktop notifications, following your Google reminders or a default you choose,
  with buttons to join the call or snooze, and the option to keep Callie running after its window
  closes.
- Sync now from the title bar, with each account's last sync on hover, and "About" and "What's new"
  in the ? menu.
- Help for when things go wrong: copy debug info, open the logs, report a bug, and `callie doctor`.
