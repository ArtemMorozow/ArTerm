#include "terminal/VtParser.hpp"

#include <QTest>

using namespace arterm::term;

namespace {

/// Records every event so a test can assert on the exact sequence produced.
class RecordingHandler : public VtHandler {
public:
    QString printed;
    QList<std::uint8_t> executed;
    QList<CsiSequence> csi;
    QList<EscSequence> esc;
    QList<QByteArray> osc;

    void print(char32_t codePoint) override { printed += QString::fromUcs4(&codePoint, 1); }
    void execute(std::uint8_t control) override { executed.append(control); }
    void csiDispatch(const CsiSequence &sequence) override { csi.append(sequence); }
    void escDispatch(const EscSequence &sequence) override { esc.append(sequence); }
    void oscDispatch(const QByteArray &payload) override { osc.append(payload); }
};

} // namespace

class TestVtParser : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void plainTextIsPrinted();
    void controlCharactersAreExecuted();
    void csiParametersAreParsed();
    void omittedParameterUsesTheFallback();
    void privateMarkerIsCaptured();
    void oscEndsOnBel();
    void oscEndsOnStringTerminator();
    void escapeAbortsAnIncompleteSequence();
    void utf8IsDecoded();
    void invalidUtf8BecomesReplacementCharacter();
    void splitInputIsResumed();
    void parameterOverflowIsClamped();
};

void TestVtParser::plainTextIsPrinted()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("hello"));

    QCOMPARE(handler.printed, QStringLiteral("hello"));
    QVERIFY(handler.csi.isEmpty());
}

void TestVtParser::controlCharactersAreExecuted()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("a\r\nb\t"));

    QCOMPARE(handler.printed, QStringLiteral("ab"));
    QCOMPARE(handler.executed, QList<std::uint8_t>({0x0D, 0x0A, 0x09}));
}

void TestVtParser::csiParametersAreParsed()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("\033[12;34H"));

    QCOMPARE(handler.csi.size(), 1);
    QCOMPARE(handler.csi.first().final, 'H');
    QCOMPARE(handler.csi.first().parameterCount, 2);
    QCOMPARE(handler.csi.first().parameter(0), 12);
    QCOMPARE(handler.csi.first().parameter(1), 34);
}

void TestVtParser::omittedParameterUsesTheFallback()
{
    RecordingHandler handler;
    VtParser parser(handler);

    // "CSI ;5H" means row default, column 5.
    parser.parse(QByteArrayLiteral("\033[;5H"));

    QCOMPARE(handler.csi.size(), 1);
    const CsiSequence &sequence = handler.csi.first();
    QCOMPARE(sequence.positiveParameter(0), 1);
    QCOMPARE(sequence.positiveParameter(1), 5);
}

void TestVtParser::privateMarkerIsCaptured()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("\033[?1049h"));

    QCOMPARE(handler.csi.size(), 1);
    QCOMPARE(handler.csi.first().privateMarker, '?');
    QCOMPARE(handler.csi.first().parameter(0), 1049);
    QCOMPARE(handler.csi.first().final, 'h');
}

void TestVtParser::oscEndsOnBel()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("\033]0;my title\007rest"));

    QCOMPARE(handler.osc.size(), 1);
    QCOMPARE(handler.osc.first(), QByteArrayLiteral("0;my title"));
    QCOMPARE(handler.printed, QStringLiteral("rest"));
}

void TestVtParser::oscEndsOnStringTerminator()
{
    RecordingHandler handler;
    VtParser parser(handler);

    // The "ESC \" form must not lose the payload collected so far.
    parser.parse(QByteArrayLiteral("\033]2;title\033\\ok"));

    QCOMPARE(handler.osc.size(), 1);
    QCOMPARE(handler.osc.first(), QByteArrayLiteral("2;title"));
    QCOMPARE(handler.printed, QStringLiteral("ok"));
}

void TestVtParser::escapeAbortsAnIncompleteSequence()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("\033[12\033[5A"));

    QCOMPARE(handler.csi.size(), 1);
    QCOMPARE(handler.csi.first().final, 'A');
    QCOMPARE(handler.csi.first().parameter(0), 5);
}

void TestVtParser::utf8IsDecoded()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QStringLiteral("héllo — 日本 🙂").toUtf8());

    QCOMPARE(handler.printed, QStringLiteral("héllo — 日本 🙂"));
}

void TestVtParser::invalidUtf8BecomesReplacementCharacter()
{
    RecordingHandler handler;
    VtParser parser(handler);

    // Two separate errors: a lone continuation byte, then a two-byte lead whose
    // continuation never arrives. Each produces its own replacement character,
    // and the byte that broke the sequence is still printed.
    parser.parse(QByteArray("\x80\xC3", 2));
    parser.parse(QByteArrayLiteral("A"));

    QCOMPARE(handler.printed, QStringLiteral("��A"));

    // An overlong encoding of '/' must not decode back to '/', which is the
    // classic path-traversal trick.
    RecordingHandler overlong;
    VtParser overlongParser(overlong);
    overlongParser.parse(QByteArray("\xC0\xAF", 2));
    QCOMPARE(overlong.printed, QStringLiteral("�"));
}

void TestVtParser::splitInputIsResumed()
{
    RecordingHandler handler;
    VtParser parser(handler);

    // A sequence arriving in three separate reads must still parse as one.
    parser.parse(QByteArrayLiteral("\033["));
    parser.parse(QByteArrayLiteral("31"));
    parser.parse(QByteArrayLiteral("m"));

    QCOMPARE(handler.csi.size(), 1);
    QCOMPARE(handler.csi.first().final, 'm');
    QCOMPARE(handler.csi.first().parameter(0), 31);
}

void TestVtParser::parameterOverflowIsClamped()
{
    RecordingHandler handler;
    VtParser parser(handler);

    parser.parse(QByteArrayLiteral("\033[99999999999999999999A"));

    QCOMPARE(handler.csi.size(), 1);
    // The exact ceiling does not matter; not overflowing does.
    QVERIFY(handler.csi.first().parameter(0) > 0);
}

QTEST_APPLESS_MAIN(TestVtParser)

#include "tst_vtparser.moc"
