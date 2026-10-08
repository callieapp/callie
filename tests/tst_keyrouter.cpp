#include "KeyRouter.h"

#include "callie/Settings.h"

#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestKeyRouter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void singleKeysAct();
    void gStartsATwoKeyCommand();
    void leaderTakesTheNextKey();
    void leaderGivesUpAfterItsTimeout();
    void offLetsKeysThrough();
    void everyKeyMeansOneThing();

private:
    std::unique_ptr<KeyRouter> m_router;
    std::unique_ptr<QSignalSpy> m_triggered;

    QStringList triggered() const
    {
        QStringList ids;
        for (const QList<QVariant> &call : *m_triggered)
            ids << call.first().toString();
        return ids;
    }
};

void TestKeyRouter::init()
{
    m_router = std::make_unique<KeyRouter>();
    m_router->setProperty("viMode", true);
    m_triggered = std::make_unique<QSignalSpy>(m_router.get(), &KeyRouter::triggered);
}

void TestKeyRouter::singleKeysAct()
{
    QVERIFY(m_router->press(u"j"_s));
    QVERIFY(m_router->press(u"G"_s));
    QVERIFY(m_router->press(u"?"_s));
    QVERIFY(!m_router->press(u"x"_s));
    QCOMPARE(triggered(), (QStringList{u"scrollDown"_s, u"bottom"_s, u"help"_s}));
}

void TestKeyRouter::gStartsATwoKeyCommand()
{
    QVERIFY(m_router->press(u"g"_s));
    QCOMPARE(m_router->pending(), u"g"_s);
    QVERIFY(m_router->press(u"g"_s));
    QCOMPARE(m_router->pending(), QString());
    // A g that leads nowhere lets the next key act on its own.
    m_router->press(u"g"_s);
    QVERIFY(m_router->press(u"k"_s));
    QCOMPARE(triggered(), (QStringList{u"top"_s, u"scrollUp"_s}));
}

void TestKeyRouter::leaderTakesTheNextKey()
{
    QSignalSpy pending(m_router.get(), &KeyRouter::pendingChanged);
    QVERIFY(m_router->press(u","_s));
    QCOMPARE(m_router->pending(), u"leader"_s);
    QVERIFY(m_router->press(u"m"_s));
    // A key the leader does not know is swallowed, not acted on.
    m_router->press(u","_s);
    QVERIFY(m_router->press(u"j"_s));
    QCOMPARE(triggered(), QStringList{u"monthView"_s});
    QCOMPARE(m_router->pending(), QString());
    QCOMPARE(pending.size(), 4);

    // Another leader, here Space.
    m_router->setProperty("leaderKey", u" "_s);
    QVERIFY(!m_router->press(u","_s));
    m_router->press(u" "_s);
    m_router->press(u"w"_s);
    QCOMPARE(triggered().last(), u"weekView"_s);
}

void TestKeyRouter::leaderGivesUpAfterItsTimeout()
{
    m_router->setProperty("leaderTimeout", 50);
    m_router->press(u","_s);
    QTRY_COMPARE(m_router->pending(), QString());
    // Then the key is an ordinary one again.
    m_router->press(u"t"_s);
    QCOMPARE(triggered(), QStringList{u"today"_s});

    // With no timeout, it waits until told to stop.
    m_router->setProperty("leaderTimeout", 0);
    m_router->press(u","_s);
    QTest::qWait(100);
    QCOMPARE(m_router->pending(), u"leader"_s);
    m_router->cancel();
    QCOMPARE(m_router->pending(), QString());
}

void TestKeyRouter::offLetsKeysThrough()
{
    m_router->press(u","_s);
    // Turning vi mode off drops a command half typed.
    m_router->setProperty("viMode", false);
    QCOMPARE(m_router->pending(), QString());
    QVERIFY(!m_router->press(u"j"_s));
    QVERIFY(triggered().isEmpty());
}

void TestKeyRouter::everyKeyMeansOneThing()
{
    QSet<QString> ids, vi, leader;
    for (const KeyAction &action : KeyRouter::actions()) {
        QVERIFY2(!ids.contains(action.id), qPrintable(action.id));
        ids.insert(action.id);
        if (!action.vi.isEmpty()) {
            QVERIFY2(!vi.contains(action.vi), qPrintable(action.vi));
            vi.insert(action.vi);
        }
        // An action has a single key or a leader key, never both.
        QVERIFY2(action.vi.isEmpty() || action.leader.isEmpty(), qPrintable(action.id));
        if (!action.leader.isEmpty()) {
            QVERIFY2(!leader.contains(action.leader), qPrintable(action.leader));
            leader.insert(action.leader);
        }
    }
    // No vi key may be a leader, or the leader could never be typed.
    for (const QString &key : Settings::kLeaderKeys)
        QVERIFY2(!vi.contains(key), qPrintable(key));
    QVERIFY(!vi.contains(u"g"_s));
}

QTEST_GUILESS_MAIN(TestKeyRouter)
#include "tst_keyrouter.moc"
