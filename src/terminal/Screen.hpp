#pragma once

#include "terminal/Cell.hpp"

#include <QVector>

#include <deque>
#include <vector>

namespace arterm::term {

using Line = std::vector<Cell>;

/// Cursor position plus the state that DECSC/DECRC must save with it.
struct CursorState {
    int row{0};
    int column{0};
    Attributes attributes;
    bool pendingWrap{false};
    bool originMode{false};
    int charset{0};
};

/// The character grid of one terminal buffer.
///
/// A `Terminal` owns two of these: the normal buffer, which keeps scrollback,
/// and the alternate buffer used by full-screen programs, which does not.
class Screen {
public:
    Screen(int columns, int rows, int scrollbackLimit);

    [[nodiscard]] int columns() const noexcept { return m_columns; }
    [[nodiscard]] int rows() const noexcept { return m_rows; }
    [[nodiscard]] int scrollbackSize() const noexcept { return static_cast<int>(m_scrollback.size()); }
    [[nodiscard]] int scrollbackLimit() const noexcept { return m_scrollbackLimit; }

    void setScrollbackLimit(int lines);

    /// Reflow to a new size. Content is anchored to the bottom, matching what
    /// every other terminal does when a window is resized.
    void resize(int columns, int rows);

    // -- Access ------------------------------------------------------------

    /// `row` is 0-based within the visible grid.
    [[nodiscard]] const Line &line(int row) const;
    [[nodiscard]] Line &line(int row);

    /// Row addressed in "history space": negative values index the scrollback,
    /// with -1 being the most recently scrolled-off line.
    [[nodiscard]] const Line *historyLine(int offset) const;

    [[nodiscard]] const CursorState &cursor() const noexcept { return m_cursor; }
    [[nodiscard]] CursorState &cursor() noexcept { return m_cursor; }

    // -- Writing -----------------------------------------------------------

    /// Place a character at the cursor, honouring wrap and insert mode.
    void writeCharacter(char32_t codePoint, int width, const Attributes &attributes, bool insertMode,
                        bool autoWrap);

    // -- Cursor movement ---------------------------------------------------

    void moveCursor(int row, int column);
    void moveCursorRelative(int rowDelta, int columnDelta);
    void setColumn(int column);
    void setRow(int row);
    void carriageReturn();

    /// LF / IND: down one line, scrolling the region if already at the bottom.
    void index(const Attributes &fill);
    /// RI: up one line, scrolling the region down if already at the top.
    void reverseIndex(const Attributes &fill);
    /// NEL.
    void nextLine(const Attributes &fill);

    // -- Erasing -----------------------------------------------------------

    enum class EraseMode { ToEnd, ToStart, All };

    void eraseInLine(EraseMode mode, const Attributes &fill);
    void eraseInDisplay(EraseMode mode, const Attributes &fill);
    void eraseCharacters(int count, const Attributes &fill);
    void clearScrollback();

    // -- Editing -----------------------------------------------------------

    void insertLines(int count, const Attributes &fill);
    void deleteLines(int count, const Attributes &fill);
    void insertCharacters(int count, const Attributes &fill);
    void deleteCharacters(int count, const Attributes &fill);

    void scrollUp(int count, const Attributes &fill);
    void scrollDown(int count, const Attributes &fill);

    // -- Scroll region -----------------------------------------------------

    void setScrollRegion(int top, int bottom);
    [[nodiscard]] int scrollTop() const noexcept { return m_scrollTop; }
    [[nodiscard]] int scrollBottom() const noexcept { return m_scrollBottom; }
    void resetScrollRegion();

    // -- Tab stops ---------------------------------------------------------

    void setTabStop(int column);
    void clearTabStop(int column);
    void clearAllTabStops();
    void resetTabStops();
    [[nodiscard]] int nextTabStop(int column) const;
    [[nodiscard]] int previousTabStop(int column) const;

    // -- Misc --------------------------------------------------------------

    void reset(const Attributes &fill);
    void fillWith(char32_t codePoint, const Attributes &attributes);

    /// True when the cursor sits one past the last column and the next
    /// printable character must wrap first.
    [[nodiscard]] bool pendingWrap() const noexcept { return m_cursor.pendingWrap; }

    /// Whether `row` was terminated by a wrap rather than a newline. Used when
    /// copying a selection so re-wrapped paragraphs paste as one line.
    [[nodiscard]] bool isLineWrapped(int row) const;
    void setLineWrapped(int row, bool wrapped);

    /// Marks every line as needing a repaint; used after a full reset.
    void markAllDirty() { m_revision++; }
    [[nodiscard]] quint64 revision() const noexcept { return m_revision; }

private:
    [[nodiscard]] Line makeLine(const Attributes &fill) const;
    void clampCursor();

    int m_columns;
    int m_rows;
    int m_scrollbackLimit;

    std::vector<Line> m_lines;
    std::deque<Line> m_scrollback;
    std::vector<bool> m_wrapped;
    std::vector<bool> m_tabStops;

    CursorState m_cursor;
    int m_scrollTop{0};
    int m_scrollBottom{0};
    quint64 m_revision{0};
};

} // namespace arterm::term
