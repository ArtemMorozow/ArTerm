#include "terminal/TerminalWidget.hpp"

#include "terminal/CharWidth.hpp"
#include "terminal/KeyEncoder.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace arterm::term {
namespace {

constexpr int kCursorBlinkIntervalMs = 530;
constexpr int kMinimumFontPointSize = 7;
constexpr int kMaximumFontPointSize = 42;

/// Characters that count as part of a word for double-click selection. Path
/// separators and URL punctuation are included so a double click grabs a whole
/// path, which is what the file-manager workflow needs.
bool isWordCharacter(char32_t codePoint)
{
    if (codePoint >= U'0' && codePoint <= U'9')
        return true;
    if (codePoint >= U'a' && codePoint <= U'z')
        return true;
    if (codePoint >= U'A' && codePoint <= U'Z')
        return true;
    if (codePoint > 0x7F)
        return true;

    switch (codePoint) {
    case U'_':
    case U'-':
    case U'.':
    case U'/':
    case U'~':
    case U':':
    case U'+':
    case U'@':
    case U'%':
    case U'=':
    case U'?':
    case U'&':
    case U'#':
        return true;
    default:
        return false;
    }
}

QFont defaultMonospaceFont()
{
    // Prefer the fonts that actually ship with macOS before falling back to
    // whatever Qt considers fixed-pitch.
    const QStringList candidates = {QStringLiteral("SF Mono"), QStringLiteral("Menlo"),
                                    QStringLiteral("JetBrains Mono"), QStringLiteral("Monaco"),
                                    QStringLiteral("DejaVu Sans Mono")};

    const QStringList families = QFontDatabase::families();
    for (const QString &candidate : candidates) {
        if (families.contains(candidate, Qt::CaseInsensitive)) {
            QFont font(candidate);
            font.setStyleHint(QFont::Monospace);
            return font;
        }
    }

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::Monospace);
    return font;
}

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_scheme(ColorScheme::arTermDark())
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setCursor(Qt::IBeamCursor);
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_font = defaultMonospaceFont();
    m_font.setPointSize(m_baseFontPointSize);
    m_font.setFixedPitch(true);
    m_font.setHintingPreference(QFont::PreferFullHinting);

    m_terminal = std::make_unique<Terminal>(m_columns, m_rows, this);

    connect(m_terminal.get(), &Terminal::reply, this, &TerminalWidget::dataEntered);
    connect(m_terminal.get(), &Terminal::titleChanged, this, &TerminalWidget::titleChanged);
    connect(m_terminal.get(), &Terminal::bellRang, this, &TerminalWidget::bellRang);
    connect(m_terminal.get(), &Terminal::clipboardWriteRequested, this, [](const QString &text) {
        QGuiApplication::clipboard()->setText(text);
    });
    connect(m_terminal.get(), &Terminal::screenChanged, this, [this] {
        updateScrollBar();
        viewport()->update();
    });
    connect(m_terminal.get(), &Terminal::alternateScreenChanged, this, [this](bool active) {
        // The alternate buffer has no history, so hide the scrollbar entirely.
        setVerticalScrollBarPolicy(active ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
        clearSelection();
        updateScrollBar();
    });

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(kCursorBlinkIntervalMs);
    connect(m_blinkTimer, &QTimer::timeout, this, [this] {
        m_cursorOn = !m_cursorOn;
        viewport()->update();
    });
    m_blinkTimer->start();

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); });

    updateMetrics();
}

TerminalWidget::~TerminalWidget() = default;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void TerminalWidget::setColorScheme(const ColorScheme &scheme)
{
    m_scheme = scheme;

    QPalette palette = viewport()->palette();
    palette.setColor(QPalette::Window, m_scheme.background());
    palette.setColor(QPalette::Base, m_scheme.background());
    viewport()->setPalette(palette);
    viewport()->setAutoFillBackground(true);

    viewport()->update();
}

void TerminalWidget::setTerminalFont(const QFont &font)
{
    m_font = font;
    m_font.setFixedPitch(true);
    m_baseFontPointSize = font.pointSize() > 0 ? font.pointSize() : m_baseFontPointSize;
    updateMetrics();
}

void TerminalWidget::setLineSpacing(double factor)
{
    m_lineSpacingFactor = std::clamp(factor, 0.0, 1.0);
    updateMetrics();
}

void TerminalWidget::setScrollbackLimit(int lines)
{
    m_terminal->setScrollbackLimit(lines);
    updateScrollBar();
}

void TerminalWidget::setCursorBlinking(bool enabled)
{
    m_cursorBlinking = enabled;
    if (enabled) {
        m_blinkTimer->start();
    } else {
        m_blinkTimer->stop();
        m_cursorOn = true;
        viewport()->update();
    }
}

void TerminalWidget::updateMetrics()
{
    m_boldFont = m_font;
    m_boldFont.setBold(true);
    m_italicFont = m_font;
    m_italicFont.setItalic(true);

    const QFontMetricsF metrics(m_font);

    // Use the advance of a representative glyph rather than maxWidth: with a
    // proportional fallback font maxWidth is far too large and the grid ends up
    // riddled with gaps.
    m_cellWidth = metrics.horizontalAdvance(QLatin1Char('W'));
    if (m_cellWidth <= 0.0)
        m_cellWidth = metrics.averageCharWidth();

    const qreal spacing = std::round(metrics.height() * m_lineSpacingFactor);
    m_cellHeight = std::ceil(metrics.height() + spacing);
    m_baseline = metrics.ascent() + spacing / 2.0;

    updateGridSize();
    viewport()->update();
}

void TerminalWidget::increaseFontSize()
{
    if (m_font.pointSize() >= kMaximumFontPointSize)
        return;
    m_font.setPointSize(m_font.pointSize() + 1);
    updateMetrics();
}

void TerminalWidget::decreaseFontSize()
{
    if (m_font.pointSize() <= kMinimumFontPointSize)
        return;
    m_font.setPointSize(m_font.pointSize() - 1);
    updateMetrics();
}

void TerminalWidget::resetFontSize()
{
    m_font.setPointSize(m_baseFontPointSize);
    updateMetrics();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QSize TerminalWidget::sizeHint() const
{
    return QSize(static_cast<int>(std::ceil(m_cellWidth * 80)) + verticalScrollBar()->sizeHint().width(),
                 static_cast<int>(std::ceil(m_cellHeight * 24)));
}

void TerminalWidget::updateGridSize()
{
    const QSize available = viewport()->size();
    if (available.isEmpty() || m_cellWidth <= 0.0 || m_cellHeight <= 0.0)
        return;

    const int columns = std::max(1, static_cast<int>(available.width() / m_cellWidth));
    const int rows = std::max(1, static_cast<int>(available.height() / m_cellHeight));

    if (columns == m_columns && rows == m_rows)
        return;

    m_columns = columns;
    m_rows = rows;
    m_terminal->resize(columns, rows);
    clearSelection();
    updateScrollBar();

    Q_EMIT terminalResized(columns, rows, available.width(), available.height());
}

void TerminalWidget::updateScrollBar()
{
    QScrollBar *bar = verticalScrollBar();
    const int history = m_terminal->modes().alternateScreen ? 0 : m_terminal->screen().scrollbackSize();

    const bool wasAtBottom = isFollowingOutput();

    // The scrollbar addresses history: [0, history] where history means "live".
    bar->setRange(0, history);
    bar->setPageStep(m_rows);
    bar->setSingleStep(1);

    if (wasAtBottom)
        bar->setValue(history);
}

bool TerminalWidget::isFollowingOutput() const
{
    const QScrollBar *bar = verticalScrollBar();
    return bar->value() >= bar->maximum();
}

int TerminalWidget::topVisibleRow() const
{
    // value == maximum means the live grid starts at row 0.
    return verticalScrollBar()->value() - verticalScrollBar()->maximum();
}

void TerminalWidget::scrollToBottom()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateGridSize();
}

const Line *TerminalWidget::lineAt(int historyRow) const
{
    return m_terminal->screen().historyLine(historyRow);
}

GridPosition TerminalWidget::positionAt(const QPoint &point) const
{
    GridPosition position;
    position.row = topVisibleRow() + static_cast<int>(std::floor(point.y() / m_cellHeight));
    position.column = static_cast<int>(std::floor(point.x() / m_cellWidth));
    position.column = std::clamp(position.column, 0, m_columns - 1);
    return position;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.fillRect(event->rect(), m_scheme.background());

    const int firstRow = topVisibleRow();

    for (int visualRow = 0; visualRow < m_rows; ++visualRow) {
        const int y = static_cast<int>(std::floor(visualRow * m_cellHeight));
        if (y > event->rect().bottom() || y + m_cellHeight < event->rect().top())
            continue;
        paintRow(painter, firstRow + visualRow, y);
    }

    paintCursor(painter);

    m_paintedRevision = m_terminal->revision();
}

void TerminalWidget::paintRow(QPainter &painter, int historyRow, int y)
{
    const Line *line = lineAt(historyRow);
    if (line == nullptr)
        return;

    const bool reverseScreen = m_terminal->modes().reverseVideo;
    const auto columnCount = std::min<int>(static_cast<int>(line->size()), m_columns);

    // Pass 1: backgrounds. Runs of the same colour are merged into one fill so
    // a full-width coloured line is a single rectangle rather than 200.
    int runStart = 0;
    QColor runColor;
    bool runValid = false;

    const auto flushRun = [&](int endColumn) {
        if (!runValid || endColumn <= runStart)
            return;
        if (runColor != m_scheme.background()) {
            const QRectF rect(runStart * m_cellWidth, y, (endColumn - runStart) * m_cellWidth,
                              m_cellHeight);
            painter.fillRect(rect, runColor);
        }
        runStart = endColumn;
    };

    for (int column = 0; column < columnCount; ++column) {
        const Cell &cell = (*line)[static_cast<std::size_t>(column)];
        const bool selected = isSelected(historyRow, column);
        const bool inverse = hasFlag(cell.attributes.flags, CellFlag::Inverse) != reverseScreen;

        QColor background;
        if (selected) {
            background = m_scheme.selection();
        } else if (inverse) {
            background = m_scheme.resolve(cell.attributes.foreground, true,
                                          hasFlag(cell.attributes.flags, CellFlag::Bold));
        } else {
            background = m_scheme.resolve(cell.attributes.background, false, false);
        }

        if (!runValid) {
            runColor = background;
            runValid = true;
            runStart = column;
        } else if (background != runColor) {
            flushRun(column);
            runColor = background;
        }
    }
    flushRun(columnCount);

    // Pass 2: glyphs.
    for (int column = 0; column < columnCount; ++column) {
        const Cell &cell = (*line)[static_cast<std::size_t>(column)];

        if (hasFlag(cell.attributes.flags, CellFlag::WideTrail))
            continue;
        if (hasFlag(cell.attributes.flags, CellFlag::Hidden))
            continue;
        if (cell.isEmpty())
            continue;

        const bool selected = isSelected(historyRow, column);
        const bool bold = hasFlag(cell.attributes.flags, CellFlag::Bold);
        const bool inverse = hasFlag(cell.attributes.flags, CellFlag::Inverse) != reverseScreen;

        QColor foreground;
        if (selected) {
            foreground = m_scheme.selectionText();
        } else if (inverse) {
            foreground = m_scheme.resolve(cell.attributes.background, false, false);
        } else {
            foreground = m_scheme.resolve(cell.attributes.foreground, true, bold);
        }

        if (hasFlag(cell.attributes.flags, CellFlag::Faint))
            foreground.setAlphaF(0.55f);

        const bool italic = hasFlag(cell.attributes.flags, CellFlag::Italic);
        painter.setFont(bold ? (italic ? [this] {
            QFont font = m_boldFont;
            font.setItalic(true);
            return font;
        }() : m_boldFont)
                             : (italic ? m_italicFont : m_font));
        painter.setPen(foreground);

        if (!cell.isBlank()) {
            const QString glyph = QString::fromUcs4(&cell.character, 1);
            const qreal x = column * m_cellWidth;
            painter.drawText(QPointF(x, y + m_baseline), glyph);
        }

        const qreal x = column * m_cellWidth;
        const qreal width = hasFlag(cell.attributes.flags, CellFlag::WideLead) ? m_cellWidth * 2
                                                                              : m_cellWidth;

        if (hasFlag(cell.attributes.flags, CellFlag::Underline)
            || hasFlag(cell.attributes.flags, CellFlag::DoubleUnderline)) {
            QColor underlineColor = foreground;
            if (!cell.attributes.underlineColor.isDefault())
                underlineColor = m_scheme.resolve(cell.attributes.underlineColor, true, false);

            painter.setPen(underlineColor);
            const qreal underlineY = y + m_baseline + 2.0;
            painter.drawLine(QPointF(x, underlineY), QPointF(x + width, underlineY));
            if (hasFlag(cell.attributes.flags, CellFlag::DoubleUnderline))
                painter.drawLine(QPointF(x, underlineY + 2.0), QPointF(x + width, underlineY + 2.0));
        }

        if (hasFlag(cell.attributes.flags, CellFlag::Strikeout)) {
            painter.setPen(foreground);
            const qreal strikeY = y + m_baseline - m_cellHeight * 0.25;
            painter.drawLine(QPointF(x, strikeY), QPointF(x + width, strikeY));
        }
    }
}

void TerminalWidget::paintCursor(QPainter &painter)
{
    if (!m_terminal->modes().cursorVisible)
        return;
    // The cursor is only meaningful where the live output is; scrolling back
    // into history should not paint a stray block.
    if (topVisibleRow() != 0)
        return;
    if (m_cursorBlinking && !m_cursorOn && hasFocus())
        return;

    const CursorState &cursor = m_terminal->screen().cursor();
    const qreal x = cursor.column * m_cellWidth;
    const qreal y = cursor.row * m_cellHeight;
    const QRectF rect(x, y, m_cellWidth, m_cellHeight);

    if (!hasFocus()) {
        // An unfocused terminal shows a hollow cursor, matching Terminal.app.
        painter.setPen(m_scheme.cursor());
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
        return;
    }

    painter.fillRect(rect, m_scheme.cursor());

    const Line *line = lineAt(cursor.row);
    if (line == nullptr || cursor.column >= static_cast<int>(line->size()))
        return;

    const Cell &cell = (*line)[static_cast<std::size_t>(cursor.column)];
    if (cell.isBlank())
        return;

    painter.setPen(m_scheme.cursorText());
    painter.setFont(hasFlag(cell.attributes.flags, CellFlag::Bold) ? m_boldFont : m_font);
    painter.drawText(QPointF(x, y + m_baseline), QString::fromUcs4(&cell.character, 1));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void TerminalWidget::receive(const QByteArray &bytes)
{
    m_terminal->receive(bytes);

    // Any output snaps the view back to the bottom, which is what a user
    // expects after typing a command while scrolled up.
    if (!m_terminal->modes().alternateScreen)
        scrollToBottom();
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    // Scrollback navigation is handled locally and never reaches the host.
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        switch (event->key()) {
        case Qt::Key_PageUp:
            verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepSub);
            return;
        case Qt::Key_PageDown:
            verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd);
            return;
        case Qt::Key_Home:
            verticalScrollBar()->setValue(0);
            return;
        case Qt::Key_End:
            scrollToBottom();
            return;
        default:
            break;
        }
    }

    KeyEncoder::Options options;
    options.applicationCursorKeys = m_terminal->modes().applicationCursorKeys;
    options.applicationKeypad = m_terminal->modes().applicationKeypad;
    options.newLineMode = m_terminal->modes().newLineMode;

    const QByteArray encoded = KeyEncoder::encode(*event, options);
    if (encoded.isEmpty()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    // Typing dismisses the selection and returns to the live output.
    if (m_hasSelection)
        clearSelection();
    scrollToBottom();

    // Restart the blink so the cursor is solid while the user is typing.
    m_cursorOn = true;
    if (m_cursorBlinking)
        m_blinkTimer->start();

    Q_EMIT dataEntered(encoded);
    event->accept();
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    // Dead keys and CJK input methods deliver their result here rather than as
    // a key event.
    if (!event->commitString().isEmpty()) {
        Q_EMIT dataEntered(event->commitString().toUtf8());
        scrollToBottom();
    }
    event->accept();
}

bool TerminalWidget::event(QEvent *event)
{
    // Tab must reach the host instead of moving focus to the next widget.
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab) {
            keyPressEvent(keyEvent);
            return true;
        }
    }
    return QAbstractScrollArea::event(event);
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    m_cursorOn = true;
    if (m_cursorBlinking)
        m_blinkTimer->start();
    if (m_terminal->modes().focusReporting)
        Q_EMIT dataEntered(QByteArrayLiteral("\033[I"));
    viewport()->update();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    m_blinkTimer->stop();
    if (m_terminal->modes().focusReporting)
        Q_EMIT dataEntered(QByteArrayLiteral("\033[O"));
    viewport()->update();
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

bool TerminalWidget::sendMouseEvent(QMouseEvent *event, bool release, bool motion)
{
    const MouseTracking tracking = m_terminal->modes().mouseTracking;
    if (tracking == MouseTracking::Off)
        return false;

    // Shift always bypasses application mouse tracking so the user can still
    // select text in vim or less.
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        return false;

    if (motion && tracking != MouseTracking::ButtonEvent && tracking != MouseTracking::AnyEvent)
        return false;
    if (release && tracking == MouseTracking::X10)
        return false;

    const GridPosition position = positionAt(event->position().toPoint());
    const int column = position.column + 1;
    const int row = position.row - topVisibleRow() + 1;
    if (row < 1 || row > m_rows)
        return true; // Inside the widget but outside the grid: swallow it.

    int button = 3; // Release, in the legacy encoding.
    switch (event->button() == Qt::NoButton ? event->buttons() : event->button()) {
    case Qt::LeftButton:
        button = 0;
        break;
    case Qt::MiddleButton:
        button = 1;
        break;
    case Qt::RightButton:
        button = 2;
        break;
    default:
        button = motion ? 3 : button;
        break;
    }

    if (motion)
        button += 32;

    if (event->modifiers().testFlag(Qt::ShiftModifier))
        button += 4;
    if (event->modifiers().testFlag(Qt::AltModifier))
        button += 8;
    if (event->modifiers().testFlag(Qt::ControlModifier))
        button += 16;

    QByteArray sequence;
    switch (m_terminal->modes().mouseEncoding) {
    case MouseEncoding::Sgr:
        sequence = QByteArrayLiteral("\033[<") + QByteArray::number(button) + ';'
                   + QByteArray::number(column) + ';' + QByteArray::number(row)
                   + (release ? 'm' : 'M');
        break;

    case MouseEncoding::Urxvt:
        sequence = QByteArrayLiteral("\033[") + QByteArray::number(button + 32) + ';'
                   + QByteArray::number(column) + ';' + QByteArray::number(row) + 'M';
        break;

    case MouseEncoding::Utf8:
    case MouseEncoding::Default: {
        if (release)
            button = 3 + (motion ? 32 : 0);
        // The legacy encoding cannot address past column 223.
        if (column > 223 || row > 223)
            return true;
        sequence = QByteArrayLiteral("\033[M");
        sequence.append(static_cast<char>(32 + button));
        sequence.append(static_cast<char>(32 + column));
        sequence.append(static_cast<char>(32 + row));
        break;
    }
    }

    Q_EMIT dataEntered(sequence);
    return true;
}

bool TerminalWidget::sendWheelEvent(QWheelEvent *event)
{
    if (m_terminal->modes().mouseTracking == MouseTracking::Off)
        return false;
    if (event->modifiers().testFlag(Qt::ShiftModifier))
        return false;

    const int steps = std::abs(event->angleDelta().y()) / 120;
    if (steps == 0)
        return false;

    const bool up = event->angleDelta().y() > 0;
    const GridPosition position = positionAt(event->position().toPoint());
    const int column = position.column + 1;
    const int row = position.row - topVisibleRow() + 1;

    const int button = up ? 64 : 65;

    QByteArray sequence;
    for (int i = 0; i < steps; ++i) {
        if (m_terminal->modes().mouseEncoding == MouseEncoding::Sgr) {
            sequence += QByteArrayLiteral("\033[<") + QByteArray::number(button) + ';'
                        + QByteArray::number(column) + ';' + QByteArray::number(row) + 'M';
        } else {
            if (column > 223 || row > 223)
                break;
            sequence += QByteArrayLiteral("\033[M");
            sequence.append(static_cast<char>(32 + button));
            sequence.append(static_cast<char>(32 + column));
            sequence.append(static_cast<char>(32 + row));
        }
    }

    if (sequence.isEmpty())
        return false;

    Q_EMIT dataEntered(sequence);
    return true;
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);

    if (sendMouseEvent(event, /*release=*/false, /*motion=*/false)) {
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton) {
        // X11-style middle-click paste; harmless on macOS where the selection
        // clipboard does not exist and the call simply returns nothing.
        const QString text = QGuiApplication::clipboard()->text(QClipboard::Selection);
        if (!text.isEmpty())
            Q_EMIT dataEntered(KeyEncoder::encodePaste(text, m_terminal->modes().bracketedPaste));
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const GridPosition position = positionAt(event->position().toPoint());

    if (event->modifiers().testFlag(Qt::ShiftModifier) && m_hasSelection) {
        extendSelectionTo(position);
    } else {
        m_selectionAnchor = position;
        m_selectionFocus = position;
        m_selectionUnit = SelectionUnit::Character;
        m_hasSelection = false;
    }

    m_selecting = true;
    viewport()->update();
    event->accept();
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting) {
        if (sendMouseEvent(event, /*release=*/false, /*motion=*/true))
            event->accept();
        else
            QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    // Dragging past the top or bottom edge scrolls the view.
    const int y = event->position().toPoint().y();
    if (y < 0)
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepSub);
    else if (y > viewport()->height())
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);

    extendSelectionTo(positionAt(event->position().toPoint()));
    viewport()->update();
    event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_selecting) {
        if (sendMouseEvent(event, /*release=*/true, /*motion=*/false))
            event->accept();
        else
            QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    m_selecting = false;

    // Mirror the selection into the X11 selection buffer where one exists.
    if (m_hasSelection && QGuiApplication::clipboard()->supportsSelection())
        QGuiApplication::clipboard()->setText(selectedText(), QClipboard::Selection);

    event->accept();
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }

    const GridPosition position = positionAt(event->position().toPoint());

    // A third click within the double-click interval selects the whole line.
    if (m_selectionUnit == SelectionUnit::Word && position.row == m_selectionAnchor.row)
        selectLineAt(position);
    else
        selectWordAt(position);

    m_selecting = true;
    viewport()->update();
    event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0)
            increaseFontSize();
        else if (event->angleDelta().y() < 0)
            decreaseFontSize();
        event->accept();
        return;
    }

    if (sendWheelEvent(event)) {
        event->accept();
        return;
    }

    QAbstractScrollArea::wheelEvent(event);
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *copy = menu.addAction(tr("Copy"), this, &TerminalWidget::copySelection);
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(m_hasSelection);

    QAction *paste = menu.addAction(tr("Paste"), this, &TerminalWidget::pasteFromClipboard);
    paste->setShortcut(QKeySequence::Paste);
    paste->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());

    menu.addSeparator();
    menu.addAction(tr("Select All"), this, &TerminalWidget::selectAll);
    menu.addAction(tr("Clear Buffer"), this, &TerminalWidget::clearScreen);

    menu.exec(event->globalPos());
    event->accept();
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

void TerminalWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText())
        event->acceptProposedAction();
}

void TerminalWidget::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();

    if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : mime->urls()) {
            if (url.isLocalFile())
                paths << url.toLocalFile();
        }

        if (!paths.isEmpty()) {
            // The session decides what a dropped file means: upload it, or
            // insert its quoted path at the prompt.
            Q_EMIT filesDropped(paths);
            event->acceptProposedAction();
            return;
        }
    }

    if (mime->hasText()) {
        Q_EMIT dataEntered(KeyEncoder::encodePaste(mime->text(), m_terminal->modes().bracketedPaste));
        event->acceptProposedAction();
    }
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

bool TerminalWidget::hasSelection() const
{
    return m_hasSelection;
}

bool TerminalWidget::isSelected(int historyRow, int column) const
{
    if (!m_hasSelection)
        return false;

    GridPosition start = m_selectionAnchor;
    GridPosition end = m_selectionFocus;
    if (end < start)
        std::swap(start, end);

    if (historyRow < start.row || historyRow > end.row)
        return false;
    if (historyRow == start.row && column < start.column)
        return false;
    if (historyRow == end.row && column > end.column)
        return false;

    return true;
}

void TerminalWidget::extendSelectionTo(const GridPosition &position)
{
    m_selectionFocus = position;

    if (m_selectionUnit != SelectionUnit::Character) {
        // Keep the originally selected word or line inside the range.
        if (position < m_selectionUnitStart) {
            m_selectionAnchor = m_selectionUnitEnd;
            m_selectionFocus = position;
        } else {
            m_selectionAnchor = m_selectionUnitStart;
            m_selectionFocus = position;
        }
    }

    m_hasSelection = m_selectionAnchor != m_selectionFocus || m_selectionUnit != SelectionUnit::Character;
}

void TerminalWidget::selectWordAt(const GridPosition &position)
{
    const Line *line = lineAt(position.row);
    if (line == nullptr)
        return;

    const auto width = static_cast<int>(line->size());
    if (position.column >= width)
        return;

    const char32_t clicked = (*line)[static_cast<std::size_t>(position.column)].character;
    if (!isWordCharacter(clicked)) {
        m_selectionAnchor = position;
        m_selectionFocus = position;
        m_hasSelection = true;
        m_selectionUnit = SelectionUnit::Word;
        m_selectionUnitStart = position;
        m_selectionUnitEnd = position;
        return;
    }

    int start = position.column;
    while (start > 0 && isWordCharacter((*line)[static_cast<std::size_t>(start - 1)].character))
        --start;

    int end = position.column;
    while (end + 1 < width && isWordCharacter((*line)[static_cast<std::size_t>(end + 1)].character))
        ++end;

    m_selectionAnchor = GridPosition{position.row, start};
    m_selectionFocus = GridPosition{position.row, end};
    m_selectionUnitStart = m_selectionAnchor;
    m_selectionUnitEnd = m_selectionFocus;
    m_selectionUnit = SelectionUnit::Word;
    m_hasSelection = true;
}

void TerminalWidget::selectLineAt(const GridPosition &position)
{
    m_selectionAnchor = GridPosition{position.row, 0};
    m_selectionFocus = GridPosition{position.row, m_columns - 1};
    m_selectionUnitStart = m_selectionAnchor;
    m_selectionUnitEnd = m_selectionFocus;
    m_selectionUnit = SelectionUnit::WholeLine;
    m_hasSelection = true;
}

void TerminalWidget::selectAll()
{
    const int history = m_terminal->screen().scrollbackSize();
    m_selectionAnchor = GridPosition{-history, 0};
    m_selectionFocus = GridPosition{m_rows - 1, m_columns - 1};
    m_selectionUnit = SelectionUnit::Character;
    m_hasSelection = true;
    viewport()->update();
}

void TerminalWidget::clearSelection()
{
    if (!m_hasSelection)
        return;
    m_hasSelection = false;
    m_selectionUnit = SelectionUnit::Character;
    viewport()->update();
}

QString TerminalWidget::selectedText() const
{
    if (!m_hasSelection)
        return {};

    GridPosition start = m_selectionAnchor;
    GridPosition end = m_selectionFocus;
    if (end < start)
        std::swap(start, end);

    QString text;

    for (int row = start.row; row <= end.row; ++row) {
        const Line *line = lineAt(row);
        if (line == nullptr)
            continue;

        const int width = static_cast<int>(line->size());
        const int from = (row == start.row) ? start.column : 0;
        const int to = (row == end.row) ? std::min(end.column, width - 1) : width - 1;

        QString rowText;
        for (int column = from; column <= to; ++column) {
            const Cell &cell = (*line)[static_cast<std::size_t>(column)];
            if (hasFlag(cell.attributes.flags, CellFlag::WideTrail))
                continue;
            rowText += QString::fromUcs4(&cell.character, 1);
        }

        // Trailing blanks are padding, not content.
        while (rowText.endsWith(QLatin1Char(' ')))
            rowText.chop(1);

        text += rowText;

        // A line that ended by wrapping is one logical line, so it must not
        // gain a newline when copied.
        const bool wrapped = row >= 0 && m_terminal->screen().isLineWrapped(row);
        if (row != end.row && !wrapped)
            text += QLatin1Char('\n');
    }

    return text;
}

void TerminalWidget::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void TerminalWidget::pasteFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    scrollToBottom();
    Q_EMIT dataEntered(KeyEncoder::encodePaste(text, m_terminal->modes().bracketedPaste));
}

void TerminalWidget::clearScreen()
{
    m_terminal->screen().eraseInDisplay(Screen::EraseMode::All, m_terminal->currentAttributes());
    m_terminal->screen().clearScrollback();
    m_terminal->screen().moveCursor(0, 0);
    clearSelection();
    updateScrollBar();
    viewport()->update();
}

} // namespace arterm::term
