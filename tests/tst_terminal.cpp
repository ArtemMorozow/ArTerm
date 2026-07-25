#include "terminal/Terminal.hpp"

#include <QSignalSpy>
#include <QTest>

using namespace arterm::term;

namespace {

QString rowText(const Terminal &terminal, int row)
{
    QString text;
    for (const Cell &cell : terminal.screen().line(row)) {
        if (hasFlag(cell.attributes.flags, CellFlag::WideTrail))
            continue;
        text += QString::fromUcs4(&cell.character, 1);
    }
    while (text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    return text;
}

} // namespace

class TestTerminal : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void printsPlainOutput();
    void cursorPositioning();
    void sgrSetsColours();
    void sgr256AndTrueColour();
    void boldAndResetAreTracked();
    void alternateScreenIsSeparate();
    void titleIsExtractedFromOsc();
    void titleStripsControlCharacters();
    void deviceStatusReportIsAnswered();
    void bracketedPasteModeToggles();
    void mouseTrackingModeToggles();
    void eraseDisplayClearsEverything();
    void lineDrawingCharsetIsApplied();
    void resetRestoresDefaults();
};

void TestTerminal::printsPlainOutput()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("hello world"));

    QCOMPARE(rowText(terminal, 0), QStringLiteral("hello world"));
}

void TestTerminal::cursorPositioning()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("\033[3;5Hx"));

    QCOMPARE(terminal.screen().cursor().row, 2);
    QCOMPARE(rowText(terminal, 2), QStringLiteral("    x"));
}

void TestTerminal::sgrSetsColours()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("\033[31mred\033[0mplain"));

    const Cell &red = terminal.screen().line(0)[0];
    QCOMPARE(red.attributes.foreground.kind(), Color::Kind::Indexed);
    QCOMPARE(red.attributes.foreground.index(), 1);

    const Cell &plain = terminal.screen().line(0)[3];
    QVERIFY(plain.attributes.foreground.isDefault());
}

void TestTerminal::sgr256AndTrueColour()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("\033[38;5;208mA\033[38;2;10;20;30mB"));

    const Cell &indexed = terminal.screen().line(0)[0];
    QCOMPARE(indexed.attributes.foreground.kind(), Color::Kind::Indexed);
    QCOMPARE(indexed.attributes.foreground.index(), 208);

    const Cell &rgb = terminal.screen().line(0)[1];
    QCOMPARE(rgb.attributes.foreground.kind(), Color::Kind::Rgb);
    QCOMPARE(rgb.attributes.foreground.red(), 10);
    QCOMPARE(rgb.attributes.foreground.green(), 20);
    QCOMPARE(rgb.attributes.foreground.blue(), 30);
}

void TestTerminal::boldAndResetAreTracked()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("\033[1mB\033[22mN"));

    QVERIFY(hasFlag(terminal.screen().line(0)[0].attributes.flags, CellFlag::Bold));
    QVERIFY(!hasFlag(terminal.screen().line(0)[1].attributes.flags, CellFlag::Bold));
}

void TestTerminal::alternateScreenIsSeparate()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("normal"));

    terminal.receive(QByteArrayLiteral("\033[?1049h"));
    QVERIFY(terminal.modes().alternateScreen);
    QCOMPARE(rowText(terminal, 0), QString());

    terminal.receive(QByteArrayLiteral("alt"));
    QCOMPARE(rowText(terminal, 0), QStringLiteral("alt"));

    terminal.receive(QByteArrayLiteral("\033[?1049l"));
    QVERIFY(!terminal.modes().alternateScreen);
    QCOMPARE(rowText(terminal, 0), QStringLiteral("normal"));
}

void TestTerminal::titleIsExtractedFromOsc()
{
    Terminal terminal(20, 5);
    QSignalSpy spy(&terminal, &Terminal::titleChanged);

    terminal.receive(QByteArrayLiteral("\033]0;build-01\007"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(terminal.title(), QStringLiteral("build-01"));
}

void TestTerminal::titleStripsControlCharacters()
{
    Terminal terminal(20, 5);
    // A hostile host must not be able to smuggle an escape sequence into the
    // tab bar through the window title.
    terminal.receive(QByteArray("\033]2;bad\x1b[31mtitle\007", 20));

    QVERIFY(!terminal.title().contains(QChar(0x1B)));
}

void TestTerminal::deviceStatusReportIsAnswered()
{
    Terminal terminal(20, 5);
    QSignalSpy spy(&terminal, &Terminal::reply);

    terminal.receive(QByteArrayLiteral("\033[3;7H\033[6n"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toByteArray(), QByteArrayLiteral("\033[3;7R"));
}

void TestTerminal::bracketedPasteModeToggles()
{
    Terminal terminal(20, 5);
    QVERIFY(!terminal.modes().bracketedPaste);

    terminal.receive(QByteArrayLiteral("\033[?2004h"));
    QVERIFY(terminal.modes().bracketedPaste);

    terminal.receive(QByteArrayLiteral("\033[?2004l"));
    QVERIFY(!terminal.modes().bracketedPaste);
}

void TestTerminal::mouseTrackingModeToggles()
{
    Terminal terminal(20, 5);

    terminal.receive(QByteArrayLiteral("\033[?1002h\033[?1006h"));
    QCOMPARE(terminal.modes().mouseTracking, MouseTracking::ButtonEvent);
    QCOMPARE(terminal.modes().mouseEncoding, MouseEncoding::Sgr);

    terminal.receive(QByteArrayLiteral("\033[?1002l"));
    QCOMPARE(terminal.modes().mouseTracking, MouseTracking::Off);
}

void TestTerminal::eraseDisplayClearsEverything()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("line one\r\nline two"));
    terminal.receive(QByteArrayLiteral("\033[2J"));

    QCOMPARE(rowText(terminal, 0), QString());
    QCOMPARE(rowText(terminal, 1), QString());
}

void TestTerminal::lineDrawingCharsetIsApplied()
{
    Terminal terminal(20, 5);
    // ESC ( 0 selects DEC special graphics for G0; 'q' becomes a horizontal line.
    terminal.receive(QByteArrayLiteral("\033(0qqq\033(B"));

    QCOMPARE(rowText(terminal, 0), QStringLiteral("───"));
}

void TestTerminal::resetRestoresDefaults()
{
    Terminal terminal(20, 5);
    terminal.receive(QByteArrayLiteral("\033[?1049h\033[31mtext\033[?7l"));

    terminal.reset();

    QVERIFY(!terminal.modes().alternateScreen);
    QVERIFY(terminal.modes().autoWrap);
    QVERIFY(terminal.currentAttributes().foreground.isDefault());
    QCOMPARE(rowText(terminal, 0), QString());
}

QTEST_MAIN(TestTerminal)

#include "tst_terminal.moc"
