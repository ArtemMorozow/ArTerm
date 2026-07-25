#pragma once

#include <QByteArray>
#include <QKeyEvent>

namespace arterm::term {

/// Turns Qt key events into the byte sequences an xterm-compatible host
/// expects.
///
/// The two mode flags come from the emulator: DECCKM changes the cursor keys
/// between the CSI and SS3 forms, and DECKPAM does the same for the keypad.
class KeyEncoder {
public:
    struct Options {
        bool applicationCursorKeys{false};
        bool applicationKeypad{false};
        bool newLineMode{false};
        /// macOS: when true, the Option key acts as Meta and prefixes ESC;
        /// when false it composes characters (é, ø, ...) as the system does.
        bool optionIsMeta{true};
        /// Send DEL (0x7F) rather than BS (0x08) for the Backspace key.
        bool backspaceSendsDelete{true};
    };

    /// Returns the bytes to send, or an empty array when the key should be
    /// handled by the widget instead (shortcuts, modifiers on their own).
    [[nodiscard]] static QByteArray encode(const QKeyEvent &event, const Options &options);

    /// Wrap pasted text for bracketed paste mode and strip anything that could
    /// be mistaken for a control sequence.
    [[nodiscard]] static QByteArray encodePaste(const QString &text, bool bracketed);

private:
    /// xterm's modifier parameter: 1 + shift(1) + alt(2) + ctrl(4) + meta(8).
    [[nodiscard]] static int modifierParameter(Qt::KeyboardModifiers modifiers);

    /// Build "ESC [ <number> ; <mod> ~" or the unmodified "ESC [ <number> ~".
    [[nodiscard]] static QByteArray tildeSequence(int number, int modifier);

    /// Build "ESC [ 1 ; <mod> <final>", or the CSI/SS3 form when unmodified.
    [[nodiscard]] static QByteArray cursorSequence(char final, int modifier, bool applicationMode);
};

} // namespace arterm::term
