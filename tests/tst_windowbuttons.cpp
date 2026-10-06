#include "WindowButtons.h"

#include <QTest>

using namespace callie;
using namespace Qt::StringLiterals;

class TestWindowButtons : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gnomeDefaultHasOnlyClose();
    void gnomeLayoutKeepsOrderAndSides();
    void gnomeIgnoresUnknownButtons();
    void kdeDefaultPutsThreeOnTheRight();
    void kdeLettersMapToButtons();
};

void TestWindowButtons::gnomeDefaultHasOnlyClose()
{
    const auto layout = WindowButtons::fromGnome(u"'appmenu:close'\n"_s);
    QCOMPARE(layout.left, QStringList());
    QCOMPARE(layout.right, QStringList{u"close"_s});
}

void TestWindowButtons::gnomeLayoutKeepsOrderAndSides()
{
    const auto layout = WindowButtons::fromGnome(u"'close,minimize,maximize:'"_s);
    QCOMPARE(layout.left, (QStringList{u"close"_s, u"minimize"_s, u"maximize"_s}));
    QCOMPARE(layout.right, QStringList());
}

void TestWindowButtons::gnomeIgnoresUnknownButtons()
{
    const auto layout = WindowButtons::fromGnome(u"icon,menu:spacer,minimize,maximize,close"_s);
    QCOMPARE(layout.left, QStringList());
    QCOMPARE(layout.right, (QStringList{u"minimize"_s, u"maximize"_s, u"close"_s}));
}

void TestWindowButtons::kdeDefaultPutsThreeOnTheRight()
{
    const auto layout = WindowButtons::fromKde(u"MS"_s, u"HIAX"_s);
    QCOMPARE(layout.left, QStringList());
    QCOMPARE(layout.right, (QStringList{u"minimize"_s, u"maximize"_s, u"close"_s}));
}

void TestWindowButtons::kdeLettersMapToButtons()
{
    const auto layout = WindowButtons::fromKde(u"XIA"_s, u"N"_s);
    QCOMPARE(layout.left, (QStringList{u"close"_s, u"minimize"_s, u"maximize"_s}));
    QCOMPARE(layout.right, QStringList());
}

QTEST_GUILESS_MAIN(TestWindowButtons)
#include "tst_windowbuttons.moc"
