#include "callie/InvitesModel.h"

#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

class ListSource : public CalendarSource
{
public:
    QList<Event> events;

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

Event invite(const QString &summary, const QDateTime &start,
             const QString &response = u"needsAction"_s)
{
    Event e;
    e.uid = summary;
    e.eventId = summary + start.toString(u"dd"_s);
    e.summary = summary;
    e.calendarId = u"work"_s;
    e.start = start;
    e.end = start.addSecs(3600);
    e.responseStatus = response;
    e.canRespond = true;
    return e;
}

} // namespace

class TestInvitesModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listsWaitingInvitesSoonestFirst();
    void answeringOneDropsIt();
};

void TestInvitesModel::listsWaitingInvitesSoonestFirst()
{
    ListSource source;
    Event weekly1 = invite(u"Weekly"_s, at(9, 10));
    weekly1.seriesId = u"weekly"_s;
    Event weekly2 = invite(u"Weekly"_s, at(16, 10));
    weekly2.seriesId = u"weekly"_s;
    Event hidden = invite(u"Hidden"_s, at(8, 9));
    hidden.calendarId = u"other"_s;
    Event mine = invite(u"Mine"_s, at(8, 11));
    mine.canRespond = false;
    source.events = {weekly2,
                     invite(u"Review"_s, at(8, 15)),
                     weekly1,
                     invite(u"Going"_s, at(8, 12), u"accepted"_s),
                     invite(u"Over"_s, at(7, 8)),
                     hidden,
                     mine};

    InvitesModel model;
    model.setNow(at(7, 12));
    model.setHiddenCalendars({u"other"_s});
    model.setSource(&source);

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.data(model.index(0), InvitesModel::SummaryRole).toString(), u"Review"_s);
    // A series is listed once, by its next occurrence.
    QCOMPARE(model.data(model.index(1), InvitesModel::SummaryRole).toString(), u"Weekly"_s);
    QCOMPARE(model.data(model.index(1), InvitesModel::StartRole).toDateTime(), at(9, 10));
    QVERIFY(model.data(model.index(1), InvitesModel::RepeatsRole).toBool());
    QCOMPARE(model.inviteAt(1).value(u"seriesId"_s).toString(), u"weekly"_s);

    // Once the review has passed it drops out, without a fresh read.
    model.setNow(at(8, 16));
    QCOMPARE(model.count(), 1);
}

void TestInvitesModel::answeringOneDropsIt()
{
    ListSource source;
    source.events = {invite(u"Review"_s, at(8, 15))};
    InvitesModel model;
    model.setNow(at(7, 12));
    model.setSource(&source);
    QCOMPARE(model.count(), 1);
    QSignalSpy counted(&model, &InvitesModel::countChanged);

    source.events.first().responseStatus = u"accepted"_s;
    Q_EMIT source.changed();
    QCOMPARE(model.count(), 0);
    QCOMPARE(counted.size(), 1);
}

QTEST_GUILESS_MAIN(TestInvitesModel)
#include "tst_invitesmodel.moc"
