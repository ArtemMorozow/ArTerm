#include "terminal/Terminal.hpp"

#include "terminal/CharWidth.hpp"

#include <algorithm>
#include <array>

namespace arterm::term {
namespace {

/// DEC Special Graphics: maps ASCII 0x5F-0x7E onto the box-drawing glyphs that
/// ncurses relies on for borders.
constexpr char32_t kDecSpecialGraphics[] = {
    0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B,
    0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C,
    0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
};

constexpr Screen::EraseMode eraseModeFor(int parameter)
{
    switch (parameter) {
    case 1:
        return Screen::EraseMode::ToStart;
    case 2:
    case 3:
        return Screen::EraseMode::All;
    default:
        return Screen::EraseMode::ToEnd;
    }
}

} // namespace

Terminal::Terminal(int columns, int rows, QObject *parent)
    : QObject(parent)
    , m_columns(std::max(1, columns))
    , m_rows(std::max(1, rows))
    , m_parser(*this)
{
    m_normal = std::make_unique<Screen>(m_columns, m_rows, m_scrollbackLimit);
    m_alternate = std::make_unique<Screen>(m_columns, m_rows, 0);
    m_active = m_normal.get();
}

Terminal::~Terminal() = default;

void Terminal::receive(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    m_dirty = false;
    m_parser.parse(data);

    // One repaint per chunk instead of one per glyph: a `cat` of a large file
    // would otherwise spend all its time in the renderer.
    Q_EMIT screenChanged();
}

void Terminal::resize(int columns, int rows)
{
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (columns == m_columns && rows == m_rows)
        return;

    m_columns = columns;
    m_rows = rows;
    m_normal->resize(columns, rows);
    m_alternate->resize(columns, rows);

    Q_EMIT screenChanged();
}

void Terminal::setScrollbackLimit(int lines)
{
    m_scrollbackLimit = std::max(0, lines);
    m_normal->setScrollbackLimit(m_scrollbackLimit);
    Q_EMIT screenChanged();
}

void Terminal::reset()
{
    m_attributes.reset();
    m_modes = TerminalModes{};
    m_charsets = {0, 0};
    m_activeCharset = 0;
    m_savedCursor = CursorState{};
    m_savedAlternateCursor = CursorState{};

    m_parser.reset();
    m_normal->reset(m_attributes);
    m_normal->clearScrollback();
    m_alternate->reset(m_attributes);
    m_active = m_normal.get();

    m_title.clear();

    Q_EMIT alternateScreenChanged(false);
    Q_EMIT mouseTrackingChanged(MouseTracking::Off);
    Q_EMIT bracketedPasteChanged(false);
    Q_EMIT screenChanged();
}

void Terminal::softReset()
{
    m_attributes.reset();
    m_modes.originMode = false;
    m_modes.insertMode = false;
    m_modes.autoWrap = true;
    m_modes.cursorVisible = true;
    m_charsets = {0, 0};
    m_activeCharset = 0;
    m_active->resetScrollRegion();
    m_active->moveCursor(0, 0);
}

char32_t Terminal::translate(char32_t codePoint) const
{
    if (m_charsets[static_cast<std::size_t>(m_activeCharset)] != 1)
        return codePoint;
    if (codePoint < 0x5F || codePoint > 0x7E)
        return codePoint;
    return kDecSpecialGraphics[codePoint - 0x5F];
}

// ---------------------------------------------------------------------------
// VtHandler
// ---------------------------------------------------------------------------

void Terminal::print(char32_t codePoint)
{
    const char32_t glyph = translate(codePoint);
    const int width = characterWidth(glyph);

    if (width == 0) {
        // A combining mark attaches to the cell to the left of the cursor.
        const int column = m_active->cursor().pendingWrap ? m_active->cursor().column
                                                          : m_active->cursor().column - 1;
        if (column >= 0) {
            Line &target = m_active->line(m_active->cursor().row);
            Cell &cell = target[static_cast<std::size_t>(column)];
            // Only variation selectors and the like are dropped; a real mark
            // would need a per-cell string, which the grid deliberately avoids.
            if (glyph >= 0xFE00 && glyph <= 0xFE0F)
                return;
            if (cell.character == U' ')
                cell.character = glyph;
        }
        return;
    }

    m_active->writeCharacter(glyph, width, m_attributes, m_modes.insertMode, m_modes.autoWrap);
    m_dirty = true;
}

void Terminal::execute(std::uint8_t control)
{
    Screen &screen = *m_active;

    switch (control) {
    case 0x07: // BEL
        Q_EMIT bellRang();
        break;
    case 0x08: // BS
        if (screen.cursor().pendingWrap)
            screen.cursor().pendingWrap = false;
        else if (screen.cursor().column > 0)
            screen.moveCursorRelative(0, -1);
        break;
    case 0x09: // HT
        screen.setColumn(screen.nextTabStop(screen.cursor().column));
        break;
    case 0x0A: // LF
    case 0x0B: // VT
    case 0x0C: // FF
        screen.index(m_attributes);
        if (m_modes.newLineMode)
            screen.carriageReturn();
        break;
    case 0x0D: // CR
        screen.carriageReturn();
        break;
    case 0x0E: // SO - select G1
        m_activeCharset = 1;
        break;
    case 0x0F: // SI - select G0
        m_activeCharset = 0;
        break;
    default:
        break;
    }

    m_dirty = true;
}

void Terminal::escDispatch(const EscSequence &sequence)
{
    Screen &screen = *m_active;

    // Charset designation: ESC ( <set> for G0, ESC ) <set> for G1.
    if (sequence.intermediate == '(' || sequence.intermediate == ')') {
        const int slot = (sequence.intermediate == '(') ? 0 : 1;
        m_charsets[static_cast<std::size_t>(slot)] = (sequence.final == '0') ? 1 : 0;
        return;
    }

    switch (sequence.final) {
    case 'D': // IND
        screen.index(m_attributes);
        break;
    case 'E': // NEL
        screen.nextLine(m_attributes);
        break;
    case 'H': // HTS
        screen.setTabStop(screen.cursor().column);
        break;
    case 'M': // RI
        screen.reverseIndex(m_attributes);
        break;
    case '7': // DECSC
        saveCursor();
        break;
    case '8':
        if (sequence.intermediate == '#') {
            // DECALN: fill the screen with 'E', used by test suites.
            screen.fillWith(U'E', m_attributes);
        } else {
            restoreCursor();
        }
        break;
    case '=': // DECKPAM
        m_modes.applicationKeypad = true;
        break;
    case '>': // DECKPNM
        m_modes.applicationKeypad = false;
        break;
    case 'c': // RIS
        reset();
        break;
    default:
        break;
    }

    m_dirty = true;
}

void Terminal::csiDispatch(const CsiSequence &sequence)
{
    Screen &screen = *m_active;
    const bool isPrivate = sequence.privateMarker == '?';

    switch (sequence.final) {
    case '@': // ICH
        screen.insertCharacters(sequence.positiveParameter(0), m_attributes);
        break;

    case 'A': // CUU
        screen.moveCursorRelative(-sequence.positiveParameter(0), 0);
        break;
    case 'B': // CUD
    case 'e': // VPR
        screen.moveCursorRelative(sequence.positiveParameter(0), 0);
        break;
    case 'C': // CUF
    case 'a': // HPR
        screen.moveCursorRelative(0, sequence.positiveParameter(0));
        break;
    case 'D': // CUB
        screen.moveCursorRelative(0, -sequence.positiveParameter(0));
        break;

    case 'E': // CNL
        screen.setRow(screen.cursor().row + sequence.positiveParameter(0));
        screen.carriageReturn();
        break;
    case 'F': // CPL
        screen.setRow(screen.cursor().row - sequence.positiveParameter(0));
        screen.carriageReturn();
        break;

    case 'G': // CHA
    case '`': // HPA
        screen.setColumn(sequence.positiveParameter(0) - 1);
        break;
    case 'd': // VPA
        screen.setRow(sequence.positiveParameter(0) - 1);
        break;

    case 'H': // CUP
    case 'f': { // HVP
        int row = sequence.positiveParameter(0) - 1;
        const int column = sequence.positiveParameter(1) - 1;
        if (m_modes.originMode)
            row += screen.scrollTop();
        screen.moveCursor(row, column);
        break;
    }

    case 'I': // CHT
        for (int i = 0; i < sequence.positiveParameter(0); ++i)
            screen.setColumn(screen.nextTabStop(screen.cursor().column));
        break;
    case 'Z': // CBT
        for (int i = 0; i < sequence.positiveParameter(0); ++i)
            screen.setColumn(screen.previousTabStop(screen.cursor().column));
        break;

    case 'J': // ED
        screen.eraseInDisplay(eraseModeFor(sequence.parameter(0, 0)), m_attributes);
        if (sequence.parameter(0, 0) == 3)
            screen.clearScrollback();
        break;
    case 'K': // EL
        screen.eraseInLine(eraseModeFor(sequence.parameter(0, 0)), m_attributes);
        break;

    case 'L': // IL
        screen.insertLines(sequence.positiveParameter(0), m_attributes);
        break;
    case 'M': // DL
        screen.deleteLines(sequence.positiveParameter(0), m_attributes);
        break;
    case 'P': // DCH
        screen.deleteCharacters(sequence.positiveParameter(0), m_attributes);
        break;
    case 'X': // ECH
        screen.eraseCharacters(sequence.positiveParameter(0), m_attributes);
        break;

    case 'S': // SU
        screen.scrollUp(sequence.positiveParameter(0), m_attributes);
        break;
    case 'T': // SD
        screen.scrollDown(sequence.positiveParameter(0), m_attributes);
        break;

    case 'c': // DA
        if (!isPrivate) {
            // "VT220 with 132 columns, selective erase and colour".
            Q_EMIT reply(QByteArrayLiteral("\033[?62;1;6;9;15;22c"));
        }
        break;

    case 'g': // TBC
        if (sequence.parameter(0, 0) == 3)
            screen.clearAllTabStops();
        else
            screen.clearTabStop(screen.cursor().column);
        break;

    case 'h': // SM / DECSET
        setMode(sequence, true);
        break;
    case 'l': // RM / DECRST
        setMode(sequence, false);
        break;

    case 'm': // SGR
        applySgr(sequence);
        break;

    case 'n': // DSR
        reportDeviceStatus(sequence);
        break;

    case 'p':
        if (sequence.intermediate == '!') // DECSTR - soft reset.
            softReset();
        break;

    case 'q':
        // DECSCUSR: cursor shape. The widget always draws a block, so the
        // request is accepted and ignored rather than echoed back.
        break;

    case 'r': // DECSTBM
        if (isPrivate)
            break;
        if (sequence.parameterCount == 0) {
            screen.resetScrollRegion();
        } else {
            screen.setScrollRegion(sequence.positiveParameter(0) - 1,
                                   sequence.positiveParameter(1, m_rows) - 1);
        }
        screen.moveCursor(m_modes.originMode ? screen.scrollTop() : 0, 0);
        break;

    case 's': // Save cursor (ANSI.SYS style).
        saveCursor();
        break;
    case 'u':
        restoreCursor();
        break;

    case 't':
        // Window manipulation. Only the size report is answered; resizing the
        // window from the remote side is deliberately not honoured.
        if (sequence.parameter(0, 0) == 18) {
            Q_EMIT reply(QStringLiteral("\033[8;%1;%2t").arg(m_rows).arg(m_columns).toLatin1());
        }
        break;

    default:
        break;
    }

    m_dirty = true;
}

void Terminal::applySgr(const CsiSequence &sequence)
{
    if (sequence.parameterCount == 0) {
        m_attributes.reset();
        return;
    }

    for (int i = 0; i < sequence.parameterCount; ++i) {
        const int code = sequence.parameter(i, 0);

        switch (code) {
        case 0:
            m_attributes.reset();
            break;
        case 1:
            m_attributes.flags |= CellFlag::Bold;
            break;
        case 2:
            m_attributes.flags |= CellFlag::Faint;
            break;
        case 3:
            m_attributes.flags |= CellFlag::Italic;
            break;
        case 4:
            m_attributes.flags |= CellFlag::Underline;
            break;
        case 5:
        case 6:
            m_attributes.flags |= CellFlag::Blink;
            break;
        case 7:
            m_attributes.flags |= CellFlag::Inverse;
            break;
        case 8:
            m_attributes.flags |= CellFlag::Hidden;
            break;
        case 9:
            m_attributes.flags |= CellFlag::Strikeout;
            break;
        case 21:
            m_attributes.flags |= CellFlag::DoubleUnderline;
            break;
        case 22:
            m_attributes.flags &= ~(CellFlag::Bold | CellFlag::Faint);
            break;
        case 23:
            m_attributes.flags &= ~CellFlag::Italic;
            break;
        case 24:
            m_attributes.flags &= ~(CellFlag::Underline | CellFlag::DoubleUnderline);
            break;
        case 25:
            m_attributes.flags &= ~CellFlag::Blink;
            break;
        case 27:
            m_attributes.flags &= ~CellFlag::Inverse;
            break;
        case 28:
            m_attributes.flags &= ~CellFlag::Hidden;
            break;
        case 29:
            m_attributes.flags &= ~CellFlag::Strikeout;
            break;

        case 39:
            m_attributes.foreground = Color::defaultColor();
            break;
        case 49:
            m_attributes.background = Color::defaultColor();
            break;
        case 59:
            m_attributes.underlineColor = Color::defaultColor();
            break;

        case 38:
        case 48:
        case 58: {
            // Extended colour: 5;<index> or 2;<r>;<g>;<b>. Both the ';' and the
            // ':' separated forms arrive as flat parameters here.
            Color parsed;
            const int selector = sequence.parameter(i + 1, -1);

            if (selector == 5 && i + 2 < sequence.parameterCount) {
                parsed = Color::indexed(static_cast<std::uint8_t>(
                    std::clamp(sequence.parameter(i + 2, 0), 0, 255)));
                i += 2;
            } else if (selector == 2 && i + 4 < sequence.parameterCount) {
                parsed = Color::rgb(
                    static_cast<std::uint8_t>(std::clamp(sequence.parameter(i + 2, 0), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(sequence.parameter(i + 3, 0), 0, 255)),
                    static_cast<std::uint8_t>(std::clamp(sequence.parameter(i + 4, 0), 0, 255)));
                i += 4;
            } else {
                // Malformed: skip the selector and carry on rather than
                // misreading the rest of the sequence as attributes.
                i += 1;
                break;
            }

            if (code == 38)
                m_attributes.foreground = parsed;
            else if (code == 48)
                m_attributes.background = parsed;
            else
                m_attributes.underlineColor = parsed;
            break;
        }

        default:
            if (code >= 30 && code <= 37)
                m_attributes.foreground = Color::indexed(static_cast<std::uint8_t>(code - 30));
            else if (code >= 40 && code <= 47)
                m_attributes.background = Color::indexed(static_cast<std::uint8_t>(code - 40));
            else if (code >= 90 && code <= 97)
                m_attributes.foreground = Color::indexed(static_cast<std::uint8_t>(code - 90 + 8));
            else if (code >= 100 && code <= 107)
                m_attributes.background = Color::indexed(static_cast<std::uint8_t>(code - 100 + 8));
            break;
        }
    }
}

void Terminal::setMode(const CsiSequence &sequence, bool enabled)
{
    if (sequence.privateMarker == '?') {
        for (int i = 0; i < sequence.parameterCount; ++i)
            setPrivateMode(sequence.parameter(i, 0), enabled);
        return;
    }

    for (int i = 0; i < sequence.parameterCount; ++i) {
        switch (sequence.parameter(i, 0)) {
        case 4: // IRM
            m_modes.insertMode = enabled;
            break;
        case 20: // LNM
            m_modes.newLineMode = enabled;
            break;
        default:
            break;
        }
    }
}

void Terminal::setPrivateMode(int mode, bool enabled)
{
    switch (mode) {
    case 1: // DECCKM
        m_modes.applicationCursorKeys = enabled;
        break;
    case 3: // DECCOLM - clears the screen as a side effect.
        m_active->eraseInDisplay(Screen::EraseMode::All, m_attributes);
        m_active->moveCursor(0, 0);
        break;
    case 6: // DECOM
        m_modes.originMode = enabled;
        m_active->moveCursor(enabled ? m_active->scrollTop() : 0, 0);
        break;
    case 7: // DECAWM
        m_modes.autoWrap = enabled;
        break;
    case 12: // Cursor blink.
        break;
    case 25: // DECTCEM
        m_modes.cursorVisible = enabled;
        break;

    case 5: // DECSCNM
        m_modes.reverseVideo = enabled;
        break;

    case 9:
        m_modes.mouseTracking = enabled ? MouseTracking::X10 : MouseTracking::Off;
        Q_EMIT mouseTrackingChanged(m_modes.mouseTracking);
        break;
    case 1000:
        m_modes.mouseTracking = enabled ? MouseTracking::Normal : MouseTracking::Off;
        Q_EMIT mouseTrackingChanged(m_modes.mouseTracking);
        break;
    case 1002:
        m_modes.mouseTracking = enabled ? MouseTracking::ButtonEvent : MouseTracking::Off;
        Q_EMIT mouseTrackingChanged(m_modes.mouseTracking);
        break;
    case 1003:
        m_modes.mouseTracking = enabled ? MouseTracking::AnyEvent : MouseTracking::Off;
        Q_EMIT mouseTrackingChanged(m_modes.mouseTracking);
        break;

    case 1004:
        m_modes.focusReporting = enabled;
        break;

    case 1005:
        m_modes.mouseEncoding = enabled ? MouseEncoding::Utf8 : MouseEncoding::Default;
        break;
    case 1006:
        m_modes.mouseEncoding = enabled ? MouseEncoding::Sgr : MouseEncoding::Default;
        break;
    case 1015:
        m_modes.mouseEncoding = enabled ? MouseEncoding::Urxvt : MouseEncoding::Default;
        break;

    case 47:
    case 1047:
        useAlternateScreen(enabled, /*clearOnEnter=*/false);
        break;
    case 1048:
        if (enabled)
            saveCursor();
        else
            restoreCursor();
        break;
    case 1049:
        // The combined form: save the cursor, switch, and clear.
        if (enabled)
            saveCursor();
        useAlternateScreen(enabled, /*clearOnEnter=*/true);
        if (!enabled)
            restoreCursor();
        break;

    case 2004:
        m_modes.bracketedPaste = enabled;
        Q_EMIT bracketedPasteChanged(enabled);
        break;

    default:
        break;
    }
}

void Terminal::useAlternateScreen(bool enabled, bool clearOnEnter)
{
    if (m_modes.alternateScreen == enabled)
        return;

    // Keep revision() monotonic so the widget never mistakes a buffer switch
    // for "nothing changed".
    m_revisionBase += m_active->revision() + 1;

    m_modes.alternateScreen = enabled;
    m_active = enabled ? m_alternate.get() : m_normal.get();

    if (enabled && clearOnEnter) {
        m_alternate->reset(m_attributes);
        m_alternate->moveCursor(0, 0);
    }

    Q_EMIT alternateScreenChanged(enabled);
}

void Terminal::reportDeviceStatus(const CsiSequence &sequence)
{
    switch (sequence.parameter(0, 0)) {
    case 5: // Terminal status: always "OK".
        Q_EMIT reply(QByteArrayLiteral("\033[0n"));
        break;
    case 6: { // CPR
        const CursorState &cursor = m_active->cursor();
        int row = cursor.row + 1;
        if (m_modes.originMode)
            row -= m_active->scrollTop();
        Q_EMIT reply(QStringLiteral("\033[%1;%2R").arg(row).arg(cursor.column + 1).toLatin1());
        break;
    }
    default:
        break;
    }
}

void Terminal::saveCursor()
{
    CursorState state = m_active->cursor();
    state.attributes = m_attributes;
    state.originMode = m_modes.originMode;
    state.charset = m_activeCharset;

    if (m_modes.alternateScreen)
        m_savedAlternateCursor = state;
    else
        m_savedCursor = state;
}

void Terminal::restoreCursor()
{
    const CursorState &state = m_modes.alternateScreen ? m_savedAlternateCursor : m_savedCursor;

    m_attributes = state.attributes;
    m_modes.originMode = state.originMode;
    m_activeCharset = state.charset;
    m_active->moveCursor(state.row, state.column);
}

void Terminal::oscDispatch(const QByteArray &payload)
{
    const int separator = payload.indexOf(';');
    if (separator < 0)
        return;

    bool ok = false;
    const int command = payload.left(separator).toInt(&ok);
    if (!ok)
        return;

    const QByteArray argument = payload.mid(separator + 1);

    switch (command) {
    case 0: // Icon name and window title.
    case 1: // Icon name.
    case 2: { // Window title.
        // Remote titles are untrusted text: strip controls so a hostile host
        // cannot smuggle escape sequences into the tab bar.
        QString title = QString::fromUtf8(argument);
        title.removeIf([](QChar character) { return character.isNonCharacter() || character < u' '; });
        title.truncate(256);

        if (command != 1 && title != m_title) {
            m_title = title;
            Q_EMIT titleChanged(m_title);
        }
        break;
    }

    case 52: { // Clipboard access.
        const int comma = argument.indexOf(';');
        if (comma < 0)
            break;
        const QByteArray encoded = argument.mid(comma + 1);
        if (encoded == "?")
            break; // Reading the local clipboard from the remote side is refused.

        const auto decoded = QByteArray::fromBase64Encoding(encoded);
        if (decoded.decodingStatus == QByteArray::Base64DecodingStatus::Ok)
            Q_EMIT clipboardWriteRequested(QString::fromUtf8(decoded.decoded));
        break;
    }

    default:
        break;
    }

    m_dirty = true;
}

} // namespace arterm::term
