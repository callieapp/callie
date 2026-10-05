#include "callie/Color.h"

#include <QTest>

using namespace callie;

namespace {

double hueDistance(double a, double b)
{
    const double d = std::fmod(std::abs(a - b), 360.0);
    return d > 180 ? 360 - d : d;
}

} // namespace

class TestColor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void contrastSpansOneToTwentyOne();
    void knownOklchValue();
    void oklchRoundTrips_data();
    void oklchRoundTrips();
    void outOfGamutKeepsHue();
    void harmonizeKeepsHueAndSetsLightness();
    void harmonizeLeavesGreysGrey();
    void parseHex_data();
    void parseHex();
};

void TestColor::contrastSpansOneToTwentyOne()
{
    QCOMPARE(qRound(color::contrastRatio(Qt::black, Qt::white) * 100), 2100);
    QCOMPARE(color::contrastRatio(QColor("#cc6699"), QColor("#cc6699")), 1.0);
}

void TestColor::knownOklchValue()
{
    // The logo pink, checked against an independent OKLCH implementation.
    const color::Oklch pink = color::toOklch(QColor("#cc6699"));
    QVERIFY(qAbs(pink.lightness - 0.645) < 0.002);
    QVERIFY(qAbs(pink.chroma - 0.141) < 0.002);
    QVERIFY(hueDistance(pink.hue, 350.5) < 0.5);
}

void TestColor::oklchRoundTrips_data()
{
    QTest::addColumn<QColor>("input");
    for (const char *hex :
         {"#000000", "#ffffff", "#cc6699", "#180f17", "#039be5", "#f6bf26", "#0b8043"})
        QTest::newRow(hex) << QColor(QString::fromLatin1(hex));
}

void TestColor::oklchRoundTrips()
{
    QFETCH(QColor, input);
    const QColor output = color::fromOklch(color::toOklch(input));
    QVERIFY2(qAbs(output.red() - input.red()) <= 1 && qAbs(output.green() - input.green()) <= 1 &&
                 qAbs(output.blue() - input.blue()) <= 1,
             qPrintable(output.name() + " != " + input.name()));
}

void TestColor::outOfGamutKeepsHue()
{
    // Chroma 0.4 is far outside sRGB; the result must shed chroma, not shift hue.
    const QColor mapped = color::fromOklch({0.72, 0.4, 140});
    QVERIFY(mapped.isValid());
    QVERIFY(hueDistance(color::toOklch(mapped).hue, 140) < 2);
}

void TestColor::harmonizeKeepsHueAndSetsLightness()
{
    const QColor source("#039be5");
    const color::Oklch fitted = color::toOklch(color::harmonize(source, 0.72, 0.12));
    QVERIFY(hueDistance(fitted.hue, color::toOklch(source).hue) < 2);
    QVERIFY(qAbs(fitted.lightness - 0.72) < 0.01);
    QVERIFY(qAbs(fitted.chroma - 0.12) < 0.01);
}

void TestColor::harmonizeLeavesGreysGrey()
{
    const color::Oklch fitted = color::toOklch(color::harmonize(QColor("#616161"), 0.72, 0.12));
    QVERIFY(fitted.chroma < 0.03);
}

void TestColor::parseHex_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QColor>("expected");
    QTest::newRow("rgb") << "#cc6699" << true << QColor(0xcc, 0x66, 0x99);
    QTest::newRow("alpha is last, as in CSS")
        << "#cc669980" << true << QColor(0xcc, 0x66, 0x99, 0x80);
    QTest::newRow("no hash") << "cc6699" << false << QColor();
    QTest::newRow("short") << "#c69" << false << QColor();
    QTest::newRow("five digits") << "#12345" << false << QColor();
    QTest::newRow("not hex") << "#zzzzzz" << false << QColor();
    QTest::newRow("signs") << "#+1+2+3" << false << QColor();
    QTest::newRow("non-ascii") << QStringLiteral("#12345\u00e9") << false << QColor();
    QTest::newRow("wide characters")
        << QStringLiteral("#\u4e2d\u4e2d\u4e2d\u4e2d\u4e2d\u4e2d") << false << QColor();
}

void TestColor::parseHex()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    QFETCH(QColor, expected);
    const auto parsed = color::parseHex(text);
    QCOMPARE(parsed.has_value(), valid);
    if (valid)
        QCOMPARE(parsed->rgba(), expected.rgba());
}

QTEST_GUILESS_MAIN(TestColor)
#include "tst_color.moc"
