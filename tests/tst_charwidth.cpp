#include "terminal/CharWidth.hpp"

#include <QTest>

using namespace arterm::term;

class TestCharWidth : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void asciiIsSingleWidth();
    void combiningMarksAreZeroWidth();
    void cjkIsDoubleWidth();
    void emojiIsDoubleWidth();
    void latinAccentsAreSingleWidth();
    void cyrillicIsSingleWidth();
    void controlCharactersAreZeroWidth();
    void boxDrawingIsSingleWidth();
};

void TestCharWidth::asciiIsSingleWidth()
{
    for (char32_t c = U' '; c <= U'~'; ++c)
        QCOMPARE(characterWidth(c), 1);
}

void TestCharWidth::combiningMarksAreZeroWidth()
{
    QCOMPARE(characterWidth(0x0301), 0); // Combining acute accent.
    QCOMPARE(characterWidth(0x0300), 0); // Combining grave accent.
    QCOMPARE(characterWidth(0xFE0F), 0); // Variation selector-16.
    QCOMPARE(characterWidth(0x200B), 0); // Zero-width space.
}

void TestCharWidth::cjkIsDoubleWidth()
{
    QCOMPARE(characterWidth(U'日'), 2);
    QCOMPARE(characterWidth(U'本'), 2);
    QCOMPARE(characterWidth(U'한'), 2); // Hangul syllable.
    QCOMPARE(characterWidth(U'あ'), 2); // Hiragana.
    QCOMPARE(characterWidth(0xFF21), 2); // Fullwidth A.
}

void TestCharWidth::emojiIsDoubleWidth()
{
    QCOMPARE(characterWidth(0x1F600), 2); // Grinning face.
    QCOMPARE(characterWidth(0x1F680), 2); // Rocket.
    QCOMPARE(characterWidth(0x2705), 2);  // White heavy check mark.
}

void TestCharWidth::latinAccentsAreSingleWidth()
{
    QCOMPARE(characterWidth(U'é'), 1);
    QCOMPARE(characterWidth(U'ü'), 1);
    QCOMPARE(characterWidth(U'ñ'), 1);
}

void TestCharWidth::cyrillicIsSingleWidth()
{
    QCOMPARE(characterWidth(U'Ж'), 1);
    QCOMPARE(characterWidth(U'п'), 1);
}

void TestCharWidth::controlCharactersAreZeroWidth()
{
    QCOMPARE(characterWidth(0x00), 0);
    QCOMPARE(characterWidth(0x07), 0);
    QCOMPARE(characterWidth(0x1B), 0);
}

void TestCharWidth::boxDrawingIsSingleWidth()
{
    // ncurses borders must line up, so these must never be reported as wide.
    QCOMPARE(characterWidth(0x2500), 1); // ─
    QCOMPARE(characterWidth(0x2502), 1); // │
    QCOMPARE(characterWidth(0x253C), 1); // ┼
    QCOMPARE(characterWidth(0x2592), 1); // ▒
}

QTEST_APPLESS_MAIN(TestCharWidth)

#include "tst_charwidth.moc"
