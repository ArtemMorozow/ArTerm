#pragma once

#include "terminal/ColorScheme.hpp"
#include "terminal/Terminal.hpp"

#include <QAbstractScrollArea>
#include <QFont>
#include <QPoint>

#include <memory>

class QTimer;

namespace arterm::term {

/// A position in the terminal's history space: `row` is negative inside the
/// scrollback and 0-based inside the visible grid.
struct GridPosition {
    int row{0};
    int column{0};

    friend constexpr auto operator<=>(const GridPosition &, const GridPosition &) = default;
};

/// Renders a `Terminal` and turns user input into the byte stream the remote
/// PTY expects.
///
/// Scrolling is provided by `QAbstractScrollArea`: the vertical scrollbar
/// addresses the scrollback, so value 0 means "oldest history line" and the
/// maximum means "following the live output".
class TerminalWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    [[nodiscard]] Terminal *terminal() const noexcept { return m_terminal.get(); }

    void setColorScheme(const ColorScheme &scheme);
    [[nodiscard]] const ColorScheme &colorScheme() const noexcept { return m_scheme; }

    void setTerminalFont(const QFont &font);
    [[nodiscard]] QFont terminalFont() const { return m_font; }

    /// Extra space between rows, as a fraction of the font height.
    void setLineSpacing(double factor);

    void setScrollbackLimit(int lines);
    void setCursorBlinking(bool enabled);

    /// Feed bytes from the SSH channel.
    void receive(const QByteArray &bytes);

    [[nodiscard]] QString selectedText() const;
    [[nodiscard]] bool hasSelection() const;

    /// Visible grid size, in characters.
    [[nodiscard]] int columns() const noexcept { return m_columns; }
    [[nodiscard]] int rows() const noexcept { return m_rows; }

    [[nodiscard]] QSize sizeHint() const override;

public Q_SLOTS:
    void copySelection();
    void pasteFromClipboard();
    void selectAll();
    void clearSelection();
    /// Drop the scrollback and clear the screen, like Cmd+K in Terminal.app.
    void clearScreen();
    void scrollToBottom();
    void increaseFontSize();
    void decreaseFontSize();
    void resetFontSize();

Q_SIGNALS:
    /// Bytes the user produced, to be written to the remote shell.
    void dataEntered(const QByteArray &data);
    /// The grid changed size and the remote PTY must be told.
    void terminalResized(int columns, int rows, int pixelWidth, int pixelHeight);
    void titleChanged(const QString &title);
    void bellRang();
    /// A path or URL was dropped onto the terminal.
    void filesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool event(QEvent *event) override;

private:
    void updateMetrics();
    void updateGridSize();
    void updateScrollBar();

    [[nodiscard]] GridPosition positionAt(const QPoint &point) const;
    [[nodiscard]] const Line *lineAt(int historyRow) const;

    /// Row index in history space of the topmost visible line.
    [[nodiscard]] int topVisibleRow() const;
    [[nodiscard]] bool isFollowingOutput() const;

    void paintRow(QPainter &painter, int historyRow, int y);
    void paintCursor(QPainter &painter);

    [[nodiscard]] bool isSelected(int historyRow, int column) const;
    void extendSelectionTo(const GridPosition &position);
    void selectWordAt(const GridPosition &position);
    void selectLineAt(const GridPosition &position);

    /// Encode a mouse event for the active tracking mode; returns false when
    /// the application is not tracking the mouse and the widget should handle
    /// the event itself (selection).
    [[nodiscard]] bool sendMouseEvent(QMouseEvent *event, bool release, bool motion);
    [[nodiscard]] bool sendWheelEvent(QWheelEvent *event);

    std::unique_ptr<Terminal> m_terminal;
    ColorScheme m_scheme;

    QFont m_font;
    QFont m_boldFont;
    QFont m_italicFont;
    int m_baseFontPointSize{13};
    double m_lineSpacingFactor{0.12};

    /// Cell metrics in device-independent pixels.
    qreal m_cellWidth{8.0};
    qreal m_cellHeight{16.0};
    qreal m_baseline{12.0};

    int m_columns{80};
    int m_rows{24};

    GridPosition m_selectionAnchor;
    GridPosition m_selectionFocus;
    bool m_selecting{false};
    bool m_hasSelection{false};
    /// Word/line selection keeps the initial unit selected while dragging.
    enum class SelectionUnit { Character, Word, WholeLine };
    SelectionUnit m_selectionUnit{SelectionUnit::Character};
    GridPosition m_selectionUnitStart;
    GridPosition m_selectionUnitEnd;

    QTimer *m_blinkTimer{nullptr};
    bool m_cursorOn{true};
    bool m_cursorBlinking{true};

    quint64 m_paintedRevision{0};
};

} // namespace arterm::term
