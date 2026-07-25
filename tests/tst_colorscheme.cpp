#include "terminal/ColorScheme.hpp"

#include <QTest>

using namespace arterm::term;

class TestColorScheme : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void defaultConstructionTerminates();
    void namedSchemesAreDistinct();
    void paletteIsFullyPopulated();
    void colourCubeMatchesXterm();
    void greyscaleRampIsMonotonic();
    void defaultColourFollowsTheScheme();
    void boldBrightensTheBaseColours();
    void rgbIsPassedThrough();
    void byNameFallsBackToDark();
};

void TestColorScheme::defaultConstructionTerminates()
{
    // Regression: the default constructor used to call arTermDark(), which
    // default-constructed another scheme, recursing until the stack overflowed.
    ColorScheme scheme;
    QCOMPARE(scheme.name(), QStringLiteral("ArTerm Dark"));
    QVERIFY(scheme.background().isValid());
}

void TestColorScheme::namedSchemesAreDistinct()
{
    const ColorScheme dark = ColorScheme::arTermDark();
    const ColorScheme light = ColorScheme::arTermLight();

    QVERIFY(dark.background() != light.background());
    QVERIFY(dark.background().lightness() < light.background().lightness());
    QVERIFY(dark.foreground().lightness() > light.foreground().lightness());
}

void TestColorScheme::paletteIsFullyPopulated()
{
    const ColorScheme scheme = ColorScheme::arTermDark();
    for (int i = 0; i < 256; ++i)
        QVERIFY2(scheme.indexed(static_cast<std::uint8_t>(i)).isValid(),
                 qPrintable(QStringLiteral("palette slot %1 is unset").arg(i)));
}

void TestColorScheme::colourCubeMatchesXterm()
{
    const ColorScheme scheme = ColorScheme::arTermDark();

    // Slot 16 is the corner of the cube: pure black.
    QCOMPARE(scheme.indexed(16), QColor(0, 0, 0));
    // Slot 231 is the opposite corner: pure white.
    QCOMPARE(scheme.indexed(231), QColor(255, 255, 255));
    // Slot 196 is 5,0,0 in the cube: full red.
    QCOMPARE(scheme.indexed(196), QColor(255, 0, 0));
}

void TestColorScheme::greyscaleRampIsMonotonic()
{
    const ColorScheme scheme = ColorScheme::arTermDark();

    for (int i = 233; i <= 255; ++i) {
        const QColor previous = scheme.indexed(static_cast<std::uint8_t>(i - 1));
        const QColor current = scheme.indexed(static_cast<std::uint8_t>(i));
        QVERIFY(current.red() > previous.red());
        QCOMPARE(current.red(), current.green());
        QCOMPARE(current.green(), current.blue());
    }
}

void TestColorScheme::defaultColourFollowsTheScheme()
{
    const ColorScheme scheme = ColorScheme::arTermDark();

    QCOMPARE(scheme.resolve(Color::defaultColor(), /*isForeground=*/true, false), scheme.foreground());
    QCOMPARE(scheme.resolve(Color::defaultColor(), /*isForeground=*/false, false), scheme.background());
}

void TestColorScheme::boldBrightensTheBaseColours()
{
    const ColorScheme scheme = ColorScheme::arTermDark();

    // "SGR 1;31" must produce bright red, i.e. slot 9 rather than slot 1.
    const QColor normal = scheme.resolve(Color::indexed(1), true, /*bold=*/false);
    const QColor bright = scheme.resolve(Color::indexed(1), true, /*bold=*/true);

    QCOMPARE(normal, scheme.indexed(1));
    QCOMPARE(bright, scheme.indexed(9));

    // Only the first eight slots brighten, and only in the foreground.
    QCOMPARE(scheme.resolve(Color::indexed(1), false, true), scheme.indexed(1));
    QCOMPARE(scheme.resolve(Color::indexed(120), true, true), scheme.indexed(120));
}

void TestColorScheme::rgbIsPassedThrough()
{
    const ColorScheme scheme = ColorScheme::arTermDark();
    QCOMPARE(scheme.resolve(Color::rgb(12, 34, 56), true, false), QColor(12, 34, 56));
}

void TestColorScheme::byNameFallsBackToDark()
{
    QCOMPARE(ColorScheme::byName(QStringLiteral("ArTerm Light")).name(),
             QStringLiteral("ArTerm Light"));
    QCOMPARE(ColorScheme::byName(QStringLiteral("does not exist")).name(),
             QStringLiteral("ArTerm Dark"));
}

QTEST_MAIN(TestColorScheme)

#include "tst_colorscheme.moc"
