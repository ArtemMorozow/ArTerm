#pragma once

#include "terminal/cell.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace arterm::term
{

	using Line = std::vector<Cell>;

	/// Cursor position plus the state that DECSC/DECRC must save with it.
	struct CursorState
	{
		int        row{ 0 };
		int        column{ 0 };
		Attributes attributes;
		bool       pending_wrap{ false };
		bool       origin_mode{ false };
		int        charset{ 0 };
	};

	/// The character grid of one terminal buffer.
	///
	/// A `Terminal` owns two of these: the normal buffer, which keeps scrollback,
	/// and the alternate buffer used by full-screen programs, which does not.
	class Screen
	{
	public:
		Screen( int columns, int rows, int scrollback_limit );

		[[nodiscard]] int columns() const noexcept { return _columns; }
		[[nodiscard]] int rows() const noexcept { return _rows; }
		[[nodiscard]] int scrollback_size() const noexcept { return static_cast<int>( _scrollback.size() ); }
		[[nodiscard]] int scrollback_limit() const noexcept { return _scrollback_limit; }

		void set_scrollback_limit( int lines );

		/// Reflow to a new size. Content is anchored to the bottom, matching what
		/// every other terminal does when a window is resized.
		void resize( int columns, int rows );

		// -- Access ------------------------------------------------------------

		/// `row` is 0-based within the visible grid.
		[[nodiscard]] Line const& line( int row ) const;
		[[nodiscard]] Line&       line( int row );

		/// Row addressed in "history space": negative values index the scrollback,
		/// with -1 being the most recently scrolled-off line.
		[[nodiscard]] Line const* history_line( int offset ) const;

		[[nodiscard]] CursorState const& cursor() const noexcept { return _cursor; }
		[[nodiscard]] CursorState&       cursor() noexcept { return _cursor; }

		// -- Writing -----------------------------------------------------------

		/// Place a character at the cursor, honouring wrap and insert mode.
		void write_character( char32_t code_point, int width, Attributes const& attributes, bool insert_mode,
							  bool auto_wrap );

		// -- Cursor movement ---------------------------------------------------

		void move_cursor( int row, int column );
		void move_cursor_relative( int row_delta, int column_delta );
		void set_column( int column );
		void set_row( int row );
		void carriage_return();

		/// LF / IND: down one line, scrolling the region if already at the bottom.
		void index( Attributes const& fill );
		/// RI: up one line, scrolling the region down if already at the top.
		void reverse_index( Attributes const& fill );
		/// NEL.
		void next_line( Attributes const& fill );

		// -- Erasing -----------------------------------------------------------

		enum class EraseMode { TO_END, TO_START, ALL };

		void erase_in_line( EraseMode mode, Attributes const& fill );
		void erase_in_display( EraseMode mode, Attributes const& fill );
		void erase_characters( int count, Attributes const& fill );
		void clear_scrollback();

		// -- Editing -----------------------------------------------------------

		void insert_lines( int count, Attributes const& fill );
		void delete_lines( int count, Attributes const& fill );
		void insert_characters( int count, Attributes const& fill );
		void delete_characters( int count, Attributes const& fill );

		void scroll_up( int count, Attributes const& fill );
		void scroll_down( int count, Attributes const& fill );

		// -- Scroll region -----------------------------------------------------

		void              set_scroll_region( int top, int bottom );
		[[nodiscard]] int scroll_top() const noexcept { return _scroll_top; }
		[[nodiscard]] int scroll_bottom() const noexcept { return _scroll_bottom; }
		void              reset_scroll_region();

		// -- Tab stops ---------------------------------------------------------

		void              set_tab_stop( int column );
		void              clear_tab_stop( int column );
		void              clear_all_tab_stops();
		void              reset_tab_stops();
		[[nodiscard]] int next_tab_stop( int column ) const;
		[[nodiscard]] int previous_tab_stop( int column ) const;

		// -- Misc --------------------------------------------------------------

		void reset( Attributes const& fill );
		void fill_with( char32_t code_point, Attributes const& attributes );

		/// True when the cursor sits one past the last column and the next
		/// printable character must wrap first.
		[[nodiscard]] bool pending_wrap() const noexcept { return _cursor.pending_wrap; }

		/// Whether `row` was terminated by a wrap rather than a newline. Used when
		/// copying a selection so re-wrapped paragraphs paste as one line.
		[[nodiscard]] bool is_line_wrapped( int row ) const;
		void               set_line_wrapped( int row, bool wrapped );

		/// Same question in the coordinate space `history_line` uses, so a
		/// selection reaching into the scrollback can still tell a wrapped row
		/// from one that ended in a newline.
		[[nodiscard]] bool is_history_line_wrapped( int offset ) const;

		/// Marks every line as needing a repaint; used after a full reset.
		void                        mark_all_dirty() { _revision++; }
		[[nodiscard]] std::uint64_t revision() const noexcept { return _revision; }

	private:
		[[nodiscard]] Line make_line( Attributes const& fill ) const;
		void               clamp_cursor();

		int _columns;
		int _rows;
		int _scrollback_limit;

		std::vector<Line> _lines;
		std::deque<Line> _scrollback;
		/// Parallel to `_scrollback`: a line keeps its wrap flag when it scrolls
		/// off, which is what lets a wrapped command be copied back out of the
		/// history as the single line it was typed as.
		std::deque<bool>  _scrollback_wrapped;
		std::vector<bool> _wrapped;
		std::vector<bool> _tab_stops;

		CursorState   _cursor;
		int           _scroll_top{ 0 };
		int           _scroll_bottom{ 0 };
		std::uint64_t _revision{ 0 };
	};

} // namespace arterm::term
