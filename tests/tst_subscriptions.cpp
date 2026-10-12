#include "FakeHttpServer.h"

#include "callie/Subscriptions.h"

#include <QDir>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestSubscriptions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void linksBecomeFeeds();
    void fetchesAndNamesTheFeed();
    void unchangedFeedsAreNotDownloadedAgain();
    void badLinksSayWhy();
    void removingForgetsTheCopy();
};

namespace {

QByteArray feed(const QByteArray &events)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//t//EN\r\nX-WR-CALNAME:Holidays\\, "
           "here\r\n" +
           events + "END:VCALENDAR\r\n";
}

const QByteArray kTwo = feed("BEGIN:VEVENT\r\nUID:a\r\nDTSTART;VALUE=DATE:20261012\r\n"
                             "SUMMARY:Columbus Day\r\nEND:VEVENT\r\n"
                             "BEGIN:VEVENT\r\nUID:b\r\nDTSTART;VALUE=DATE:20261111\r\n"
                             "SUMMARY:Veterans Day\r\nEND:VEVENT\r\n");
const QByteArray kOne = feed("BEGIN:VEVENT\r\nUID:a\r\nDTSTART;VALUE=DATE:20261012\r\n"
                             "SUMMARY:Columbus Day\r\nEND:VEVENT\r\n");

QList<Event> autumn(const CalendarSource &source)
{
    const QDateTime from(QDate(2026, 10, 1), QTime(0, 0), QTimeZone::UTC);
    return source.eventsBetween(from, from.addMonths(2), QTimeZone::UTC);
}

} // namespace

void TestSubscriptions::linksBecomeFeeds()
{
    QCOMPARE(Subscriptions::feedUrl(u"webcal://example.com/h.ics"_s),
             QUrl(u"https://example.com/h.ics"_s));
    QCOMPARE(Subscriptions::feedUrl(u" http://example.com/h.ics "_s),
             QUrl(u"http://example.com/h.ics"_s));
    QVERIFY(!Subscriptions::feedUrl(u"file:///etc/passwd"_s).isValid());
    QVERIFY(!Subscriptions::feedUrl(u"holidays"_s).isValid());
}

void TestSubscriptions::fetchesAndNamesTheFeed()
{
    QTemporaryDir dir;
    FakeHttpServer server;
    server.replier = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Reply{200, kTwo, {{"ETag", "\"v1\""}}, "text/calendar"};
    };
    QNetworkAccessManager network;
    Subscriptions subscriptions(dir.path(), &network);
    QSignalSpy fetched(&subscriptions, &Subscriptions::fetched);
    QString error;
    const QString id = subscriptions.add(server.url(u"/h.ics"_s).toString(), {}, &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY(subscriptions.syncing());
    QVERIFY(fetched.wait());

    const QList<CalendarInfo> calendars = subscriptions.calendars();
    QCOMPARE(calendars.size(), 1);
    QCOMPARE(calendars.first().displayName, u"Holidays, here"_s);
    QVERIFY(!calendars.first().writable);
    QCOMPARE(calendars.first().account, u"subscriptions"_s);
    QCOMPARE(autumn(subscriptions).size(), 2);
    QVERIFY(!autumn(subscriptions).first().canEdit);
    QVERIFY(subscriptions.lastSynced().isValid());
    QVERIFY(subscriptions.lastError().isEmpty());

    // The same link again is the same subscription.
    QCOMPARE(subscriptions.add(server.url(u"/h.ics"_s).toString(), {}, &error), id);
}

void TestSubscriptions::unchangedFeedsAreNotDownloadedAgain()
{
    QTemporaryDir dir;
    FakeHttpServer server;
    QByteArray body = kTwo;
    server.replier = [&body](const FakeHttpServer::Request &request) {
        if (request.headers.value("if-none-match") == "\"v1\"" && body == kTwo)
            return FakeHttpServer::Reply{304, {}, {}, "text/calendar"};
        return FakeHttpServer::Reply{200, body, {{"ETag", "\"v1\""}}, "text/calendar"};
    };
    QNetworkAccessManager network;
    Subscriptions subscriptions(dir.path(), &network);
    QSignalSpy fetched(&subscriptions, &Subscriptions::fetched);
    QString error;
    subscriptions.add(server.url(u"/h.ics"_s).toString(), u"Days off"_s, &error);
    QVERIFY(fetched.wait());

    // Within the hour, a refresh leaves it be.
    subscriptions.refresh();
    QVERIFY(!subscriptions.syncing());
    QCOMPARE(server.requests.size(), 1);

    subscriptions.fetchAll();
    QVERIFY(fetched.wait());
    QCOMPARE(server.requests.size(), 2);
    QCOMPARE(autumn(subscriptions).size(), 2);

    // An event gone from the feed goes from the copy, and a name the user
    // gave stays.
    body = kOne;
    subscriptions.fetchAll();
    QVERIFY(fetched.wait());
    QCOMPARE(autumn(subscriptions).size(), 1);
    QCOMPARE(subscriptions.calendars().first().displayName, u"Days off"_s);
}

void TestSubscriptions::badLinksSayWhy()
{
    QTemporaryDir dir;
    FakeHttpServer server;
    server.replier = [](const FakeHttpServer::Request &request) {
        if (request.target.startsWith("/missing.ics"))
            return FakeHttpServer::Reply{404, "gone", {}, "text/plain"};
        return FakeHttpServer::Reply{200, "<html>a web page</html>", {}, "text/html"};
    };
    QNetworkAccessManager network;
    Subscriptions subscriptions(dir.path(), &network);
    QSignalSpy fetched(&subscriptions, &Subscriptions::fetched);
    QString error;
    QVERIFY(subscriptions.add(u"ftp://example.com/x.ics"_s, {}, &error).isEmpty());
    QVERIFY(!error.isEmpty());

    // A private link's secret stays out of what is kept and shown.
    subscriptions.add(server.url(u"/missing.ics"_s).toString() + u"?token=s3cret"_s, u"Missing"_s,
                      &error);
    QVERIFY(fetched.wait());
    QVERIFY(subscriptions.lastError().startsWith(u"Missing: "_s));
    QVERIFY(subscriptions.lastError().contains(u"404"_s));
    QVERIFY(!subscriptions.lastError().contains(u"s3cret"_s));
    QVERIFY(!subscriptions.lastError().contains(u"missing.ics"_s));
    subscriptions.add(server.url(u"/page"_s).toString(), u"Page"_s, &error);
    QVERIFY(fetched.wait());
    QCOMPARE(subscriptions.syncReport().size(), 2);
    QVERIFY(subscriptions.syncReport()
                .last()
                .toMap()
                .value(u"error"_s)
                .toString()
                .contains(u"calendar"_s));
}

void TestSubscriptions::removingForgetsTheCopy()
{
    QTemporaryDir dir;
    FakeHttpServer server;
    server.replier = [](const FakeHttpServer::Request &) {
        return FakeHttpServer::Reply{200, kTwo, {}, "text/calendar"};
    };
    QNetworkAccessManager network;
    Subscriptions subscriptions(dir.path(), &network);
    QSignalSpy fetched(&subscriptions, &Subscriptions::fetched);
    QString error;
    const QString id = subscriptions.add(server.url(u"/h.ics"_s).toString(), {}, &error);
    QVERIFY(fetched.wait());
    QVERIFY(subscriptions.remove(id, &error));
    QVERIFY(subscriptions.calendars().isEmpty());
    QVERIFY(QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());

    // Stopped while its first fetch is under way, it does not come back.
    subscriptions.add(server.url(u"/h.ics"_s).toString(), u"Again"_s, &error);
    QVERIFY(subscriptions.syncing());
    QVERIFY(subscriptions.remove(u"subscription:again"_s, &error));
    QVERIFY(fetched.wait());
    QVERIFY(QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    subscriptions.refresh();
    QVERIFY(subscriptions.calendars().isEmpty());
}

QTEST_MAIN(TestSubscriptions)
#include "tst_subscriptions.moc"
