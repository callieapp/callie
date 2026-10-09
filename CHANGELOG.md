# Changelog

What changed in each version of Callie, for the people using it. Each section is drafted with
`git cliff` and then written up by hand; see [RELEASING.md](RELEASING.md).

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
