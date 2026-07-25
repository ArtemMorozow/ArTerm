#include "terminal/Screen.hpp"

#include <algorithm>

namespace arterm::term {
namespace {

constexpr int kTabWidth = 8;

Cell blankCell(const Attributes &attributes)
{
    Cell cell;
    cell.character = U' ';
    cell.attributes = attributes;
    // A blank inherits the background but never the glyph decorations.
    cell.attributes.flags &= ~(CellFlag::Underline | CellFlag::DoubleUnderline | CellFlag::Strikeout
                               | CellFlag::WideLead | CellFlag::WideTrail);
    return cell;
}

} // namespace

Screen::Screen(int columns, int rows, int scrollbackLimit)
    : m_columns(std::max(1, columns))
    , m_rows(std::max(1, rows))
    , m_scrollbackLimit(std::max(0, scrollbackLimit))
{
    m_lines.assign(static_cast<std::size_t>(m_rows), makeLine(Attributes{}));
    m_wrapped.assign(static_cast<std::size_t>(m_rows), false);
    m_scrollBottom = m_rows - 1;
    resetTabStops();
}

Line Screen::makeLine(const Attributes &fill) const
{
    return Line(static_cast<std::size_t>(m_columns), blankCell(fill));
}

const Line &Screen::line(int row) const
{
    const int clamped = std::clamp(row, 0, m_rows - 1);
    return m_lines[static_cast<std::size_t>(clamped)];
}

Line &Screen::line(int row)
{
    const int clamped = std::clamp(row, 0, m_rows - 1);
    return m_lines[static_cast<std::size_t>(clamped)];
}

const Line *Screen::historyLine(int offset) const
{
    if (offset >= 0)
        return offset < m_rows ? &m_lines[static_cast<std::size_t>(offset)] : nullptr;

    const int index = static_cast<int>(m_scrollback.size()) + offset;
    if (index < 0 || index >= static_cast<int>(m_scrollback.size()))
        return nullptr;
    return &m_scrollback[static_cast<std::size_t>(index)];
}

bool Screen::isLineWrapped(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_wrapped.size()))
        return false;
    return m_wrapped[static_cast<std::size_t>(row)];
}

void Screen::setLineWrapped(int row, bool wrapped)
{
    if (row < 0 || row >= static_cast<int>(m_wrapped.size()))
        return;
    m_wrapped[static_cast<std::size_t>(row)] = wrapped;
}

void Screen::setScrollbackLimit(int lines)
{
    m_scrollbackLimit = std::max(0, lines);
    while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit)
        m_scrollback.pop_front();
    ++m_revision;
}

void Screen::resize(int columns, int rows)
{
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (columns == m_columns && rows == m_rows)
        return;

    const int oldRows = m_rows;

    if (columns != m_columns) {
        const Cell filler = blankCell(Attributes{});
        for (Line &existing : m_lines)
            existing.resize(static_cast<std::size_t>(columns), filler);
        for (Line &existing : m_scrollback)
            existing.resize(static_cast<std::size_t>(columns), filler);
        m_columns = columns;
    }

    if (rows < oldRows) {
        // Shrinking: push the lines that fall off the top into scrollback so
        // the content the user was reading is not simply destroyed.
        const int surplus = oldRows - rows;

        // Keep the cursor line visible when it sits near the bottom.
        const int cursorSlack = std::max(0, m_cursor.row - (rows - 1));
        const int toScroll = std::min(surplus, std::max(cursorSlack, surplus));

        for (int i = 0; i < toScroll; ++i) {
            if (m_scrollbackLimit > 0) {
                m_scrollback.push_back(std::move(m_lines.front()));
                while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit)
                    m_scrollback.pop_front();
            }
            m_lines.erase(m_lines.begin());
            m_wrapped.erase(m_wrapped.begin());
            m_cursor.row = std::max(0, m_cursor.row - 1);
        }

        m_lines.resize(static_cast<std::size_t>(rows));
        m_wrapped.resize(static_cast<std::size_t>(rows), false);
    } else if (rows > oldRows) {
        // Growing: pull lines back out of scrollback before padding with blanks
        // so the window "unrolls" the history instead of showing empty space.
        int deficit = rows - oldRows;

        while (deficit > 0 && !m_scrollback.empty()) {
            m_lines.insert(m_lines.begin(), std::move(m_scrollback.back()));
            m_scrollback.pop_back();
            m_wrapped.insert(m_wrapped.begin(), false);
            m_cursor.row++;
            --deficit;
        }

        for (int i = 0; i < deficit; ++i) {
            m_lines.push_back(makeLine(Attributes{}));
            m_wrapped.push_back(false);
        }
    }

    m_rows = rows;
    m_lines.resize(static_cast<std::size_t>(m_rows), makeLine(Attributes{}));
    m_wrapped.resize(static_cast<std::size_t>(m_rows), false);

    resetScrollRegion();
    resetTabStops();
    clampCursor();
    ++m_revision;
}

void Screen::clampCursor()
{
    m_cursor.row = std::clamp(m_cursor.row, 0, m_rows - 1);
    m_cursor.column = std::clamp(m_cursor.column, 0, m_columns - 1);
    m_cursor.pendingWrap = false;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

void Screen::writeCharacter(char32_t codePoint, int width, const Attributes &attributes,
                            bool insertMode, bool autoWrap)
{
    if (width <= 0)
        width = 1;

    if (m_cursor.pendingWrap && autoWrap) {
        setLineWrapped(m_cursor.row, true);
        m_cursor.column = 0;
        index(attributes);
        m_cursor.pendingWrap = false;
    }

    // A double-width glyph never straddles the right edge.
    if (width == 2 && m_cursor.column == m_columns - 1) {
        if (autoWrap) {
            setLineWrapped(m_cursor.row, true);
            m_cursor.column = 0;
            index(attributes);
        } else {
            return;
        }
    }

    Line &target = line(m_cursor.row);

    if (insertMode) {
        const auto position = static_cast<std::size_t>(m_cursor.column);
        for (int i = 0; i < width; ++i) {
            target.insert(target.begin() + static_cast<std::ptrdiff_t>(position), blankCell(attributes));
            target.pop_back();
        }
    }

    // Overwriting half of an existing wide glyph must blank its other half,
    // otherwise a stale trailer is left on screen.
    const auto blankPartner = [&](int column) {
        if (column < 0 || column >= m_columns)
            return;
        Cell &cell = target[static_cast<std::size_t>(column)];
        if (hasFlag(cell.attributes.flags, CellFlag::WideTrail) && column > 0) {
            Cell &lead = target[static_cast<std::size_t>(column - 1)];
            lead = blankCell(lead.attributes);
        } else if (hasFlag(cell.attributes.flags, CellFlag::WideLead) && column + 1 < m_columns) {
            Cell &trail = target[static_cast<std::size_t>(column + 1)];
            trail = blankCell(trail.attributes);
        }
    };
    blankPartner(m_cursor.column);
    if (width == 2)
        blankPartner(m_cursor.column + 1);

    Cell &cell = target[static_cast<std::size_t>(m_cursor.column)];
    cell.character = codePoint;
    cell.attributes = attributes;
    if (width == 2) {
        cell.attributes.flags |= CellFlag::WideLead;

        Cell &trailer = target[static_cast<std::size_t>(m_cursor.column + 1)];
        trailer.character = U'\0';
        trailer.attributes = attributes;
        trailer.attributes.flags |= CellFlag::WideTrail;
    }

    m_cursor.column += width;
    if (m_cursor.column >= m_columns) {
        m_cursor.column = m_columns - 1;
        m_cursor.pendingWrap = true;
    } else {
        m_cursor.pendingWrap = false;
    }

    ++m_revision;
}

// ---------------------------------------------------------------------------
// Cursor movement
// ---------------------------------------------------------------------------

void Screen::moveCursor(int row, int column)
{
    m_cursor.row = std::clamp(row, 0, m_rows - 1);
    m_cursor.column = std::clamp(column, 0, m_columns - 1);
    m_cursor.pendingWrap = false;
}

void Screen::moveCursorRelative(int rowDelta, int columnDelta)
{
    moveCursor(m_cursor.row + rowDelta, m_cursor.column + columnDelta);
}

void Screen::setColumn(int column)
{
    m_cursor.column = std::clamp(column, 0, m_columns - 1);
    m_cursor.pendingWrap = false;
}

void Screen::setRow(int row)
{
    m_cursor.row = std::clamp(row, 0, m_rows - 1);
    m_cursor.pendingWrap = false;
}

void Screen::carriageReturn()
{
    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
}

void Screen::index(const Attributes &fill)
{
    if (m_cursor.row == m_scrollBottom) {
        scrollUp(1, fill);
    } else if (m_cursor.row < m_rows - 1) {
        ++m_cursor.row;
    }
    m_cursor.pendingWrap = false;
}

void Screen::reverseIndex(const Attributes &fill)
{
    if (m_cursor.row == m_scrollTop) {
        scrollDown(1, fill);
    } else if (m_cursor.row > 0) {
        --m_cursor.row;
    }
    m_cursor.pendingWrap = false;
}

void Screen::nextLine(const Attributes &fill)
{
    carriageReturn();
    index(fill);
}

// ---------------------------------------------------------------------------
// Erasing
// ---------------------------------------------------------------------------

void Screen::eraseInLine(EraseMode mode, const Attributes &fill)
{
    Line &target = line(m_cursor.row);
    const Cell filler = blankCell(fill);

    switch (mode) {
    case EraseMode::ToEnd:
        std::fill(target.begin() + m_cursor.column, target.end(), filler);
        break;
    case EraseMode::ToStart:
        std::fill(target.begin(), target.begin() + std::min(m_cursor.column + 1, m_columns), filler);
        break;
    case EraseMode::All:
        std::fill(target.begin(), target.end(), filler);
        break;
    }

    setLineWrapped(m_cursor.row, false);
    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::eraseInDisplay(EraseMode mode, const Attributes &fill)
{
    const Cell filler = blankCell(fill);

    switch (mode) {
    case EraseMode::ToEnd:
        eraseInLine(EraseMode::ToEnd, fill);
        for (int row = m_cursor.row + 1; row < m_rows; ++row) {
            std::fill(m_lines[static_cast<std::size_t>(row)].begin(),
                      m_lines[static_cast<std::size_t>(row)].end(), filler);
            setLineWrapped(row, false);
        }
        break;

    case EraseMode::ToStart:
        for (int row = 0; row < m_cursor.row; ++row) {
            std::fill(m_lines[static_cast<std::size_t>(row)].begin(),
                      m_lines[static_cast<std::size_t>(row)].end(), filler);
            setLineWrapped(row, false);
        }
        eraseInLine(EraseMode::ToStart, fill);
        break;

    case EraseMode::All:
        for (int row = 0; row < m_rows; ++row) {
            std::fill(m_lines[static_cast<std::size_t>(row)].begin(),
                      m_lines[static_cast<std::size_t>(row)].end(), filler);
            setLineWrapped(row, false);
        }
        break;
    }

    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::eraseCharacters(int count, const Attributes &fill)
{
    count = std::max(1, count);
    Line &target = line(m_cursor.row);
    const int end = std::min(m_cursor.column + count, m_columns);
    std::fill(target.begin() + m_cursor.column, target.begin() + end, blankCell(fill));
    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::clearScrollback()
{
    m_scrollback.clear();
    ++m_revision;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void Screen::insertLines(int count, const Attributes &fill)
{
    if (m_cursor.row < m_scrollTop || m_cursor.row > m_scrollBottom)
        return;

    count = std::clamp(count, 1, m_scrollBottom - m_cursor.row + 1);

    for (int i = 0; i < count; ++i) {
        m_lines.erase(m_lines.begin() + m_scrollBottom);
        m_wrapped.erase(m_wrapped.begin() + m_scrollBottom);
        m_lines.insert(m_lines.begin() + m_cursor.row, makeLine(fill));
        m_wrapped.insert(m_wrapped.begin() + m_cursor.row, false);
    }

    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::deleteLines(int count, const Attributes &fill)
{
    if (m_cursor.row < m_scrollTop || m_cursor.row > m_scrollBottom)
        return;

    count = std::clamp(count, 1, m_scrollBottom - m_cursor.row + 1);

    for (int i = 0; i < count; ++i) {
        m_lines.erase(m_lines.begin() + m_cursor.row);
        m_wrapped.erase(m_wrapped.begin() + m_cursor.row);
        m_lines.insert(m_lines.begin() + m_scrollBottom, makeLine(fill));
        m_wrapped.insert(m_wrapped.begin() + m_scrollBottom, false);
    }

    m_cursor.column = 0;
    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::insertCharacters(int count, const Attributes &fill)
{
    count = std::clamp(count, 1, m_columns - m_cursor.column);
    Line &target = line(m_cursor.row);

    for (int i = 0; i < count; ++i) {
        target.insert(target.begin() + m_cursor.column, blankCell(fill));
        target.pop_back();
    }

    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::deleteCharacters(int count, const Attributes &fill)
{
    count = std::clamp(count, 1, m_columns - m_cursor.column);
    Line &target = line(m_cursor.row);

    for (int i = 0; i < count; ++i) {
        target.erase(target.begin() + m_cursor.column);
        target.push_back(blankCell(fill));
    }

    m_cursor.pendingWrap = false;
    ++m_revision;
}

void Screen::scrollUp(int count, const Attributes &fill)
{
    count = std::clamp(count, 1, m_scrollBottom - m_scrollTop + 1);

    // Only a full-height region feeds the scrollback; a program that set a
    // smaller region is drawing a pane, not producing history.
    const bool feedsScrollback = (m_scrollTop == 0 && m_scrollBottom == m_rows - 1);

    for (int i = 0; i < count; ++i) {
        if (feedsScrollback && m_scrollbackLimit > 0) {
            m_scrollback.push_back(std::move(m_lines[static_cast<std::size_t>(m_scrollTop)]));
            while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit)
                m_scrollback.pop_front();
        }

        m_lines.erase(m_lines.begin() + m_scrollTop);
        m_wrapped.erase(m_wrapped.begin() + m_scrollTop);
        m_lines.insert(m_lines.begin() + m_scrollBottom, makeLine(fill));
        m_wrapped.insert(m_wrapped.begin() + m_scrollBottom, false);
    }

    ++m_revision;
}

void Screen::scrollDown(int count, const Attributes &fill)
{
    count = std::clamp(count, 1, m_scrollBottom - m_scrollTop + 1);

    for (int i = 0; i < count; ++i) {
        m_lines.erase(m_lines.begin() + m_scrollBottom);
        m_wrapped.erase(m_wrapped.begin() + m_scrollBottom);
        m_lines.insert(m_lines.begin() + m_scrollTop, makeLine(fill));
        m_wrapped.insert(m_wrapped.begin() + m_scrollTop, false);
    }

    ++m_revision;
}

// ---------------------------------------------------------------------------
// Scroll region and tab stops
// ---------------------------------------------------------------------------

void Screen::setScrollRegion(int top, int bottom)
{
    top = std::clamp(top, 0, m_rows - 1);
    bottom = std::clamp(bottom, 0, m_rows - 1);

    if (top >= bottom) {
        resetScrollRegion();
        return;
    }

    m_scrollTop = top;
    m_scrollBottom = bottom;
}

void Screen::resetScrollRegion()
{
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
}

void Screen::resetTabStops()
{
    m_tabStops.assign(static_cast<std::size_t>(m_columns), false);
    for (int column = kTabWidth; column < m_columns; column += kTabWidth)
        m_tabStops[static_cast<std::size_t>(column)] = true;
}

void Screen::setTabStop(int column)
{
    if (column >= 0 && column < m_columns)
        m_tabStops[static_cast<std::size_t>(column)] = true;
}

void Screen::clearTabStop(int column)
{
    if (column >= 0 && column < m_columns)
        m_tabStops[static_cast<std::size_t>(column)] = false;
}

void Screen::clearAllTabStops()
{
    m_tabStops.assign(static_cast<std::size_t>(m_columns), false);
}

int Screen::nextTabStop(int column) const
{
    for (int i = column + 1; i < m_columns; ++i) {
        if (m_tabStops[static_cast<std::size_t>(i)])
            return i;
    }
    return m_columns - 1;
}

int Screen::previousTabStop(int column) const
{
    for (int i = column - 1; i > 0; --i) {
        if (m_tabStops[static_cast<std::size_t>(i)])
            return i;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

void Screen::reset(const Attributes &fill)
{
    m_lines.assign(static_cast<std::size_t>(m_rows), makeLine(fill));
    m_wrapped.assign(static_cast<std::size_t>(m_rows), false);
    m_cursor = CursorState{};
    resetScrollRegion();
    resetTabStops();
    ++m_revision;
}

void Screen::fillWith(char32_t codePoint, const Attributes &attributes)
{
    Cell cell;
    cell.character = codePoint;
    cell.attributes = attributes;

    for (Line &target : m_lines)
        std::fill(target.begin(), target.end(), cell);

    ++m_revision;
}

} // namespace arterm::term
