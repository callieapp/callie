#include "callie/ContactBook.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

constexpr int kDaysAround = 365;

// How well `contact` matches `words`: 3 when the address starts with the
// text, 2 when every word starts a word of the name, 1 when the text is
// somewhere in either, 0 when it is not.
int matchOf(const Contact &contact, const QString &text, const QStringList &words)
{
    const QString email = contact.email.toLower();
    if (email.startsWith(text))
        return 3;
    const QStringList nameWords = contact.name.toLower().split(u' ', Qt::SkipEmptyParts);
    const bool named =
        !words.isEmpty() && std::all_of(words.cbegin(), words.cend(), [&](const QString &w) {
            return std::any_of(nameWords.cbegin(), nameWords.cend(),
                               [&w](const QString &n) { return n.startsWith(w); });
        });
    if (named)
        return 2;
    return email.contains(text) || contact.name.toLower().contains(text) ? 1 : 0;
}

} // namespace

ContactBook::ContactBook(QString path, QObject *parent) : QObject(parent), m_path(std::move(path))
{
    m_reread.setSingleShot(true);
    m_reread.setInterval(2000);
    connect(&m_reread, &QTimer::timeout, this, &ContactBook::readSource);
    connect(&m_reading, &QFutureWatcher<SourceSnapshot>::finished, this, [this] {
        if (m_reading.future().resultCount() > 0)
            learn(m_reading.result().events);
    });
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &person : QJsonDocument::fromJson(file.readAll()).array()) {
        const Contact contact{person[u"name"].toString(), person[u"email"].toString()};
        const QStringList accounts = person[u"accounts"].toVariant().toStringList();
        if (contact.email.isEmpty() || accounts.isEmpty())
            continue;
        m_saved.insert(contact.email.toLower(), contact);
        m_owners.insert(contact.email.toLower(), {accounts.cbegin(), accounts.cend()});
    }
}

QString ContactBook::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
           u"/callie/contacts.json"_s;
}

void ContactBook::setSource(CalendarSource *source)
{
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (!m_source)
        return;
    // A sync changes events in bursts, so they are read once it settles.
    connect(m_source, &CalendarSource::changed, &m_reread, qOverload<>(&QTimer::start));
    readSource();
}

void ContactBook::readSource()
{
    if (!m_source || m_reading.isRunning())
        return;
    const QDateTime now = QDateTime::currentDateTime();
    m_reading.setFuture(m_source->load(now.addDays(-kDaysAround), now.addDays(kDaysAround),
                                       QTimeZone::systemTimeZone()));
}

void ContactBook::add(const QString &account, const QList<Contact> &contacts)
{
    for (const Contact &contact : contacts) {
        const QString key = contact.email.toLower();
        if (key.isEmpty())
            continue;
        Contact &kept = m_saved[key];
        kept.email = contact.email;
        if (!contact.name.isEmpty())
            kept.name = contact.name;
        m_owners[key].insert(account);
    }
    save();
    Q_EMIT changed();
}

void ContactBook::keepOnly(const QStringList &accounts)
{
    QSet<QString> gone;
    for (const QSet<QString> &owners : std::as_const(m_owners)) {
        for (const QString &owner : owners) {
            if (!accounts.contains(owner))
                gone.insert(owner);
        }
    }
    for (const QString &account : std::as_const(gone))
        forget(account);
}

void ContactBook::forget(const QString &account)
{
    for (auto it = m_owners.begin(); it != m_owners.end();) {
        it->remove(account);
        if (it->isEmpty()) {
            m_saved.remove(it.key());
            it = m_owners.erase(it);
        } else {
            ++it;
        }
    }
    save();
    Q_EMIT changed();
}

void ContactBook::learn(const QList<Event> &events)
{
    m_met.clear();
    m_times.clear();
    // Each event once, however many of its occurrences were read.
    QSet<QString> seen;
    for (const Event &event : events) {
        const QString key = event.seriesId.isEmpty() ? event.uid : event.seriesId;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        for (const Guest &guest : event.guests) {
            const QString email = guest.email.toLower();
            if (guest.self || email.isEmpty())
                continue;
            ++m_times[email];
            Contact &known = m_met[email];
            known.email = guest.email;
            if (!guest.name.isEmpty())
                known.name = guest.name;
        }
    }
    Q_EMIT changed();
}

QVariantList ContactBook::suggest(const QString &text, const QStringList &exclude, int limit) const
{
    const QString wanted = text.trimmed().toLower();
    if (wanted.isEmpty())
        return {};
    const QStringList words = wanted.split(u' ', Qt::SkipEmptyParts);
    QSet<QString> left;
    for (const QString &email : exclude)
        left.insert(email.trimmed().toLower());

    struct Match
    {
        Contact contact;
        int match;
        int times;
    };
    QList<Match> matches;
    const auto consider = [&](const QString &key, Contact contact) {
        if (left.contains(key))
            return;
        // A name from the contacts beats one only seen on an invitation.
        if (const auto saved = m_saved.constFind(key);
            saved != m_saved.cend() && !saved->name.isEmpty())
            contact.name = saved->name;
        if (const int match = matchOf(contact, wanted, words); match > 0)
            matches.append({contact, match, m_times.value(key)});
        left.insert(key);
    };
    for (auto it = m_met.cbegin(); it != m_met.cend(); ++it)
        consider(it.key(), it.value());
    for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it)
        consider(it.key(), it.value());

    std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
        if (a.match != b.match)
            return a.match > b.match;
        if (a.times != b.times)
            return a.times > b.times;
        return a.contact.email < b.contact.email;
    });
    QVariantList list;
    for (const Match &m : std::as_const(matches)) {
        if (list.size() == limit)
            break;
        list.append(QVariantMap{{u"name"_s, m.contact.name}, {u"email"_s, m.contact.email}});
    }
    return list;
}

void ContactBook::save() const
{
    if (m_path.isEmpty())
        return;
    QJsonArray people;
    for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it) {
        const QSet<QString> &owners = m_owners.value(it.key());
        people.append(QJsonObject{{u"name"_s, it->name},
                                  {u"email"_s, it->email},
                                  {u"accounts"_s, QJsonArray::fromStringList(QStringList(
                                                      owners.cbegin(), owners.cend()))}});
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(people).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

} // namespace callie
