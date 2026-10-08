#pragma once

#include "CalendarSource.h"
#include "Contact.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

namespace callie {

/// The people the user may invite, for suggestions while typing a guest:
/// everyone on their events' guest lists, and what Google knows of their
/// contacts and their organization's directory. People met more often come
/// first.
class ContactBook : public QObject
{
    Q_OBJECT

public:
    /// Keeps Google's contacts in the JSON file at `path`, so they are there
    /// before the first sync of a run; an empty path keeps them in memory.
    explicit ContactBook(QString path, QObject *parent = nullptr);

    /// `$XDG_CACHE_HOME/callie/contacts.json`
    [[nodiscard]] static QString defaultPath();

    /// Learns the guests of the source's events, again whenever they change.
    void setSource(CalendarSource *source);

    /// Sets what Google lists for an account, replacing what it listed before,
    /// so people deleted there are no longer suggested.
    void setContacts(const QString &account, const QList<Contact> &contacts);
    /// Drops the contacts only `account` brought, once it is removed.
    void forget(const QString &account);
    /// Drops the contacts of every account not in `accounts`, as for accounts
    /// removed while Callie was not running.
    void keepOnly(const QStringList &accounts);
    /// Learns the guests of `events`, replacing what earlier events taught.
    void learn(const QList<Event> &events);

    /// Up to `limit` people whose name or address matches `text`, as
    /// {name, email}, leaving out the addresses in `exclude`.
    Q_INVOKABLE QVariantList suggest(const QString &text, const QStringList &exclude,
                                     int limit = 8) const;

Q_SIGNALS:
    void changed();

private:
    void readSource();
    /// Takes `account` off every contact, and drops those no account has.
    void drop(const QString &account);
    void save() const;

    QString m_path;
    /// By lowercase address.
    QHash<QString, Contact> m_saved;
    /// The accounts each saved contact came from.
    QHash<QString, QSet<QString>> m_owners;
    QHash<QString, Contact> m_met;
    QHash<QString, int> m_times;
    QPointer<CalendarSource> m_source;
    QTimer m_reread;
    QFutureWatcher<SourceSnapshot> m_reading;
};

} // namespace callie
