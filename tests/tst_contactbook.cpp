#include "callie/ContactBook.h"
#include "callie/SampleSource.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

namespace {

Event meeting(const QString &uid, const QList<Guest> &guests)
{
    Event e;
    e.uid = e.eventId = uid;
    e.guests = guests;
    return e;
}

QStringList emails(const QVariantList &suggestions)
{
    QStringList list;
    for (const QVariant &s : suggestions)
        list << s.toMap().value(u"email"_s).toString();
    return list;
}

} // namespace

class TestContactBook : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void guestsMetMoreComeFirst();
    void matchesByAddressThenName();
    void contactsKeepTheirNamesAndSurvive();
    void sourceTeachesItsGuests();
    void removedAccountsTakeTheirContacts();
};

void TestContactBook::guestsMetMoreComeFirst()
{
    ContactBook book({});
    const Guest me{u"me@example.com"_s, u"Me"_s, u"accepted"_s, false, true};
    const Guest pat{u"pat@example.com"_s, u"Pat Lee"_s, {}, false, false};
    const Guest paula{u"paula@example.com"_s, u"Paula"_s, {}, false, false};
    // Pat is at two meetings, one of them read twice as two occurrences.
    Event weekly = meeting(u"w-1"_s, {me, pat});
    weekly.seriesId = u"w"_s;
    Event again = weekly;
    again.eventId = u"w-2"_s;
    book.learn({weekly, again, meeting(u"a"_s, {me, pat, paula}), meeting(u"b"_s, {paula})});

    // Pat and Paula match "pa"; tied on meetings, the address decides.
    QCOMPARE(emails(book.suggest(u"pa"_s, {})),
             (QStringList{u"pat@example.com"_s, u"paula@example.com"_s}));
    // The user is never suggested, and those already invited are left out.
    QVERIFY(book.suggest(u"me"_s, {}).isEmpty());
    QCOMPARE(emails(book.suggest(u"pa"_s, {u"PAT@example.com"_s})),
             QStringList{u"paula@example.com"_s});
    QCOMPARE(book.suggest(u"pa"_s, {}, 1).size(), 1);
    QVERIFY(book.suggest(u"  "_s, {}).isEmpty());
}

void TestContactBook::matchesByAddressThenName()
{
    ContactBook book({});
    book.add(u"me@example.com"_s, {{u"Alex Kim"_s, u"akim@example.com"_s},
                                   {u"Kimberly Ross"_s, u"kross@example.com"_s},
                                   {u"Jo"_s, u"jo.kim@example.com"_s}});
    // The address starting with it, then a name word starting with it, then anywhere.
    QCOMPARE(emails(book.suggest(u"kim"_s, {})),
             (QStringList{u"akim@example.com"_s, u"kross@example.com"_s, u"jo.kim@example.com"_s}));
    QCOMPARE(emails(book.suggest(u"k"_s, {})).first(), u"kross@example.com"_s);
    // Every word of a name counts.
    QCOMPARE(emails(book.suggest(u"alex k"_s, {})), QStringList{u"akim@example.com"_s});
    QCOMPARE(book.suggest(u"alex"_s, {}).first().toMap().value(u"name"_s).toString(),
             u"Alex Kim"_s);
}

void TestContactBook::contactsKeepTheirNamesAndSurvive()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"callie/contacts.json"_s);
    {
        ContactBook book(path);
        QSignalSpy changed(&book, &ContactBook::changed);
        book.add(u"me@example.com"_s, {{u"Priya Shah"_s, u"priya@example.com"_s}});
        // An address alone does not wipe out the name known for it.
        book.add(u"me@example.com"_s, {{{}, u"Priya@Example.com"_s}});
        QCOMPARE(changed.size(), 2);
        // A guest list's name gives way to the contact's.
        book.learn({meeting(u"a"_s, {{u"priya@example.com"_s, u"P."_s, {}, false, false}})});
        QCOMPARE(book.suggest(u"pri"_s, {}).first().toMap().value(u"name"_s).toString(),
                 u"Priya Shah"_s);
    }
    ContactBook again(path);
    QCOMPARE(emails(again.suggest(u"shah"_s, {})), QStringList{u"Priya@Example.com"_s});
}

void TestContactBook::sourceTeachesItsGuests()
{
    SampleSource sample;
    ContactBook book({});
    QSignalSpy changed(&book, &ContactBook::changed);
    book.setSource(&sample);
    QTRY_VERIFY(!changed.isEmpty());
    QCOMPARE(emails(book.suggest(u"priya"_s, {})), QStringList{u"priya@example.com"_s});
}

void TestContactBook::removedAccountsTakeTheirContacts()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(u"contacts.json"_s);
    {
        ContactBook book(path);
        book.add(u"work@example.com"_s,
                 {{u"Pat"_s, u"pat@example.com"_s}, {u"Lee"_s, u"lee@example.com"_s}});
        book.add(u"home@example.com"_s, {{u"Lee"_s, u"lee@example.com"_s}});
        book.forget(u"work@example.com"_s);
        // Lee is still a contact of the other account.
        QCOMPARE(emails(book.suggest(u"e"_s, {})), QStringList{u"lee@example.com"_s});
    }
    ContactBook again(path);
    QCOMPARE(emails(again.suggest(u"e"_s, {})), QStringList{u"lee@example.com"_s});
    // Removed while Callie was closed, an account is not among those it starts with.
    again.keepOnly({u"work@example.com"_s});
    QVERIFY(again.suggest(u"e"_s, {}).isEmpty());
}

QTEST_GUILESS_MAIN(TestContactBook)
#include "tst_contactbook.moc"
