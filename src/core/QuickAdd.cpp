#include "callie/QuickAdd.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kTime = uR"((?:(\d{1,2})(?::(\d{2}))?\s*(am|pm|a\.m\.|p\.m\.|a|p)?|noon|midnight))"_s;
const QString kWeekdays =
    uR"((mon|monday|tue|tues|tuesday|wed|wednesday|thu|thur|thurs|thursday|fri|friday|sat|saturday|sun|sunday))"_s;
const QString kMonths =
    uR"((jan|january|feb|february|mar|march|apr|april|may|jun|june|jul|july|aug|august|sep|sept|september|oct|october|nov|november|dec|december))"_s;

QRegularExpression pattern(const QString &source)
{
    return QRegularExpression(source, QRegularExpression::CaseInsensitiveOption);
}

/// A time as written, before a missing am or pm is settled.
struct ClockTime
{
    int hour = -1;
    int minute = 0;
    /// "am", "pm", or empty when the text did not say.
    QString meridiem;
    /// Written as 24-hour (15:00) or as a word, so never adjusted.
    bool exact = false;

    [[nodiscard]] bool isValid() const { return hour >= 0; }

    [[nodiscard]] QTime withMeridiem(const QString &assumed) const
    {
        const QString m = meridiem.isEmpty() ? assumed : meridiem;
        int h = hour;
        if (!exact && h <= 12) {
            if (m == u"pm" && h < 12)
                h += 12;
            else if (m == u"am" && h == 12)
                h = 0;
        }
        return QTime(h % 24, minute);
    }
};

/// Reads one time from the captures of kTime starting at `first`.
ClockTime clockTime(const QRegularExpressionMatch &match, int first)
{
    ClockTime time;
    const QString whole = match.captured(first - 1).toLower().trimmed();
    if (whole.endsWith(u"noon"_s)) {
        time.hour = 12;
        time.exact = true;
        return time;
    }
    if (whole.endsWith(u"midnight"_s)) {
        time.hour = 0;
        time.exact = true;
        return time;
    }
    time.hour = match.captured(first).toInt();
    time.minute = match.captured(first + 1).isEmpty() ? 0 : match.captured(first + 1).toInt();
    const QString m = match.captured(first + 2).toLower();
    if (!m.isEmpty())
        time.meridiem = m.startsWith(u'a') ? u"am"_s : u"pm"_s;
    // Above 12, or a leading zero, is a 24-hour clock.
    time.exact = time.hour > 12 || match.captured(first).startsWith(u'0');
    if (time.hour > 23 || time.minute > 59)
        time.hour = -1;
    return time;
}

/// A bare hour with no am or pm: mornings from 8 to 11, afternoons otherwise,
/// since few events start at 3 in the night.
QString guessMeridiem(const ClockTime &time)
{
    return time.hour >= 8 && time.hour <= 11 ? u"am"_s : u"pm"_s;
}

int weekday(const QString &name)
{
    static const QStringList names{u"mon"_s, u"tue"_s, u"wed"_s, u"thu"_s,
                                   u"fri"_s, u"sat"_s, u"sun"_s};
    return int(names.indexOf(name.left(3).toLower())) + 1;
}

int monthNumber(const QString &name)
{
    static const QStringList names{u"jan"_s, u"feb"_s, u"mar"_s, u"apr"_s, u"may"_s, u"jun"_s,
                                   u"jul"_s, u"aug"_s, u"sep"_s, u"oct"_s, u"nov"_s, u"dec"_s};
    return int(names.indexOf(name.left(3).toLower())) + 1;
}

/// The date in `month`/`day` on or after `today`, rolling into next year.
QDate upcoming(const QDate &today, int month, int day)
{
    QDate date(today.year(), month, day);
    if (date.isValid() && date < today)
        date = QDate(today.year() + 1, month, day);
    return date;
}

/// Removes the first match of `re` from `text` and returns it.
QRegularExpressionMatch take(QString &text, const QRegularExpression &re)
{
    const QRegularExpressionMatch match = re.match(text);
    if (match.hasMatch())
        text.replace(match.capturedStart(), match.capturedLength(), u" "_s);
    return match;
}

} // namespace

EventDraft QuickAdd::parse(const QString &text, const QDateTime &now, const QTimeZone &zone)
{
    QString rest = u' ' + text + u' ';
    const QDateTime local = now.toTimeZone(zone);
    const QDate today = local.date();

    // Length: "for 30 min", "for 2 hours", "for 1.5h".
    int minutes = 0;
    if (const auto m = take(
            rest,
            pattern(
                uR"(\s(?:for\s+)(\d+(?:\.\d+)?)\s*(h|hr|hrs|hour|hours|m|min|mins|minute|minutes)\b)"_s));
        m.hasMatch()) {
        const double amount = m.captured(1).toDouble();
        minutes =
            m.captured(2).startsWith(u'h', Qt::CaseInsensitive) ? int(amount * 60) : int(amount);
    }

    // A range: "3-4pm", "3pm to 5pm", "from 3 to 4:30". Times that cannot
    // exist, such as 25:00, stay in the title like impossible dates do.
    ClockTime start;
    ClockTime end;
    const QRegularExpressionMatch range =
        pattern(uR"(\s(?:from\s+|at\s+)?()"_s + kTime + uR"()\s*(?:-|to|until|till)\s*()"_s +
                kTime + uR"()(?=\s))"_s)
            .match(rest);
    if (range.hasMatch() && clockTime(range, 2).isValid() && clockTime(range, 6).isValid()) {
        start = clockTime(range, 2);
        end = clockTime(range, 6);
        rest.replace(range.capturedStart(), range.capturedLength(), u" "_s);
    } else if (!range.hasMatch()) {
        // A bare number is only a time after "at", or with am, pm or a colon.
        auto matches = pattern(uR"(\s(at|@)?\s*()"_s + kTime + uR"()(?=\s))"_s).globalMatch(rest);
        while (matches.hasNext()) {
            const QRegularExpressionMatch m = matches.next();
            const bool marked = !m.captured(1).isEmpty() || !m.captured(4).isEmpty() ||
                                !m.captured(5).isEmpty() || !m.captured(2).front().isDigit();
            if (!marked || !clockTime(m, 3).isValid())
                continue;
            start = clockTime(m, 3);
            rest.replace(m.capturedStart(), m.capturedLength(), u" "_s);
            break;
        }
    }

    // The day.
    QDate date;
    if (const auto m = take(rest, pattern(uR"(\s(today|tonight|tomorrow|tmrw|tmr)(?=\s))"_s));
        m.hasMatch()) {
        const QString word = m.captured(1).toLower();
        date = word == u"today" || word == u"tonight" ? today : today.addDays(1);
        if (word == u"tonight" && !start.isValid())
            start = ClockTime{19, 0, u"pm"_s, true};
    } else if (const auto m =
                   take(rest, pattern(uR"(\s(?:on\s+)?(next\s+)?)"_s + kWeekdays + uR"((?=\s))"_s));
               m.hasMatch()) {
        int ahead = (weekday(m.captured(2)) - today.dayOfWeek() + 7) % 7;
        if (ahead == 0)
            ahead = 7;
        if (!m.captured(1).isEmpty())
            ahead += 7;
        date = today.addDays(ahead);
    } else if (const auto m =
                   take(rest, pattern(uR"(\sin\s+(\d+)\s+(day|days|week|weeks)(?=\s))"_s));
               m.hasMatch()) {
        const int count = m.captured(1).toInt();
        date =
            today.addDays(m.captured(2).startsWith(u'w', Qt::CaseInsensitive) ? count * 7 : count);
    } else {
        // Written dates. One that cannot exist, such as feb 30, stays in the
        // title rather than moving the event to some other day.
        const auto dated = [&](const QRegularExpression &re, auto read) {
            const QRegularExpressionMatch m = re.match(rest);
            if (!m.hasMatch())
                return false;
            const QDate found = read(m);
            if (found.isValid()) {
                date = found;
                rest.replace(m.capturedStart(), m.capturedLength(), u" "_s);
            }
            return true;
        };
        dated(pattern(uR"(\s(\d{4})-(\d{2})-(\d{2})(?=\s))"_s),
              [](const QRegularExpressionMatch &m) {
                  return QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
              }) ||
            dated(pattern(uR"(\s(?:on\s+)?)"_s + kMonths +
                          uR"(\s+(\d{1,2})(?:st|nd|rd|th)?(?=\s))"_s),
                  [&](const QRegularExpressionMatch &m) {
                      return upcoming(today, monthNumber(m.captured(1)), m.captured(2).toInt());
                  }) ||
            dated(pattern(uR"(\s(?:on\s+)?(\d{1,2})(?:st|nd|rd|th)?\s+(?:of\s+)?)"_s + kMonths +
                          uR"((?=\s))"_s),
                  [&](const QRegularExpressionMatch &m) {
                      return upcoming(today, monthNumber(m.captured(2)), m.captured(1).toInt());
                  });
    }

    // Whatever follows "at" or "@" is the place, if it has a letter in it:
    // "at 25" is a time that cannot exist, not a place.
    QString location;
    if (const auto m = take(rest, pattern(uR"(\s(?:at|@)\s+(.*\p{L}.*?)\s*$)"_s)); m.hasMatch())
        location = m.captured(1).trimmed();

    EventDraft draft;
    draft.summary = rest.simplified();
    // Words left dangling once their day or time was taken out.
    static const QRegularExpression dangling = pattern(uR"(\s+(on|at|from|for|in)$)"_s);
    draft.summary.remove(dangling);
    draft.location = location;

    if (start.isValid()) {
        QString assumed = start.meridiem;
        if (assumed.isEmpty() && end.isValid() && !end.meridiem.isEmpty()) {
            // "11-1pm" starts in the morning, "3-4pm" in the afternoon.
            assumed = end.meridiem;
            if (start.withMeridiem(assumed) > end.withMeridiem(end.meridiem))
                assumed = assumed == u"pm" ? u"am"_s : u"pm"_s;
        }
        if (assumed.isEmpty())
            assumed = guessMeridiem(start);
        const QTime startTime = start.withMeridiem(assumed);
        QDate day = date.isValid() ? date : today;
        if (!date.isValid() && startTime < local.time())
            day = day.addDays(1);
        draft.start = QDateTime(day, startTime, zone);
        if (end.isValid()) {
            QString endMeridiem = end.meridiem;
            if (endMeridiem.isEmpty())
                endMeridiem = startTime.hour() >= 12 ? u"pm"_s : guessMeridiem(end);
            QDateTime finish(day, end.withMeridiem(endMeridiem), zone);
            if (finish <= draft.start)
                finish = finish.addDays(1);
            draft.end = finish;
        } else {
            draft.end = draft.start.addSecs(60 * (minutes > 0 ? minutes : kDefaultMinutes));
        }
    } else if (date.isValid() && minutes == 0) {
        draft.allDay = true;
        draft.start = QDateTime(date, QTime(0, 0), zone);
        draft.end = QDateTime(date.addDays(1), QTime(0, 0), zone);
    } else {
        // No time given: the next whole hour, or 9:00 on another day.
        QDateTime begin = date.isValid() && date != today
                              ? QDateTime(date, QTime(9, 0), zone)
                              : QDateTime(today, QTime(local.time().hour(), 0), zone).addSecs(3600);
        draft.start = begin;
        draft.end = begin.addSecs(60 * (minutes > 0 ? minutes : kDefaultMinutes));
    }
    return draft;
}

} // namespace callie
