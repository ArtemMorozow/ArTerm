#include "terminal/KeyEncoder.hpp"

namespace arterm::term {
namespace {

constexpr char kEsc = '\033';

/// On macOS Qt reports Cmd as ControlModifier and Ctrl as MetaModifier unless
/// AA_MacDontSwapCtrlAndMeta is set. ArTerm sets that attribute at startup, so
/// here ControlModifier really is the Control key on every platform.
constexpr bool hasControl(Qt::KeyboardModifiers modifiers)
{
    return modifiers.testFlag(Qt::ControlModifier);
}

} // namespace

int KeyEncoder::modifierParameter(Qt::KeyboardModifiers modifiers)
{
    int value = 0;
    if (modifiers.testFlag(Qt::ShiftModifier))
        value |= 1;
    if (modifiers.testFlag(Qt::AltModifier))
        value |= 2;
    if (hasControl(modifiers))
        value |= 4;
    if (modifiers.testFlag(Qt::MetaModifier))
        value |= 8;
    return value == 0 ? 0 : value + 1;
}

QByteArray KeyEncoder::tildeSequence(int number, int modifier)
{
    if (modifier == 0)
        return QByteArrayLiteral("\033[") + QByteArray::number(number) + '~';
    return QByteArrayLiteral("\033[") + QByteArray::number(number) + ';'
           + QByteArray::number(modifier) + '~';
}

QByteArray KeyEncoder::cursorSequence(char final, int modifier, bool applicationMode)
{
    if (modifier != 0) {
        // The modified form is always CSI, even in application mode.
        return QByteArrayLiteral("\033[1;") + QByteArray::number(modifier) + final;
    }
    return QByteArray(1, kEsc) + (applicationMode ? 'O' : '[') + final;
}

QByteArray KeyEncoder::encode(const QKeyEvent &event, const Options &options)
{
    const Qt::KeyboardModifiers modifiers = event.modifiers();
    const int modifier = modifierParameter(modifiers);
    const int key = event.key();

    // Cmd on macOS (and Ctrl+Shift elsewhere) is reserved for menu shortcuts.
    if (modifiers.testFlag(Qt::MetaModifier) && !modifiers.testFlag(Qt::ControlModifier)
        && !modifiers.testFlag(Qt::AltModifier)) {
#ifdef Q_OS_MACOS
        return {};
#endif
    }

    switch (key) {
    case Qt::Key_Up:
        return cursorSequence('A', modifier, options.applicationCursorKeys);
    case Qt::Key_Down:
        return cursorSequence('B', modifier, options.applicationCursorKeys);
    case Qt::Key_Right:
        return cursorSequence('C', modifier, options.applicationCursorKeys);
    case Qt::Key_Left:
        return cursorSequence('D', modifier, options.applicationCursorKeys);
    case Qt::Key_Home:
        return cursorSequence('H', modifier, options.applicationCursorKeys);
    case Qt::Key_End:
        return cursorSequence('F', modifier, options.applicationCursorKeys);

    case Qt::Key_Insert:
        return tildeSequence(2, modifier);
    case Qt::Key_Delete:
        return tildeSequence(3, modifier);
    case Qt::Key_PageUp:
        return tildeSequence(5, modifier);
    case Qt::Key_PageDown:
        return tildeSequence(6, modifier);

    case Qt::Key_F1:
        return modifier == 0 ? QByteArrayLiteral("\033OP") : cursorSequence('P', modifier, true);
    case Qt::Key_F2:
        return modifier == 0 ? QByteArrayLiteral("\033OQ") : cursorSequence('Q', modifier, true);
    case Qt::Key_F3:
        return modifier == 0 ? QByteArrayLiteral("\033OR") : cursorSequence('R', modifier, true);
    case Qt::Key_F4:
        return modifier == 0 ? QByteArrayLiteral("\033OS") : cursorSequence('S', modifier, true);
    case Qt::Key_F5:
        return tildeSequence(15, modifier);
    case Qt::Key_F6:
        return tildeSequence(17, modifier);
    case Qt::Key_F7:
        return tildeSequence(18, modifier);
    case Qt::Key_F8:
        return tildeSequence(19, modifier);
    case Qt::Key_F9:
        return tildeSequence(20, modifier);
    case Qt::Key_F10:
        return tildeSequence(21, modifier);
    case Qt::Key_F11:
        return tildeSequence(23, modifier);
    case Qt::Key_F12:
        return tildeSequence(24, modifier);

    case Qt::Key_Return:
    case Qt::Key_Enter:
        return options.newLineMode ? QByteArrayLiteral("\r\n") : QByteArrayLiteral("\r");

    case Qt::Key_Backspace: {
        QByteArray result;
        if (modifiers.testFlag(Qt::AltModifier) && options.optionIsMeta)
            result.append(kEsc);
        // Ctrl inverts the choice, which is the convention every terminal uses
        // to let the user reach whichever byte the host actually wants.
        const bool sendDelete = options.backspaceSendsDelete != hasControl(modifiers);
        result.append(sendDelete ? '\x7F' : '\x08');
        return result;
    }

    case Qt::Key_Tab:
        return QByteArrayLiteral("\t");
    case Qt::Key_Backtab:
        return QByteArrayLiteral("\033[Z");

    case Qt::Key_Escape:
        return QByteArray(1, kEsc);

    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return {};

    default:
        break;
    }

    QString text = event.text();

    if (hasControl(modifiers)) {
        // Map Ctrl+<key> onto the matching C0 control code.
        if (key >= Qt::Key_A && key <= Qt::Key_Z) {
            char control = static_cast<char>(key - Qt::Key_A + 1);
            QByteArray result;
            if (modifiers.testFlag(Qt::AltModifier) && options.optionIsMeta)
                result.append(kEsc);
            result.append(control);
            return result;
        }

        switch (key) {
        case Qt::Key_Space:
        case Qt::Key_2:
        case Qt::Key_At:
            return QByteArray(1, '\0');
        case Qt::Key_BracketLeft:
            return QByteArray(1, '\x1B');
        case Qt::Key_Backslash:
            return QByteArray(1, '\x1C');
        case Qt::Key_BracketRight:
            return QByteArray(1, '\x1D');
        case Qt::Key_AsciiCircum:
        case Qt::Key_6:
            return QByteArray(1, '\x1E');
        case Qt::Key_Underscore:
        case Qt::Key_Minus:
            return QByteArray(1, '\x1F');
        case Qt::Key_Question:
            return QByteArray(1, '\x7F');
        default:
            break;
        }
    }

    if (text.isEmpty())
        return {};

    QByteArray result;

    // Meta/Alt prefixes the sequence with ESC, which is how bash's M-x bindings
    // and vim's Alt mappings are reached.
    if (modifiers.testFlag(Qt::AltModifier) && options.optionIsMeta)
        result.append(kEsc);

    result.append(text.toUtf8());
    return result;
}

QByteArray KeyEncoder::encodePaste(const QString &text, bool bracketed)
{
    QString sanitised = text;

    // Normalise line endings; a stray CRLF makes shells see a blank command.
    sanitised.replace(QLatin1String("\r\n"), QLatin1String("\r"));
    sanitised.replace(QLatin1Char('\n'), QLatin1Char('\r'));

    if (bracketed) {
        // The guard sequence itself must not appear inside the payload,
        // otherwise a crafted clipboard could end the paste early and have the
        // rest executed as typed input.
        sanitised.remove(QLatin1String("\033[201~"));
        return QByteArrayLiteral("\033[200~") + sanitised.toUtf8() + QByteArrayLiteral("\033[201~");
    }

    // Without bracketed paste the shell cannot tell pasted text from typing, so
    // drop the control characters that would run something unintended.
    sanitised.removeIf([](QChar character) {
        const char16_t unit = character.unicode();
        return unit < 0x20 && unit != u'\r' && unit != u'\t';
    });

    return sanitised.toUtf8();
}

} // namespace arterm::term
