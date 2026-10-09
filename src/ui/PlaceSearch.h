#pragma once

#include "callie/CalendarSource.h"

#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

namespace callie {

/// Places to suggest while typing an event's place: those of the user's own
/// events first, then matches from OpenStreetMap through a Photon server,
/// which is sent what was typed once it is three letters or more.
class PlaceSearch : public QObject
{
    Q_OBJECT
    Q_PROPERTY(callie::CalendarSource *source READ source WRITE setSource NOTIFY sourceChanged)
    /// {label, past}: past for a place from the user's own events.
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)

public:
    explicit PlaceSearch(QObject *parent = nullptr);

    /// The Photon server to ask, ending with a slash; without one, only the
    /// user's own places are suggested.
    void setServer(const QUrl &url);
    /// OpenStreetMap's public Photon server.
    static QUrl publicServer() { return QUrl(QStringLiteral("https://photon.komoot.io/")); }
    /// How long typing must pause before the server is asked, in ms.
    void setDelay(int ms) { m_delay.setInterval(ms); }

    [[nodiscard]] CalendarSource *source() const { return m_source; }
    void setSource(CalendarSource *source);
    [[nodiscard]] QVariantList results() const { return m_results; }

    /// Suggests places for `text`: the user's own at once, the server's after a pause.
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void clear();

    /// At most this many of each kind.
    static constexpr int kLimit = 5;

Q_SIGNALS:
    void sourceChanged();
    void resultsChanged();

private:
    void ask();
    void matchPast();
    void show();

    QPointer<CalendarSource> m_source;
    /// The places of the user's events, most used first.
    QStringList m_past;
    bool m_pastRead = false;
    QFutureWatcher<SourceSnapshot> m_reading;
    QString m_text;
    QStringList m_pastMatches;
    QStringList m_remote;
    QVariantList m_results;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QUrl m_server;
    QTimer m_delay;
};

} // namespace callie
