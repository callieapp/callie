#include "Paint.h"

#include <QTest>

using namespace callie::cli;
using namespace Qt::StringLiterals;

class TestPaint : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void eachDepthHasItsOwnCodes();
    void nearestColorsAreClose();
    void plainWhenOff();
    void pipesGetNoColor();
};

void TestPaint::eachDepthHasItsOwnCodes()
{
    const QColor pink(240, 143, 184);
    QCOMPARE(Paint::foreground(pink, Paint::Depth::True), u"\033[38;2;240;143;184m"_s);
    QVERIFY(Paint::foreground(pink, Paint::Depth::Ansi256).startsWith(u"\033[38;5;"_s));
    QCOMPARE(Paint::foreground(pink, Paint::Depth::None), QString());
}

void TestPaint::nearestColorsAreClose()
{
    // Pure colors land on their own cube corners, greys on the grey ramp.
    QCOMPARE(Paint::foreground(QColor(255, 0, 0), Paint::Depth::Ansi256), u"\033[38;5;196m"_s);
    QCOMPARE(Paint::foreground(QColor(0, 0, 255), Paint::Depth::Ansi256), u"\033[38;5;21m"_s);
    QCOMPARE(Paint::foreground(QColor(128, 128, 128), Paint::Depth::Ansi256), u"\033[38;5;244m"_s);
    // And on the basic colors by what they look like.
    QCOMPARE(Paint::foreground(QColor(220, 40, 40), Paint::Depth::Basic), u"\033[31m"_s);
    QCOMPARE(Paint::foreground(QColor(30, 110, 210), Paint::Depth::Basic), u"\033[34m"_s);
    QCOMPARE(Paint::foreground(QColor(20, 170, 200), Paint::Depth::Basic), u"\033[36m"_s);
}

void TestPaint::plainWhenOff()
{
    const Paint none;
    QVERIFY(!none.enabled());
    QCOMPARE(none.bold(u"Today"_s), u"Today"_s);
    QCOMPARE(none.calendar(QColor(200, 0, 0), u"●"_s), u"●"_s);
    const Paint off(Paint::Depth::None, Palette::forTheme({}));
    QVERIFY(!off.enabled());
    QCOMPARE(off.accent(u"Today"_s), u"Today"_s);
}

void TestPaint::pipesGetNoColor()
{
    // Tests run with output to a file or pipe, never a terminal.
    QCOMPARE(Paint::detect(-1), Paint::Depth::None);
    qputenv("NO_COLOR", "1");
    QCOMPARE(Paint::detect(1), Paint::Depth::None);
    qunsetenv("NO_COLOR");
}

QTEST_GUILESS_MAIN(TestPaint)
#include "tst_paint.moc"
