#pragma once

#include "terminal/screen.hpp"

#include <string>

namespace arterm::term
{

	/// A cell address in history space: `row` is the same coordinate
	/// `Screen::history_line` takes, so it stays valid while the view scrolls.
	struct Position
	{
		int row{ 0 };
		int column{ 0 };

		[[nodiscard]] constexpr bool operator==( Position const& ) const noexcept = default;

		/// Reading order, which is what deciding "is this cell inside" needs.
		[[nodiscard]] constexpr bool operator<( Position const& other ) const noexcept{
			return row != other.row ? row < other.row : column < other.column;
		}
	};

	/// What a drag extends by.
	enum class SelectionUnit{
		CHARACTER,
		WORD,
		LINE,
	};

	/// A range of cells the user has selected.
	///
	/// Anchor and focus are kept as the user made them - the anchor is where the
	/// drag began - and normalised only when asked, so dragging backwards works
	/// without the caller thinking about it.
	class Selection
	{
	public:
		[[nodiscard]] bool is_active() const noexcept { return _active; }

		[[nodiscard]] SelectionUnit unit() const noexcept { return _unit; }

		/// Begins a selection at `position`. For WORD or LINE the range snaps to
		/// that unit immediately, which is what a double or triple click wants.
		void begin( Position position, SelectionUnit unit, Screen const& screen );

		/// Moves the loose end. Keeps the anchor's unit, so dragging after a
		/// double click extends word by word the way every editor does.
		void extend_to( Position position, Screen const& screen );

		void clear() noexcept { _active = false; }

		/// Ordered bounds; `first` is never after `last`.
		[[nodiscard]] Position first() const noexcept { return _first; }
		[[nodiscard]] Position last() const noexcept { return _last; }

		/// True when the cell at `row`/`column` lies inside the selection.
		[[nodiscard]] bool contains( int row, int column ) const noexcept;

		/// The selected text as UTF-8.
		///
		/// Trailing blanks on each row are dropped, and a row that the emulator
		/// wrapped is joined to the next without a newline - so a wrapped command
		/// pastes back as the single line it was typed as.
		[[nodiscard]] std::string text( Screen const& screen ) const;

	private:
		/// Expands `position` to the word or line around it.
		[[nodiscard]] Position snap_start( Position position, Screen const& screen ) const;
		[[nodiscard]] Position snap_end( Position position, Screen const& screen ) const;

		/// Recomputes the snapped bounds from the raw origin and focus.
		void resolve( Screen const& screen );

		/// Where the drag began and where it is now, both unsnapped. Keeping the
		/// raw positions is what lets a word selection dragged backwards past its
		/// origin still extend to the far edge of the word it started in.
		Position _origin;
		Position _focus;

		Position      _first;
		Position      _last;
		SelectionUnit _unit{ SelectionUnit::CHARACTER };
		bool          _active{ false };
	};

	/// True when `character` belongs to a word for double-click purposes.
	/// Deliberately generous: paths and URLs should select in one go.
	[[nodiscard]] bool is_word_character( char32_t character ) noexcept;

} // namespace arterm::term
