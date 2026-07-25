#pragma once

#include "terminal/Screen.hpp"
#include "terminal/VtParser.hpp"

#include <QObject>
#include <QString>

#include <array>
#include <memory>

namespace arterm::term {

/// How the application asked for mouse events to be reported.
enum class MouseTracking {
    Off,
    X10,        ///< 9: press only.
    Normal,     ///< 1000: press and release.
    ButtonEvent,///< 1002: press, release and drag.
    AnyEvent,   ///< 1003: everything including plain motion.
};

enum class MouseEncoding {
    Default, ///< Legacy 0x20-offset byte encoding.
    Utf8,    ///< 1005.
    Sgr,     ///< 1006, the only one that survives past column 223.
    Urxvt,   ///< 1015.
};

/// The DEC/xterm modes ArTerm honours.
struct TerminalModes {
    bool autoWrap{true};             ///< DECAWM (7).
    bool originMode{false};          ///< DECOM (6).
    bool insertMode{false};          ///< IRM (4).
    bool cursorVisible{true};        ///< DECTCEM (25).
    bool applicationCursorKeys{false}; ///< DECCKM (1).
    bool applicationKeypad{false};   ///< DECKPAM.
    bool reverseVideo{false};        ///< DECSCNM (5).
    bool bracketedPaste{false};      ///< 2004.
    bool newLineMode{false};         ///< LNM (20).
    bool focusReporting{false};      ///< 1004.
    bool alternateScreen{false};     ///< 1047/1049.

    MouseTracking mouseTracking{MouseTracking::Off};
    MouseEncoding mouseEncoding{MouseEncoding::Default};
};

/// A complete VT100/xterm emulator: feed it bytes, read a screen back.
///
/// The class is GUI-free; `TerminalWidget` renders whatever it exposes. That
/// split is what makes the emulator testable without a running QApplication.
class Terminal : public QObject, private VtHandler {
    Q_OBJECT

public:
    Terminal(int columns, int rows, QObject *parent = nullptr);
    ~Terminal() override;

    /// Feed bytes received from the remote shell.
    void receive(const QByteArray &data);

    void resize(int columns, int rows);
    void reset();

    [[nodiscard]] int columns() const noexcept { return m_columns; }
    [[nodiscard]] int rows() const noexcept { return m_rows; }

    /// The buffer currently displayed (normal or alternate).
    [[nodiscard]] const Screen &screen() const { return *m_active; }
    [[nodiscard]] Screen &screen() { return *m_active; }

    [[nodiscard]] const TerminalModes &modes() const noexcept { return m_modes; }
    [[nodiscard]] const Attributes &currentAttributes() const noexcept { return m_attributes; }
    [[nodiscard]] const QString &title() const noexcept { return m_title; }

    void setScrollbackLimit(int lines);
    [[nodiscard]] int scrollbackLimit() const noexcept { return m_scrollbackLimit; }

    /// Bumped on every change; the widget uses it to skip redundant repaints.
    [[nodiscard]] quint64 revision() const noexcept { return m_active->revision() + m_revisionBase; }

Q_SIGNALS:
    /// Bytes the emulator wants sent back to the host (DA, DSR, ...).
    void reply(const QByteArray &data);
    void titleChanged(const QString &title);
    void bellRang();
    void screenChanged();
    void alternateScreenChanged(bool active);
    void mouseTrackingChanged(arterm::term::MouseTracking tracking);
    void bracketedPasteChanged(bool enabled);
    /// OSC 52: the remote side wants to put `text` on the local clipboard.
    void clipboardWriteRequested(const QString &text);

private:
    // VtHandler.
    void print(char32_t codePoint) override;
    void execute(std::uint8_t control) override;
    void csiDispatch(const CsiSequence &sequence) override;
    void escDispatch(const EscSequence &sequence) override;
    void oscDispatch(const QByteArray &payload) override;

    void applySgr(const CsiSequence &sequence);
    void setMode(const CsiSequence &sequence, bool enabled);
    void setPrivateMode(int mode, bool enabled);
    void useAlternateScreen(bool enabled, bool clearOnEnter);
    void reportDeviceStatus(const CsiSequence &sequence);
    void saveCursor();
    void restoreCursor();
    void softReset();

    /// Translate through the active G0/G1 charset (DEC line drawing).
    [[nodiscard]] char32_t translate(char32_t codePoint) const;

    int m_columns;
    int m_rows;
    int m_scrollbackLimit{10'000};

    std::unique_ptr<Screen> m_normal;
    std::unique_ptr<Screen> m_alternate;
    Screen *m_active{nullptr};

    VtParser m_parser;
    TerminalModes m_modes;
    Attributes m_attributes;
    QString m_title;

    CursorState m_savedCursor;
    CursorState m_savedAlternateCursor;

    /// 0 = US ASCII, 1 = DEC special graphics. Index 0/1 are G0/G1.
    std::array<int, 2> m_charsets{0, 0};
    int m_activeCharset{0};

    /// Offset keeping `revision()` monotonic across a buffer switch.
    quint64 m_revisionBase{0};

    /// Coalesces the `screenChanged` signal to once per batch of input.
    bool m_dirty{false};
};

} // namespace arterm::term

Q_DECLARE_METATYPE(arterm::term::MouseTracking)
