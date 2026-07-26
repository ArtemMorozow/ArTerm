#pragma once

#include "terminal/color_scheme.hpp"
#include "terminal/terminal.hpp"

#include <QAbstractScrollArea>
#include <QFont>
#include <QPoint>

#include <memory>

class QTimer;

namespace arterm::term
{

	/// A position in the terminal's history space: `row` is negative inside the
	/// scrollback and 0-based inside the visible grid.
	struct GridPosition
	{
		int row{ 0 };
		int column{ 0 };

		friend constexpr auto operator<=>( GridPosition const&, GridPosition const& ) = default;
	};

	/// Renders a `Terminal` and turns user input into the byte stream the remote
	/// PTY expects.
	///
	/// Scrolling is provided by `QAbstractScrollArea`: the vertical scrollbar
	/// addresses the scrollback, so value 0 means "oldest history line" and the
	/// maximum means "following the live output".
	class TerminalWidget : public QAbstractScrollArea
	{
		Q_OBJECT

	public:
		explicit TerminalWidget( QWidget* parent = nullptr );
		~TerminalWidget() override;

		[[nodiscard]] Terminal* terminal() const noexcept { return _terminal.get(); }

		void                             set_color_scheme( ColorScheme const& scheme );
		[[nodiscard]] ColorScheme const& color_scheme() const noexcept { return _scheme; }

		void                set_terminal_font( QFont const& font );
		[[nodiscard]] QFont terminal_font() const { return _font; }

		/// Extra space between rows, as a fraction of the font height.
		void set_line_spacing( double factor );

		void set_scrollback_limit( int lines );
		void set_cursor_blinking( bool enabled );

		/// Feed bytes from the SSH channel.
		void receive( QByteArray const& bytes );

		[[nodiscard]] QString selected_text() const;
		[[nodiscard]] bool    has_selection() const;

		/// Visible grid size, in characters.
		[[nodiscard]] int columns() const noexcept { return _columns; }
		[[nodiscard]] int rows() const noexcept { return _rows; }

		[[nodiscard]] QSize sizeHint() const override;

	public Q_SLOTS:
		void copy_selection();
		void paste_from_clipboard();
		void select_all();
		void clear_selection();
		/// Drop the scrollback and clear the screen, like Cmd+K in Terminal.app.
		void clear_screen();
		void scroll_to_bottom();
		void increase_font_size();
		void decrease_font_size();
		void reset_font_size();

	Q_SIGNALS:
		/// Bytes the user produced, to be written to the remote shell.
		void data_entered( QByteArray const& data );
		/// The grid changed size and the remote PTY must be told.
		void terminal_resized( int columns, int rows, int pixel_width, int pixel_height );
		void title_changed( QString const& title );
		void bell_rang();
		/// A path or URL was dropped onto the terminal.
		void files_dropped( QStringList const& paths );

	protected:
		void paintEvent( QPaintEvent* event ) override;
		void resizeEvent( QResizeEvent* event ) override;
		void keyPressEvent( QKeyEvent* event ) override;
		void mousePressEvent( QMouseEvent* event ) override;
		void mouseMoveEvent( QMouseEvent* event ) override;
		void mouseReleaseEvent( QMouseEvent* event ) override;
		void mouseDoubleClickEvent( QMouseEvent* event ) override;
		void wheelEvent( QWheelEvent* event ) override;
		void focusInEvent( QFocusEvent* event ) override;
		void focusOutEvent( QFocusEvent* event ) override;
		void inputMethodEvent( QInputMethodEvent* event ) override;
		void contextMenuEvent( QContextMenuEvent* event ) override;
		void dragEnterEvent( QDragEnterEvent* event ) override;
		void dropEvent( QDropEvent* event ) override;
		bool event( QEvent* event ) override;

	private:
		void update_metrics();
		void update_grid_size();
		void update_scroll_bar();

		[[nodiscard]] GridPosition position_at( QPoint const& point ) const;
		[[nodiscard]] Line const*  line_at( int history_row ) const;

		/// Row index in history space of the topmost visible line.
		[[nodiscard]] int  top_visible_row() const;
		[[nodiscard]] bool is_following_output() const;

		void paint_row( QPainter& painter, int history_row, int y );
		void paint_cursor( QPainter& painter );

		[[nodiscard]] bool is_selected( int history_row, int column ) const;
		void               extend_selection_to( GridPosition const& position );
		void               select_word_at( GridPosition const& position );
		void               select_line_at( GridPosition const& position );

		/// Encode a mouse event for the active tracking mode; returns false when
		/// the application is not tracking the mouse and the widget should handle
		/// the event itself (selection).
		[[nodiscard]] bool send_mouse_event( QMouseEvent* event, bool release, bool motion );
		[[nodiscard]] bool send_wheel_event( QWheelEvent* event );

		std::unique_ptr<Terminal> _terminal;
		ColorScheme               _scheme;

		QFont  _font;
		QFont  _bold_font;
		QFont  _italic_font;
		int    _base_font_point_size{ 13 };
		double _line_spacing_factor{ 0.12 };

		/// Cell metrics in device-independent pixels.
		qreal _cell_width{ 8.0 };
		qreal _cell_height{ 16.0 };
		qreal _baseline{ 12.0 };

		int _columns{ 80 };
		int _rows{ 24 };

		GridPosition _selection_anchor;
		GridPosition _selection_focus;
		bool         _selecting{ false };
		bool         _has_selection{ false };
		/// Word/line selection keeps the initial unit selected while dragging.
		enum class SelectionUnit { CHARACTER, WORD, WHOLE_LINE };
		SelectionUnit _selection_unit{ SelectionUnit::CHARACTER };
		GridPosition  _selection_unit_start;
		GridPosition  _selection_unit_end;

		QTimer* _blink_timer{ nullptr };
		bool    _cursor_on{ true };
		bool    _cursor_blinking{ true };

		quint64 _painted_revision{ 0 };
	};

} // namespace arterm::term
