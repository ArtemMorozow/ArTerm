#include "terminal/KeyEncoder.hpp"

#include <QTest>

using namespace arterm::term;

namespace {

QByteArray encode(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                  const QString &text = {}, const KeyEncoder::Options &options = {})
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
    return KeyEncoder::encode(event, options);
}

} // namespace

class TestKeyEncoder : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cursorKeysUseCsiByDefault();
    void cursorKeysUseSs3InApplicationMode();
    void modifiedCursorKeysUseTheCsiForm();
    void functionKeys();
    void controlLettersBecomeC0();
    void controlPunctuation();
    void altPrefixesEscape();
    void backspaceSendsDelete();
    void enterRespectsNewLineMode();
    void modifierKeysAloneProduceNothing();
    void bracketedPasteWrapsTheText();
    void bracketedPasteCannotBeEscaped();
    void unbracketedPasteStripsControls();
};

void TestKeyEncoder::cursorKeysUseCsiByDefault()
{
    QCOMPARE(encode(Qt::Key_Up), QByteArrayLiteral("\033[A"));
    QCOMPARE(encode(Qt::Key_Down), QByteArrayLiteral("\033[B"));
    QCOMPARE(encode(Qt::Key_Right), QByteArrayLiteral("\033[C"));
    QCOMPARE(encode(Qt::Key_Left), QByteArrayLiteral("\033[D"));
}

void TestKeyEncoder::cursorKeysUseSs3InApplicationMode()
{
    KeyEncoder::Options options;
    options.applicationCursorKeys = true;

    QCOMPARE(encode(Qt::Key_Up, Qt::NoModifier, {}, options), QByteArrayLiteral("\033OA"));
    QCOMPARE(encode(Qt::Key_Home, Qt::NoModifier, {}, options), QByteArrayLiteral("\033OH"));
}

void TestKeyEncoder::modifiedCursorKeysUseTheCsiForm()
{
    // Modifier parameter: 1 + shift(1) = 2, 1 + ctrl(4) = 5.
    QCOMPARE(encode(Qt::Key_Right, Qt::ShiftModifier), QByteArrayLiteral("\033[1;2C"));
    QCOMPARE(encode(Qt::Key_Left, Qt::ControlModifier), QByteArrayLiteral("\033[1;5D"));
}

void TestKeyEncoder::functionKeys()
{
    QCOMPARE(encode(Qt::Key_F1), QByteArrayLiteral("\033OP"));
    QCOMPARE(encode(Qt::Key_F5), QByteArrayLiteral("\033[15~"));
    QCOMPARE(encode(Qt::Key_F12), QByteArrayLiteral("\033[24~"));
    QCOMPARE(encode(Qt::Key_PageUp), QByteArrayLiteral("\033[5~"));
    QCOMPARE(encode(Qt::Key_Delete), QByteArrayLiteral("\033[3~"));
}

void TestKeyEncoder::controlLettersBecomeC0()
{
    QCOMPARE(encode(Qt::Key_C, Qt::ControlModifier, QStringLiteral("c")), QByteArray(1, '\x03'));
    QCOMPARE(encode(Qt::Key_D, Qt::ControlModifier, QStringLiteral("d")), QByteArray(1, '\x04'));
    QCOMPARE(encode(Qt::Key_A, Qt::ControlModifier, QStringLiteral("a")), QByteArray(1, '\x01'));
}

void TestKeyEncoder::controlPunctuation()
{
    QCOMPARE(encode(Qt::Key_Space, Qt::ControlModifier), QByteArray(1, '\0'));
    QCOMPARE(encode(Qt::Key_BracketLeft, Qt::ControlModifier), QByteArray(1, '\x1B'));
    QCOMPARE(encode(Qt::Key_Backslash, Qt::ControlModifier), QByteArray(1, '\x1C'));
}

void TestKeyEncoder::altPrefixesEscape()
{
    const QByteArray encoded = encode(Qt::Key_F, Qt::AltModifier, QStringLiteral("f"));
    QCOMPARE(encoded, QByteArrayLiteral("\033f"));
}

void TestKeyEncoder::backspaceSendsDelete()
{
    QCOMPARE(encode(Qt::Key_Backspace), QByteArray(1, '\x7F'));

    // Ctrl inverts the choice so the other byte is still reachable.
    QCOMPARE(encode(Qt::Key_Backspace, Qt::ControlModifier), QByteArray(1, '\x08'));

    KeyEncoder::Options options;
    options.backspaceSendsDelete = false;
    QCOMPARE(encode(Qt::Key_Backspace, Qt::NoModifier, {}, options), QByteArray(1, '\x08'));
}

void TestKeyEncoder::enterRespectsNewLineMode()
{
    QCOMPARE(encode(Qt::Key_Return), QByteArrayLiteral("\r"));

    KeyEncoder::Options options;
    options.newLineMode = true;
    QCOMPARE(encode(Qt::Key_Return, Qt::NoModifier, {}, options), QByteArrayLiteral("\r\n"));
}

void TestKeyEncoder::modifierKeysAloneProduceNothing()
{
    QVERIFY(encode(Qt::Key_Shift).isEmpty());
    QVERIFY(encode(Qt::Key_Control).isEmpty());
    QVERIFY(encode(Qt::Key_CapsLock).isEmpty());
}

void TestKeyEncoder::bracketedPasteWrapsTheText()
{
    const QByteArray encoded = KeyEncoder::encodePaste(QStringLiteral("ls -la"), true);
    QCOMPARE(encoded, QByteArrayLiteral("\033[200~ls -la\033[201~"));
}

void TestKeyEncoder::bracketedPasteCannotBeEscaped()
{
    // Clipboard content containing the end marker must not be able to close the
    // paste early and have the remainder executed as typed input.
    const QByteArray encoded =
        KeyEncoder::encodePaste(QStringLiteral("safe\033[201~rm -rf /"), true);

    QCOMPARE(encoded.count(QByteArrayLiteral("\033[201~")), 1);
    QVERIFY(encoded.endsWith(QByteArrayLiteral("\033[201~")));
}

void TestKeyEncoder::unbracketedPasteStripsControls()
{
    const QByteArray encoded = KeyEncoder::encodePaste(QStringLiteral("a\033[31mb\x07c"), false);

    QVERIFY(!encoded.contains('\033'));
    QVERIFY(!encoded.contains('\x07'));

    // Newlines survive, because a multi-line paste is legitimate.
    const QByteArray multiline = KeyEncoder::encodePaste(QStringLiteral("one\ntwo"), false);
    QCOMPARE(multiline, QByteArrayLiteral("one\rtwo"));
}

QTEST_MAIN(TestKeyEncoder)

#include "tst_keyencoder.moc"
