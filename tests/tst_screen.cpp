#include "terminal/Screen.hpp"

#include <QTest>

using namespace arterm::term;

namespace {

/// Reads a row back as text so assertions stay readable.
QString rowText(const Screen &screen, int row)
{
    QString text;
    const Line &line = screen.line(row);
    for (const Cell &cell : line) {
        if (hasFlag(cell.attributes.flags, CellFlag::WideTrail))
            continue;
        text += QString::fromUcs4(&cell.character, 1);
    }
    while (text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    return text;
}

void write(Screen &screen, const QString &text, bool autoWrap = true)
{
    for (const QChar character : text) {
        screen.writeCharacter(character.unicode(), 1, Attributes{}, /*insertMode=*/false, autoWrap);
    }
}

} // namespace

class TestScreen : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void writesAdvanceTheCursor();
    void wrappingIsDeferredToTheNextCharacter();
    void autoWrapOffOverwritesTheLastColumn();
    void indexScrollsAtTheBottom();
    void scrollUpFeedsScrollback();
    void scrollRegionDoesNotFeedScrollback();
    void eraseInLineRespectsTheMode();
    void insertAndDeleteCharacters();
    void insertAndDeleteLines();
    void resizePreservesContent();
    void growingPullsBackFromScrollback();
    void tabStopsAdvanceByEight();
    void wideCharactersOccupyTwoColumns();
};

void TestScreen::writesAdvanceTheCursor()
{
    Screen screen(10, 4, 100);
    write(screen, QStringLiteral("abc"));

    QCOMPARE(rowText(screen, 0), QStringLiteral("abc"));
    QCOMPARE(screen.cursor().column, 3);
    QCOMPARE(screen.cursor().row, 0);
}

void TestScreen::wrappingIsDeferredToTheNextCharacter()
{
    Screen screen(3, 3, 100);
    write(screen, QStringLiteral("abc"));

    // After filling the row the cursor stays put with the wrap pending, so a
    // sequence that repositions the cursor does not lose a line.
    QCOMPARE(screen.cursor().row, 0);
    QCOMPARE(screen.cursor().column, 2);
    QVERIFY(screen.pendingWrap());

    write(screen, QStringLiteral("d"));
    QCOMPARE(screen.cursor().row, 1);
    QCOMPARE(rowText(screen, 1), QStringLiteral("d"));
    QVERIFY(screen.isLineWrapped(0));
}

void TestScreen::autoWrapOffOverwritesTheLastColumn()
{
    Screen screen(3, 3, 100);
    write(screen, QStringLiteral("abcXY"), /*autoWrap=*/false);

    QCOMPARE(rowText(screen, 0), QStringLiteral("abY"));
    QCOMPARE(rowText(screen, 1), QString());
}

void TestScreen::indexScrollsAtTheBottom()
{
    Screen screen(5, 2, 100);
    write(screen, QStringLiteral("one"));
    screen.index(Attributes{});
    screen.carriageReturn();
    write(screen, QStringLiteral("two"));
    screen.index(Attributes{});
    screen.carriageReturn();
    write(screen, QStringLiteral("three"));

    QCOMPARE(rowText(screen, 0), QStringLiteral("two"));
    QCOMPARE(rowText(screen, 1), QStringLiteral("three"));
}

void TestScreen::scrollUpFeedsScrollback()
{
    Screen screen(5, 2, 100);
    write(screen, QStringLiteral("one"));
    screen.scrollUp(1, Attributes{});

    QCOMPARE(screen.scrollbackSize(), 1);
    const Line *history = screen.historyLine(-1);
    QVERIFY(history != nullptr);
    QCOMPARE(QString::fromUcs4(&history->at(0).character, 1), QStringLiteral("o"));
}

void TestScreen::scrollRegionDoesNotFeedScrollback()
{
    Screen screen(5, 4, 100);
    screen.setScrollRegion(1, 2);
    screen.scrollUp(1, Attributes{});

    // A partial region is a pane being redrawn, not history being produced.
    QCOMPARE(screen.scrollbackSize(), 0);
}

void TestScreen::eraseInLineRespectsTheMode()
{
    Screen screen(6, 2, 10);
    write(screen, QStringLiteral("abcdef"));
    screen.moveCursor(0, 3);
    screen.eraseInLine(Screen::EraseMode::ToEnd, Attributes{});
    QCOMPARE(rowText(screen, 0), QStringLiteral("abc"));

    write(screen, QStringLiteral("XYZ"));
    screen.moveCursor(0, 3);
    screen.eraseInLine(Screen::EraseMode::ToStart, Attributes{});
    QCOMPARE(rowText(screen, 0), QStringLiteral("    YZ"));
}

void TestScreen::insertAndDeleteCharacters()
{
    Screen screen(6, 2, 10);
    write(screen, QStringLiteral("abcdef"));

    screen.moveCursor(0, 2);
    screen.deleteCharacters(2, Attributes{});
    QCOMPARE(rowText(screen, 0), QStringLiteral("abef"));

    screen.moveCursor(0, 2);
    screen.insertCharacters(1, Attributes{});
    QCOMPARE(rowText(screen, 0), QStringLiteral("ab ef"));
}

void TestScreen::insertAndDeleteLines()
{
    Screen screen(4, 3, 10);
    write(screen, QStringLiteral("aaa"));
    screen.moveCursor(1, 0);
    write(screen, QStringLiteral("bbb"));
    screen.moveCursor(2, 0);
    write(screen, QStringLiteral("ccc"));

    screen.moveCursor(1, 0);
    screen.insertLines(1, Attributes{});
    QCOMPARE(rowText(screen, 0), QStringLiteral("aaa"));
    QCOMPARE(rowText(screen, 1), QString());
    QCOMPARE(rowText(screen, 2), QStringLiteral("bbb"));

    screen.moveCursor(1, 0);
    screen.deleteLines(1, Attributes{});
    QCOMPARE(rowText(screen, 1), QStringLiteral("bbb"));
}

void TestScreen::resizePreservesContent()
{
    Screen screen(10, 4, 100);
    write(screen, QStringLiteral("hello"));

    screen.resize(20, 4);
    QCOMPARE(rowText(screen, 0), QStringLiteral("hello"));
    QCOMPARE(screen.columns(), 20);
}

void TestScreen::growingPullsBackFromScrollback()
{
    Screen screen(6, 2, 100);
    write(screen, QStringLiteral("first"));
    screen.scrollUp(1, Attributes{});
    QCOMPARE(screen.scrollbackSize(), 1);

    screen.resize(6, 3);

    // The line that had scrolled off comes back rather than being replaced by
    // an empty row.
    QCOMPARE(screen.scrollbackSize(), 0);
    QCOMPARE(rowText(screen, 0), QStringLiteral("first"));
}

void TestScreen::tabStopsAdvanceByEight()
{
    Screen screen(40, 2, 10);
    QCOMPARE(screen.nextTabStop(0), 8);
    QCOMPARE(screen.nextTabStop(8), 16);
    QCOMPARE(screen.previousTabStop(20), 16);

    screen.clearAllTabStops();
    QCOMPARE(screen.nextTabStop(0), 39);
}

void TestScreen::wideCharactersOccupyTwoColumns()
{
    Screen screen(6, 2, 10);
    screen.writeCharacter(U'日', 2, Attributes{}, false, true);

    QCOMPARE(screen.cursor().column, 2);
    QVERIFY(hasFlag(screen.line(0)[0].attributes.flags, CellFlag::WideLead));
    QVERIFY(hasFlag(screen.line(0)[1].attributes.flags, CellFlag::WideTrail));

    // Overwriting the lead must clear its orphaned trailer.
    screen.moveCursor(0, 0);
    screen.writeCharacter(U'x', 1, Attributes{}, false, true);
    QVERIFY(!hasFlag(screen.line(0)[1].attributes.flags, CellFlag::WideTrail));
}

QTEST_APPLESS_MAIN(TestScreen)

#include "tst_screen.moc"
