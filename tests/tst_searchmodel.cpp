#include "callie/SearchModel.h"

#include <QCoreApplication>
#include <QPromise>
#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

class ListSource : public CalendarSource
{
public:
    QList<Event> events;
    /// Reads wait here until answered, as a disk read in the background would.
    bool slow = false;
    mutable QList<std::shared_ptr<QPromise<SourceSnapshot>>> waiting;
    mutable int reads = 0;

    QFuture<SourceSnapshot> load(const QDateTime &from, const QDateTime &to,
                                 const QTimeZone &tz) const override
    {
        ++reads;
        if (!slow)
            return CalendarSource::load(from, to, tz);
        auto promise = std::make_shared<QPromise<SourceSnapshot>>();
        promise->start();
        waiting.append(promise);
        return promise->future();
    }
    void answer(int index) const
    {
        auto promise = waiting.at(index);
        promise->addResult(SourceSnapshot{events, {}});
        promise->finish();
        QCoreApplication::processEvents();
    }

    QString sourceId() const override { return u"list"_s; }
    QList<CalendarInfo> calendars() const override { return {}; }
    QList<Event> eventsBetween(const QDateTime &from, const QDateTime &to,
                               const QTimeZone &) const override
    {
        QList<Event> result;
        for (const Event &event : events) {
            if (event.start < to && event.end > from)
                result.append(event);
        }
        return result;
    }
    void refresh() override {}
};

QDateTime at(int day, int hour)
{
    return QDateTime(QDate(2026, 10, day), QTime(hour, 0), QTimeZone::UTC);
}

Event makeEvent(const QString &uid, const QString &summary, const QDateTime &start)
{
    Event e;
    e.uid = uid;
    e.summary = summary;
    e.calendarId = u"work"_s;
    e.start = start;
    e.end = start.addSecs(3600);
    return e;
}

QStringList summaries(const SearchModel &model)
{
    QStringList list;
    for (int row = 0; row < model.rowCount(); ++row)
        list << model.data(model.index(row), SearchModel::SummaryRole).toString() + u'@' +
                    QString::number(model.data(model.index(row), SearchModel::StartRole)
                                        .toDateTime()
                                        .date()
                                        .day());
    return list;
}

} // namespace

class TestSearchModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyWordMustMatchSomewhere();
    void eachEventOnceUpcomingFirst();
    void hiddenCalendarsAndEmptyQueries();
    void changesAreSearchedAfresh();
    void slowReadsAndStaleAnswers();
    void ticksKeepTheResults();
};

void TestSearchModel::everyWordMustMatchSomewhere()
{
    Event lunch = makeEvent(u"lunch"_s, u"Lunch"_s, at(8, 12));
    lunch.location = u"Cafe Sol"_s;
    lunch.guests = {{u"alex@example.com"_s, u"Alex Kim"_s, u"accepted"_s, false, false}};
    QVERIFY(SearchModel::matches(lunch, {u"lunch"_s}));
    QVERIFY(SearchModel::matches(lunch, {u"cafe"_s, u"KIM"_s}));
    QVERIFY(SearchModel::matches(lunch, {u"alex@"_s}));
    QVERIFY(!SearchModel::matches(lunch, {u"lunch"_s, u"dinner"_s}));
}

void TestSearchModel::eachEventOnceUpcomingFirst()
{
    ListSource source;
    source.events = {makeEvent(u"standup"_s, u"Standup"_s, at(5, 9)),
                     makeEvent(u"standup"_s, u"Standup"_s, at(8, 9)),
                     makeEvent(u"standup"_s, u"Standup"_s, at(9, 9)),
                     makeEvent(u"retro"_s, u"Standup retro"_s, at(2, 11)),
                     makeEvent(u"old"_s, u"Old standup"_s, at(1, 11)),
                     makeEvent(u"review"_s, u"Review"_s, at(8, 15))};
    SearchModel model;
    model.setNow(at(7, 12));
    model.setSource(&source);
    model.setQuery(u"standup"_s);

    // The next standup only, then past matches with the latest first.
    QCOMPARE(summaries(model),
             (QStringList{u"Standup@8"_s, u"Standup retro@2"_s, u"Old standup@1"_s}));
    QVERIFY(model.data(model.index(0), SearchModel::UpcomingRole).toBool());
    QVERIFY(!model.data(model.index(1), SearchModel::UpcomingRole).toBool());
}

void TestSearchModel::hiddenCalendarsAndEmptyQueries()
{
    ListSource source;
    Event hidden = makeEvent(u"h"_s, u"Planning"_s, at(8, 10));
    hidden.calendarId = u"other"_s;
    source.events = {makeEvent(u"p"_s, u"Planning"_s, at(9, 10)), hidden};
    SearchModel model;
    model.setNow(at(7, 12));
    model.setSource(&source);
    model.setHiddenCalendars({u"other"_s});
    model.setQuery(u"plan"_s);
    QCOMPARE(model.count(), 1);
    model.setQuery(u"   "_s);
    QCOMPARE(model.count(), 0);
}

void TestSearchModel::changesAreSearchedAfresh()
{
    ListSource source;
    source.events = {makeEvent(u"p"_s, u"Planning"_s, at(9, 10))};
    SearchModel model;
    model.setNow(at(7, 12));
    model.setSource(&source);
    model.setQuery(u"plan"_s);
    QCOMPARE(model.count(), 1);

    source.events.append(makeEvent(u"q"_s, u"Planning 2"_s, at(10, 10)));
    Q_EMIT source.changed();
    QCOMPARE(model.count(), 2);
}

void TestSearchModel::slowReadsAndStaleAnswers()
{
    ListSource source;
    source.slow = true;
    source.events = {makeEvent(u"p"_s, u"Planning"_s, at(9, 10))};
    SearchModel model;
    model.setNow(at(7, 12));
    model.setSource(&source);
    model.setQuery(u"plan"_s);
    QVERIFY(model.busy());
    QCOMPARE(model.count(), 0);

    // A change while reading asks for a fresh read once this one lands.
    source.events.append(makeEvent(u"q"_s, u"Planning 2"_s, at(10, 10)));
    Q_EMIT source.changed();
    source.answer(0);
    QCOMPARE(source.reads, 2);
    source.answer(1);
    QVERIFY(!model.busy());
    QCOMPARE(model.count(), 2);

    // Ten minutes on, a new search reads again.
    model.setNow(at(7, 12).addSecs(10 * 60));
    model.setQuery(u"planning"_s);
    QCOMPARE(source.reads, 3);
}

void TestSearchModel::ticksKeepTheResults()
{
    ListSource source;
    source.events = {makeEvent(u"p"_s, u"Planning"_s, at(9, 10))};
    SearchModel model;
    model.setNow(at(7, 12));
    model.setSource(&source);
    model.setQuery(u"plan"_s);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    model.setNow(at(7, 12).addSecs(30));
    QCOMPARE(reset.size(), 0);
    // Once it is past, it moves to the past results.
    model.setNow(at(9, 12));
    QCOMPARE(reset.size(), 1);
    QVERIFY(!model.data(model.index(0), SearchModel::UpcomingRole).toBool());
}

QTEST_GUILESS_MAIN(TestSearchModel)
#include "tst_searchmodel.moc"
