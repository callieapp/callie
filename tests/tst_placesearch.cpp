#include "FakeHttpServer.h"
#include "PlaceSearch.h"
#include "callie/SampleSource.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

QStringList labels(const QVariantList &results)
{
    QStringList list;
    for (const QVariant &r : results)
        list << r.toMap().value(u"label"_s).toString() +
                    (r.toMap().value(u"past"_s).toBool() ? u" (past)"_s : QString());
    return list;
}

// Photon's answer: GeoJSON features with the place's parts as properties.
QByteArray photon(const QList<QVariantMap> &places)
{
    QJsonArray features;
    for (const QVariantMap &place : places)
        features.append(QJsonObject{{u"type"_s, u"Feature"_s},
                                    {u"properties"_s, QJsonObject::fromVariantMap(place)}});
    return QJsonDocument(
               QJsonObject{{u"type"_s, u"FeatureCollection"_s}, {u"features"_s, features}})
        .toJson(QJsonDocument::Compact);
}

} // namespace

class TestPlaceSearch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ownPlacesComeFirst();
    void shortTextStaysLocal();
    void typingOnAsksOnce();
    void offlineAsksNoServer();
};

void TestPlaceSearch::ownPlacesComeFirst()
{
    SampleSource source;
    FakeHttpServer server;
    server.respond(200,
                   photon({
                       {{u"name"_s, u"Studio Rosa"_s},
                        {u"street"_s, u"Main St"_s},
                        {u"housenumber"_s, u"12"_s},
                        {u"city"_s, u"Leeds"_s},
                        {u"country"_s, u"United Kingdom"_s}},
                       // The user's own place, when OpenStreetMap knows it too, is listed once.
                       {{u"name"_s, u"Studio"_s}},
                       {{u"city"_s, u"Studiopolis"_s}, {u"country"_s, u"Nowhere"_s}},
                   }));
    PlaceSearch search;
    search.setSource(&source);
    search.setServer(server.url(u"/"_s));
    search.setDelay(0);

    search.search(u" stu"_s);
    // The user's own first, then the server's.
    QTRY_COMPARE(
        labels(search.results()),
        (QStringList{u"Studio (past)"_s, u"Studio Rosa, 12 Main St, Leeds, United Kingdom"_s,
                     u"Studiopolis, Nowhere"_s}));

    QCOMPARE(server.requests.size(), 1);
    const FakeHttpServer::Request request = server.requests.first();
    const QUrl asked(QString::fromUtf8(request.target));
    QCOMPARE(asked.path(), u"/api/"_s);
    QCOMPARE(QUrlQuery(asked).queryItemValue(u"q"_s), u"stu"_s);
    QVERIFY(request.headers.value("user-agent").startsWith("Callie/"));

    search.clear();
    QVERIFY(search.results().isEmpty());
}

void TestPlaceSearch::shortTextStaysLocal()
{
    SampleSource source;
    FakeHttpServer server;
    PlaceSearch search;
    search.setSource(&source);
    search.setServer(server.url(u"/"_s));
    search.setDelay(0);

    search.search(u"cl"_s);
    QTRY_COMPARE(labels(search.results()), QStringList{u"Clinic (past)"_s});
    QTest::qWait(100);
    QVERIFY(server.requests.isEmpty());

    // A place first used since is offered too, after those used more.
    EventDraft draft;
    draft.calendarId = source.calendars().first().id;
    draft.summary = u"Climb"_s;
    draft.location = u"Climbing wall"_s;
    draft.start = QDateTime::currentDateTime();
    draft.end = draft.start.addSecs(3600);
    source.createEvent(draft, [](const QString &) {});
    search.search(u"cl"_s);
    QTRY_COMPARE(labels(search.results()),
                 (QStringList{u"Clinic (past)"_s, u"Climbing wall (past)"_s}));
}

void TestPlaceSearch::typingOnAsksOnce()
{
    FakeHttpServer server;
    server.handler = [](const FakeHttpServer::Request &request) {
        const QString q = QUrlQuery(QUrl(QString::fromUtf8(request.target))).queryItemValue(u"q"_s);
        return FakeHttpServer::Response(200, photon({{{u"name"_s, q + u" place"_s}}}));
    };
    PlaceSearch search;
    search.setServer(server.url(u"/"_s));
    search.setDelay(0);
    QSignalSpy changed(&search, &PlaceSearch::resultsChanged);

    search.search(u"park"_s);
    search.search(u"parkway"_s);
    QVERIFY(changed.wait());
    QTest::qWait(100);
    QCOMPARE(labels(search.results()), QStringList{u"parkway place"_s});
    QCOMPARE(server.requests.size(), 1);
}

void TestPlaceSearch::offlineAsksNoServer()
{
    SampleSource source;
    FakeHttpServer server;
    PlaceSearch search;
    search.setSource(&source);
    search.setDelay(0);

    // With online search off, only the user's own places come.
    search.search(u"studio"_s);
    QTRY_COMPARE(labels(search.results()), QStringList{u"Studio (past)"_s});
    QTest::qWait(100);
    QVERIFY(server.requests.isEmpty());

    // Turned on, the server is asked from the next search.
    search.setServer(server.url(u"/"_s));
    QTest::qWait(100);
    QVERIFY(server.requests.isEmpty());
    search.search(u"studio"_s);
    QTRY_COMPARE(server.requests.size(), 1);

    // And off again, it is not, even for a search already waiting.
    search.setDelay(200);
    search.search(u"studios"_s);
    search.setServer({});
    QTest::qWait(400);
    QCOMPARE(server.requests.size(), 1);
}

QTEST_GUILESS_MAIN(TestPlaceSearch)
#include "tst_placesearch.moc"
