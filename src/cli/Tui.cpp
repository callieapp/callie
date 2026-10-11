#include "Tui.h"

#include "Palette.h"
#include "TuiState.h"

#include "callie/Settings.h"
#include "callie/Times.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QTimer>

#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

using namespace Qt::StringLiterals;

namespace callie::cli {

namespace {

using ftxui::Element;
using ftxui::Elements;

ftxui::Color rgb(const QColor &color)
{
    return ftxui::Color::RGB(uint8_t(color.red()), uint8_t(color.green()), uint8_t(color.blue()));
}

Element label(const QString &text)
{
    return ftxui::text(text.toStdString());
}

/// What the TUI draws with: the theme, and the times as the user reads them.
struct Look
{
    const Palette &palette;
    const Settings &settings;
    /// The terminal's width, in cells.
    int width = 80;
    const ThemeSpec::Colors &c = palette.colors();
};

QString when(const Event &event, const Look &look)
{
    if (event.allDay)
        return QObject::tr("all day");
    return QObject::tr("%1 to %2")
        .arg(look.settings.times()->time(event.start), look.settings.times()->time(event.end));
}

/// An event as a sticker, in its calendar's colors as the app draws it: hollow
/// until answered, struck through when declined.
Element sticker(const Event &event, const QString &text, const Look &look)
{
    using namespace ftxui;
    const QColor fill = look.palette.fill(event.color);
    Element body = label(u' ' + text + u' ');
    if (event.responseStatus == u"needsAction")
        body = body | color(rgb(fill)) | bold;
    else
        body = body | color(rgb(look.palette.ink(event.color))) | bgcolor(rgb(fill));
    if (event.declined)
        body = body | strikethrough;
    return body;
}

/// The marker beside the selected event.
Element marker(bool selected, const Look &look)
{
    using namespace ftxui;
    return selected ? text("▌") | color(rgb(look.c.accent)) : text(" ");
}

/// The widest of the events' times, so they line up.
int timeWidth(const TuiState &state, const Look &look)
{
    qsizetype widest = 0;
    for (const Event &event : state.events())
        widest = std::max(widest, when(event, look).size());
    return int(widest) + 2;
}

Element eventRow(const TuiState &state, int index, int timeColumn, const Look &look)
{
    using namespace ftxui;
    const Event &event = state.events().at(index);
    const bool selected = index == state.selected();
    Element row =
        hbox({marker(selected, look),
              label(when(event, look).leftJustified(timeColumn)) | color(rgb(look.c.textMuted)),
              sticker(event, event.summary, look),
              selected && !event.location.isEmpty()
                  ? label(u"  "_s + event.location) | color(rgb(look.c.textFaint))
                  : text(""),
              filler()});
    if (event.end <= state.now())
        row = row | dim;
    return selected ? row | focus : row;
}

QString dayHeading(QDate day, const TuiState &state)
{
    const QString name = QLocale().toString(day, u"dddd, MMMM d"_s);
    return day == state.today() ? QObject::tr("Today, %1").arg(name) : name;
}

Element empty(const QString &text, const Look &look)
{
    using namespace ftxui;
    return label(u"  "_s + text) | color(rgb(look.c.textFaint));
}

Element dayView(const TuiState &state, const Look &look)
{
    Elements rows;
    const int timeColumn = timeWidth(state, look);
    for (int i = 0; i < state.events().size(); ++i)
        rows.push_back(eventRow(state, i, timeColumn, look));
    if (rows.empty())
        rows.push_back(empty(QObject::tr("Nothing on this day."), look));
    return ftxui::vbox(std::move(rows));
}

Element agendaView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Elements rows;
    QDate current;
    const int timeColumn = timeWidth(state, look);
    for (int i = 0; i < state.events().size(); ++i) {
        const QDate day = state.dayOf(state.events().at(i));
        if (day != current) {
            current = day;
            if (!rows.empty())
                rows.push_back(text(""));
            rows.push_back(label(dayHeading(day, state)) | bold |
                           color(rgb(day == state.today() ? look.c.accent : look.c.text)));
        }
        rows.push_back(eventRow(state, i, timeColumn, look));
    }
    if (rows.empty())
        rows.push_back(empty(QObject::tr("Nothing in the next two weeks."), look));
    return vbox(std::move(rows));
}

Element weekView(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    // Seven equal columns, whatever their events hold, between six rules and the margins.
    const int column = std::max(4, (look.width - 2 - 6) / 7);
    Elements columns;
    for (int d = 0; d < 7; ++d) {
        const QDate day = state.rangeStart().addDays(d);
        const bool today = day == state.today();
        Elements cells;
        cells.push_back(label(QLocale().toString(day, u"ddd d"_s)) | bold |
                        color(rgb(today ? look.c.accent : look.c.textMuted)));
        for (int i = 0; i < state.events().size(); ++i) {
            const Event &event = state.events().at(i);
            if (state.dayOf(event) != day)
                continue;
            const QString time =
                event.allDay ? QString() : look.settings.times()->time(event.start) + u' ';
            Element cell = hbox({marker(i == state.selected(), look),
                                 sticker(event, time + event.summary, look) | flex});
            if (event.end <= state.now())
                cell = cell | dim;
            cells.push_back(i == state.selected() ? cell | focus : cell);
        }
        columns.push_back(vbox(std::move(cells)) | size(WIDTH, EQUAL, column));
        if (d < 6)
            columns.push_back(separatorLight() | color(rgb(look.c.hairline)));
    }
    return hbox(std::move(columns));
}

QString title(const TuiState &state)
{
    const QLocale locale;
    switch (state.view()) {
    case TuiState::View::Day: return dayHeading(state.day(), state);
    case TuiState::View::Week: {
        const QDate first = state.rangeStart(), last = first.addDays(6);
        return first.month() == last.month() ? QObject::tr("%1 to %2")
                                                   .arg(locale.toString(first, u"MMMM d"_s),
                                                        locale.toString(last, u"d, yyyy"_s))
                                             : QObject::tr("%1 to %2")
                                                   .arg(locale.toString(first, u"MMMM d"_s),
                                                        locale.toString(last, u"MMMM d, yyyy"_s));
    }
    case TuiState::View::Agenda: return QObject::tr("From %1").arg(dayHeading(state.day(), state));
    }
    return {};
}

Element header(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Elements tabs;
    const std::pair<TuiState::View, QString> views[] = {
        {TuiState::View::Day, QObject::tr("Day")},
        {TuiState::View::Week, QObject::tr("Week")},
        {TuiState::View::Agenda, QObject::tr("Agenda")}};
    for (const auto &[view, name] : views) {
        Element tab = label(u' ' + name + u' ');
        tabs.push_back(view == state.view() ? tab | bold | color(rgb(look.c.accentText)) |
                                                  bgcolor(rgb(look.c.accent))
                                            : tab | color(rgb(look.c.textMuted)));
    }
    return hbox({label(title(state)) | bold, filler(), hbox(std::move(tabs))});
}

Element footer(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Element left = label(state.syncStatus()) |
                   color(rgb(state.syncFailed() ? look.c.danger : look.c.textMuted));
    if (state.pending() == u"leader")
        left = label(QObject::tr("d day   w week   a agenda")) | color(rgb(look.c.accent));
    return hbox(
        {left, filler(), label(QObject::tr("? keys   q quit")) | color(rgb(look.c.textFaint))});
}

Element help(const Look &look)
{
    using namespace ftxui;
    const QString leader = look.settings.leaderKey();
    const std::pair<QString, QString> keys[] = {
        {u"h  l"_s, QObject::tr("Earlier, later")},
        {u"t"_s, QObject::tr("Today")},
        {u"j  k"_s, QObject::tr("Next, previous event")},
        {u"gg  G"_s, QObject::tr("First, last event")},
        {leader + u" d"_s, QObject::tr("Day view")},
        {leader + u" w"_s, QObject::tr("Week view")},
        {leader + u" a"_s, QObject::tr("Agenda view")},
        {u"Tab"_s, QObject::tr("Next view")},
        {u"r"_s, QObject::tr("Sync now")},
        {u"q"_s, QObject::tr("Quit")},
    };
    Elements rows;
    for (const auto &[key, what] : keys) {
        rows.push_back(
            hbox({text(" "), label(key.leftJustified(8)) | bold | color(rgb(look.c.accent)),
                  label(what), text(" ")}));
    }
    return window(label(QObject::tr(" Keys ")) | bold, vbox(std::move(rows))) |
           color(rgb(look.c.text)) | bgcolor(rgb(look.c.surface)) | clear_under | center;
}

Element render(const TuiState &state, const Look &look)
{
    using namespace ftxui;
    Element body = state.view() == TuiState::View::Week     ? weekView(state, look)
                   : state.view() == TuiState::View::Agenda ? agendaView(state, look)
                                                            : dayView(state, look);
    Element page = vbox({header(state, look), separatorLight() | color(rgb(look.c.hairline)),
                         body | yframe | flex, footer(state, look)});
    Element screen = hbox({text(" "), page | flex, text(" ")}) | color(rgb(look.c.text)) |
                     bgcolor(rgb(look.c.background));
    return state.helpShown() ? dbox({screen, help(look)}) : screen;
}

/// The name TuiState gives a key, or nothing for one it does not take.
QString keyName(const ftxui::Event &event)
{
    using ftxui::Event;
    if (event == Event::ArrowLeft)
        return u"Left"_s;
    if (event == Event::ArrowRight)
        return u"Right"_s;
    if (event == Event::ArrowUp)
        return u"Up"_s;
    if (event == Event::ArrowDown)
        return u"Down"_s;
    if (event == Event::Home)
        return u"Home"_s;
    if (event == Event::End)
        return u"End"_s;
    if (event == Event::Return)
        return u"Enter"_s;
    if (event == Event::Escape)
        return u"Escape"_s;
    if (event == Event::Tab)
        return u"Tab"_s;
    if (event.is_character())
        return QString::fromStdString(event.character());
    return {};
}

} // namespace

QString renderTui(const TuiState &state, const Settings &settings, const Palette &palette,
                  int width, int height)
{
    auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, render(state, Look{palette, settings, width}));
    // Without the escape codes for colors and styles.
    static const QRegularExpression codes(u"\x1b\\[[0-9;?]*[A-Za-z]"_s);
    return QString::fromStdString(screen.ToString()).remove(codes).remove(u'\r');
}

int runTui(TuiState &state, const Settings &settings, const Palette &palette)
{
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto view = ftxui::Renderer(
        [&] { return render(state, Look{palette, settings, ftxui::Terminal::Size().dimx}); });
    auto keyed = ftxui::CatchEvent(view, [&](const ftxui::Event &event) {
        const QString key = keyName(event);
        if (key.isEmpty())
            return false;
        state.press(key);
        if (state.quitRequested())
            screen.ExitLoopClosure()();
        return true;
    });
    // Qt runs the loop, so syncs and changes go on while FTXUI draws a step at a time.
    ftxui::Loop loop(&screen, keyed);
    QTimer tick;
    tick.setInterval(15);
    QObject::connect(&tick, &QTimer::timeout, &tick, [&loop] {
        loop.RunOnce();
        if (loop.HasQuitted())
            QCoreApplication::quit();
    });
    QObject::connect(&state, &TuiState::changed, &tick,
                     [&screen] { screen.PostEvent(ftxui::Event::Custom); });
    tick.start();
    return QCoreApplication::exec();
}

} // namespace callie::cli
