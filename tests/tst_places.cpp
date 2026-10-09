#include "callie/Event.h"
#include "callie/Places.h"

#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

class TestPlaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void callLinksAreFound_data();
    void callLinksAreFound();
    void mapLinksFollowTheApp();
    void joinPrefersTheProvidersCall();
};

void TestPlaces::callLinksAreFound_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QUrl>("link");
    QTest::newRow("zoom") << u"Dial in: https://acme.zoom.us/j/123456?pwd=abc."_s
                          << QUrl(u"https://acme.zoom.us/j/123456?pwd=abc"_s);
    QTest::newRow("meet") << u"(https://meet.google.com/abc-defg-hij)"_s
                          << QUrl(u"https://meet.google.com/abc-defg-hij"_s);
    QTest::newRow("teams") << u"https://teams.microsoft.com/l/meetup-join/19%3a1/0"_s
                           << QUrl(u"https://teams.microsoft.com/l/meetup-join/19%3a1/0"_s);
    QTest::newRow("webex") << u"https://acme.webex.com/meet/pat"_s
                           << QUrl(u"https://acme.webex.com/meet/pat"_s);
    QTest::newRow("first of two")
        << u"https://example.com then https://zoom.us/j/1 https://zoom.us/j/2"_s
        << QUrl(u"https://zoom.us/j/1"_s);
    QTest::newRow("not a call") << u"https://example.com/zoom and zoom.us/j/1"_s << QUrl();
    QTest::newRow("zoom elsewhere") << u"https://zoom.us/pricing"_s << QUrl();
}

void TestPlaces::callLinksAreFound()
{
    QFETCH(QString, text);
    QFETCH(QUrl, link);
    QCOMPARE(places::findCallLink(text), link);
}

void TestPlaces::mapLinksFollowTheApp()
{
    const QString place = u"Café Rosa, 12 Main St & 3rd"_s;
    const QUrl system = places::mapUrl(u"system"_s, place);
    QCOMPARE(system.scheme(), u"geo"_s);
    QCOMPARE(QUrlQuery(system).queryItemValue(u"q"_s, QUrl::FullyDecoded), place);

    const QUrl google = places::mapUrl(u"google"_s, place);
    QCOMPARE(google.host(), u"www.google.com"_s);
    QCOMPARE(QUrlQuery(google).queryItemValue(u"query"_s, QUrl::FullyDecoded), place);
    QCOMPARE(
        QUrlQuery(places::mapUrl(u"osm"_s, place)).queryItemValue(u"query"_s, QUrl::FullyDecoded),
        place);
    QCOMPARE(
        QUrlQuery(places::mapUrl(u"apple"_s, place)).queryItemValue(u"q"_s, QUrl::FullyDecoded),
        place);

    // A web address is a place already.
    QCOMPARE(places::mapUrl(u"google"_s, u" https://example.com/venue "_s),
             QUrl(u"https://example.com/venue"_s));
}

void TestPlaces::joinPrefersTheProvidersCall()
{
    Event e;
    e.description = u"Agenda first.\nJoin: https://example.zoom.us/j/987?pwd=x."_s;
    QCOMPARE(e.joinUrl(), QUrl(u"https://example.zoom.us/j/987?pwd=x"_s));
    e.location = u"https://meet.google.com/aaa-bbbb-ccc"_s;
    QCOMPARE(e.joinUrl(), QUrl(u"https://meet.google.com/aaa-bbbb-ccc"_s));
    e.conferenceUrl = QUrl(u"https://meet.google.com/own-call"_s);
    QCOMPARE(e.joinUrl(), e.conferenceUrl);

    Event other;
    other.description = u"https://example.com/zoom"_s;
    QVERIFY(other.joinUrl().isEmpty());
}

QTEST_GUILESS_MAIN(TestPlaces)
#include "tst_places.moc"
