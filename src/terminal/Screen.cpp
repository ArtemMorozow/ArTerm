#include "terminal/screen.hpp"

#include <algorithm>

namespace arterm::term
{
	namespace
	{

		constexpr int TAB_WIDTH = 8;

		Cell blank_cell( Attributes const& attributes ){
			Cell cell;
			cell.character  = U' ';
			cell.attributes = attributes;
			// A blank inherits the background but never the glyph decorations.
			cell.attributes.flags &= ~( CellFlag::UNDERLINE | CellFlag::DOUBLE_UNDERLINE | CellFlag::STRIKEOUT |
										CellFlag::WIDE_LEAD | CellFlag::WIDE_TRAIL );
			return cell;
		}

	} // namespace

	Screen::Screen( int columns, int rows, int scrollback_limit )
		: _columns( std::max( 1, columns ) )
		, _rows( std::max( 1, rows ) )
		, _scrollback_limit( std::max( 0, scrollback_limit ) )
	{
		_lines.assign( static_cast<std::size_t>( _rows ), make_line( Attributes{} ) );
		_wrapped.assign( static_cast<std::size_t>( _rows ), false );
		_scroll_bottom = _rows - 1;
		reset_tab_stops();
	}

	Line Screen::make_line( Attributes const& fill ) const{
		return Line( static_cast<std::size_t>( _columns ), blank_cell( fill ) );
	}

	Line const& Screen::line( int row ) const{
		int const clamped = std::clamp( row, 0, _rows - 1 );
		return _lines[static_cast<std::size_t>( clamped )];
	}

	Line& Screen::line( int row ){
		int const clamped = std::clamp( row, 0, _rows - 1 );
		return _lines[static_cast<std::size_t>( clamped )];
	}

	Line const* Screen::history_line( int offset ) const{
		if( offset >= 0 )
			return offset < _rows ? &_lines[static_cast<std::size_t>( offset )] : nullptr;

		int const index = static_cast<int>( _scrollback.size() ) + offset;
		if( index < 0 || index >= static_cast<int>( _scrollback.size() ) )
			return nullptr;
		return &_scrollback[static_cast<std::size_t>( index )];
	}

	bool Screen::is_line_wrapped( int row ) const{
		if( row < 0 || row >= static_cast<int>( _wrapped.size() ) )
			return false;
		return _wrapped[static_cast<std::size_t>( row )];
	}

	void Screen::set_line_wrapped( int row, bool wrapped ){
		if( row < 0 || row >= static_cast<int>( _wrapped.size() ) )
			return;
		_wrapped[static_cast<std::size_t>( row )] = wrapped;
	}

	void Screen::set_scrollback_limit( int lines ){
		_scrollback_limit = std::max( 0, lines );
		while( static_cast<int>( _scrollback.size() ) > _scrollback_limit )
			_scrollback.pop_front();
		++_revision;
	}

	void Screen::resize( int columns, int rows ){
		columns = std::max( 1, columns );
		rows    = std::max( 1, rows );
		if( columns == _columns && rows == _rows )
			return;

		int const old_rows = _rows;

		if( columns != _columns ){
			Cell const filler = blank_cell( Attributes{} );
			for( Line& existing : _lines )
				existing.resize( static_cast<std::size_t>( columns ), filler );
			for( Line& existing : _scrollback )
				existing.resize( static_cast<std::size_t>( columns ), filler );
			_columns = columns;
		}

		if( rows < old_rows ){
			// Shrinking: push the lines that fall off the top into scrollback so
			// the content the user was reading is not simply destroyed.
			int const surplus = old_rows - rows;

			// Keep the cursor line visible when it sits near the bottom.
			int const cursor_slack = std::max( 0, _cursor.row - ( rows - 1 ) );
			int const to_scroll    = std::min( surplus, std::max( cursor_slack, surplus ) );

			for( int i = 0; i < to_scroll; ++i ){
				if( _scrollback_limit > 0 ){
					_scrollback.push_back( std::move( _lines.front() ) );
					while( static_cast<int>( _scrollback.size() ) > _scrollback_limit )
						_scrollback.pop_front();
				}
				_lines.erase( _lines.begin() );
				_wrapped.erase( _wrapped.begin() );
				_cursor.row = std::max( 0, _cursor.row - 1 );
			}

			_lines.resize( static_cast<std::size_t>( rows ) );
			_wrapped.resize( static_cast<std::size_t>( rows ), false );
		}
		else if( rows > old_rows ){
			// Growing: pull lines back out of scrollback before padding with blanks
			// so the window "unrolls" the history instead of showing empty space.
			int deficit = rows - old_rows;

			while( deficit > 0 && !_scrollback.empty() ){
				_lines.insert( _lines.begin(), std::move( _scrollback.back() ) );
				_scrollback.pop_back();
				_wrapped.insert( _wrapped.begin(), false );
				_cursor.row++;
				--deficit;
			}

			for( int i = 0; i < deficit; ++i ){
				_lines.push_back( make_line( Attributes{} ) );
				_wrapped.push_back( false );
			}
		}

		_rows = rows;
		_lines.resize( static_cast<std::size_t>( _rows ), make_line( Attributes{} ) );
		_wrapped.resize( static_cast<std::size_t>( _rows ), false );

		reset_scroll_region();
		reset_tab_stops();
		clamp_cursor();
		++_revision;
	}

	void Screen::clamp_cursor(){
		_cursor.row          = std::clamp( _cursor.row, 0, _rows - 1 );
		_cursor.column       = std::clamp( _cursor.column, 0, _columns - 1 );
		_cursor.pending_wrap = false;
	}

	// ---------------------------------------------------------------------------
	// Writing
	// ---------------------------------------------------------------------------

	void Screen::write_character( char32_t code_point, int width, Attributes const& attributes, bool insert_mode,
								  bool auto_wrap ){
		if( width <= 0 )
			width = 1;

		if( _cursor.pending_wrap && auto_wrap ){
			set_line_wrapped( _cursor.row, true );
			_cursor.column = 0;
			index( attributes );
			_cursor.pending_wrap = false;
		}

		// A double-width glyph never straddles the right edge.
		if( width == 2 && _cursor.column == _columns - 1 ){
			if( auto_wrap ){
				set_line_wrapped( _cursor.row, true );
				_cursor.column = 0;
				index( attributes );
			}
			else{
				return;
			}
		}

		Line& target = line( _cursor.row );

		if( insert_mode ){
			auto const position = static_cast<std::size_t>( _cursor.column );
			for( int i = 0; i < width; ++i ){
				target.insert( target.begin() + static_cast<std::ptrdiff_t>( position ), blank_cell( attributes ) );
				target.pop_back();
			}
		}

		// Overwriting half of an existing wide glyph must blank its other half,
		// otherwise a stale trailer is left on screen.
		auto const blank_partner = [&]( int column ){
			if( column < 0 || column >= _columns )
				return;
			Cell& cell = target[static_cast<std::size_t>( column )];
			if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) && column > 0 ){
				Cell& lead = target[static_cast<std::size_t>( column - 1 )];
				lead       = blank_cell( lead.attributes );
			}
			else if( has_flag( cell.attributes.flags, CellFlag::WIDE_LEAD ) && column + 1 < _columns ){
				Cell& trail = target[static_cast<std::size_t>( column + 1 )];
				trail       = blank_cell( trail.attributes );
			}
		};
		blank_partner( _cursor.column );
		if( width == 2 )
			blank_partner( _cursor.column + 1 );

		Cell& cell      = target[static_cast<std::size_t>( _cursor.column )];
		cell.character  = code_point;
		cell.attributes = attributes;
		if( width == 2 ){
			cell.attributes.flags |= CellFlag::WIDE_LEAD;

			Cell& trailer      = target[static_cast<std::size_t>( _cursor.column + 1 )];
			trailer.character  = U'\0';
			trailer.attributes = attributes;
			trailer.attributes.flags |= CellFlag::WIDE_TRAIL;
		}

		_cursor.column += width;
		if( _cursor.column >= _columns ){
			_cursor.column       = _columns - 1;
			_cursor.pending_wrap = true;
		}
		else{
			_cursor.pending_wrap = false;
		}

		++_revision;
	}

	// ---------------------------------------------------------------------------
	// Cursor movement
	// ---------------------------------------------------------------------------

	void Screen::move_cursor( int row, int column ){
		_cursor.row          = std::clamp( row, 0, _rows - 1 );
		_cursor.column       = std::clamp( column, 0, _columns - 1 );
		_cursor.pending_wrap = false;
	}

	void Screen::move_cursor_relative( int row_delta, int column_delta ){
		move_cursor( _cursor.row + row_delta, _cursor.column + column_delta );
	}

	void Screen::set_column( int column ){
		_cursor.column       = std::clamp( column, 0, _columns - 1 );
		_cursor.pending_wrap = false;
	}

	void Screen::set_row( int row ){
		_cursor.row          = std::clamp( row, 0, _rows - 1 );
		_cursor.pending_wrap = false;
	}

	void Screen::carriage_return(){
		_cursor.column       = 0;
		_cursor.pending_wrap = false;
	}

	void Screen::index( Attributes const& fill ){
		if( _cursor.row == _scroll_bottom ){
			scroll_up( 1, fill );
		}
		else if( _cursor.row < _rows - 1 ){
			++_cursor.row;
		}
		_cursor.pending_wrap = false;
	}

	void Screen::reverse_index( Attributes const& fill ){
		if( _cursor.row == _scroll_top ){
			scroll_down( 1, fill );
		}
		else if( _cursor.row > 0 ){
			--_cursor.row;
		}
		_cursor.pending_wrap = false;
	}

	void Screen::next_line( Attributes const& fill ){
		carriage_return();
		index( fill );
	}

	// ---------------------------------------------------------------------------
	// Erasing
	// ---------------------------------------------------------------------------

	void Screen::erase_in_line( EraseMode mode, Attributes const& fill ){
		Line&      target = line( _cursor.row );
		Cell const filler = blank_cell( fill );

		switch( mode ){
			case EraseMode::TO_END:
				std::fill( target.begin() + _cursor.column, target.end(), filler );
				break;
			case EraseMode::TO_START:
				std::fill( target.begin(), target.begin() + std::min( _cursor.column + 1, _columns ), filler );
				break;
			case EraseMode::ALL:
				std::fill( target.begin(), target.end(), filler );
				break;
		}

		set_line_wrapped( _cursor.row, false );
		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::erase_in_display( EraseMode mode, Attributes const& fill ){
		Cell const filler = blank_cell( fill );

		switch( mode ){
			case EraseMode::TO_END:
				erase_in_line( EraseMode::TO_END, fill );
				for( int row = _cursor.row + 1; row < _rows; ++row ){
					std::fill( _lines[static_cast<std::size_t>( row )].begin(),
							   _lines[static_cast<std::size_t>( row )].end(), filler );
					set_line_wrapped( row, false );
				}
				break;

			case EraseMode::TO_START:
				for( int row = 0; row < _cursor.row; ++row ){
					std::fill( _lines[static_cast<std::size_t>( row )].begin(),
							   _lines[static_cast<std::size_t>( row )].end(), filler );
					set_line_wrapped( row, false );
				}
				erase_in_line( EraseMode::TO_START, fill );
				break;

			case EraseMode::ALL:
				for( int row = 0; row < _rows; ++row ){
					std::fill( _lines[static_cast<std::size_t>( row )].begin(),
							   _lines[static_cast<std::size_t>( row )].end(), filler );
					set_line_wrapped( row, false );
				}
				break;
		}

		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::erase_characters( int count, Attributes const& fill ){
		count            = std::max( 1, count );
		Line&     target = line( _cursor.row );
		int const end    = std::min( _cursor.column + count, _columns );
		std::fill( target.begin() + _cursor.column, target.begin() + end, blank_cell( fill ) );
		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::clear_scrollback(){
		_scrollback.clear();
		++_revision;
	}

	// ---------------------------------------------------------------------------
	// Editing
	// ---------------------------------------------------------------------------

	void Screen::insert_lines( int count, Attributes const& fill ){
		if( _cursor.row < _scroll_top || _cursor.row > _scroll_bottom )
			return;

		count = std::clamp( count, 1, _scroll_bottom - _cursor.row + 1 );

		for( int i = 0; i < count; ++i ){
			_lines.erase( _lines.begin() + _scroll_bottom );
			_wrapped.erase( _wrapped.begin() + _scroll_bottom );
			_lines.insert( _lines.begin() + _cursor.row, make_line( fill ) );
			_wrapped.insert( _wrapped.begin() + _cursor.row, false );
		}

		_cursor.column       = 0;
		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::delete_lines( int count, Attributes const& fill ){
		if( _cursor.row < _scroll_top || _cursor.row > _scroll_bottom )
			return;

		count = std::clamp( count, 1, _scroll_bottom - _cursor.row + 1 );

		for( int i = 0; i < count; ++i ){
			_lines.erase( _lines.begin() + _cursor.row );
			_wrapped.erase( _wrapped.begin() + _cursor.row );
			_lines.insert( _lines.begin() + _scroll_bottom, make_line( fill ) );
			_wrapped.insert( _wrapped.begin() + _scroll_bottom, false );
		}

		_cursor.column       = 0;
		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::insert_characters( int count, Attributes const& fill ){
		count        = std::clamp( count, 1, _columns - _cursor.column );
		Line& target = line( _cursor.row );

		for( int i = 0; i < count; ++i ){
			target.insert( target.begin() + _cursor.column, blank_cell( fill ) );
			target.pop_back();
		}

		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::delete_characters( int count, Attributes const& fill ){
		count        = std::clamp( count, 1, _columns - _cursor.column );
		Line& target = line( _cursor.row );

		for( int i = 0; i < count; ++i ){
			target.erase( target.begin() + _cursor.column );
			target.push_back( blank_cell( fill ) );
		}

		_cursor.pending_wrap = false;
		++_revision;
	}

	void Screen::scroll_up( int count, Attributes const& fill ){
		count = std::clamp( count, 1, _scroll_bottom - _scroll_top + 1 );

		// Only a full-height region feeds the scrollback; a program that set a
		// smaller region is drawing a pane, not producing history.
		bool const feeds_scrollback = ( _scroll_top == 0 && _scroll_bottom == _rows - 1 );

		for( int i = 0; i < count; ++i ){
			if( feeds_scrollback && _scrollback_limit > 0 ){
				_scrollback.push_back( std::move( _lines[static_cast<std::size_t>( _scroll_top )] ) );
				while( static_cast<int>( _scrollback.size() ) > _scrollback_limit )
					_scrollback.pop_front();
			}

			_lines.erase( _lines.begin() + _scroll_top );
			_wrapped.erase( _wrapped.begin() + _scroll_top );
			_lines.insert( _lines.begin() + _scroll_bottom, make_line( fill ) );
			_wrapped.insert( _wrapped.begin() + _scroll_bottom, false );
		}

		++_revision;
	}

	void Screen::scroll_down( int count, Attributes const& fill ){
		count = std::clamp( count, 1, _scroll_bottom - _scroll_top + 1 );

		for( int i = 0; i < count; ++i ){
			_lines.erase( _lines.begin() + _scroll_bottom );
			_wrapped.erase( _wrapped.begin() + _scroll_bottom );
			_lines.insert( _lines.begin() + _scroll_top, make_line( fill ) );
			_wrapped.insert( _wrapped.begin() + _scroll_top, false );
		}

		++_revision;
	}

	// ---------------------------------------------------------------------------
	// Scroll region and tab stops
	// ---------------------------------------------------------------------------

	void Screen::set_scroll_region( int top, int bottom ){
		top    = std::clamp( top, 0, _rows - 1 );
		bottom = std::clamp( bottom, 0, _rows - 1 );

		if( top >= bottom ){
			reset_scroll_region();
			return;
		}

		_scroll_top    = top;
		_scroll_bottom = bottom;
	}

	void Screen::reset_scroll_region(){
		_scroll_top    = 0;
		_scroll_bottom = _rows - 1;
	}

	void Screen::reset_tab_stops(){
		_tab_stops.assign( static_cast<std::size_t>( _columns ), false );
		for( int column = TAB_WIDTH; column < _columns; column += TAB_WIDTH )
			_tab_stops[static_cast<std::size_t>( column )] = true;
	}

	void Screen::set_tab_stop( int column ){
		if( column >= 0 && column < _columns )
			_tab_stops[static_cast<std::size_t>( column )] = true;
	}

	void Screen::clear_tab_stop( int column ){
		if( column >= 0 && column < _columns )
			_tab_stops[static_cast<std::size_t>( column )] = false;
	}

	void Screen::clear_all_tab_stops(){
		_tab_stops.assign( static_cast<std::size_t>( _columns ), false );
	}

	int Screen::next_tab_stop( int column ) const{
		for( int i = column + 1; i < _columns; ++i ){
			if( _tab_stops[static_cast<std::size_t>( i )] )
				return i;
		}
		return _columns - 1;
	}

	int Screen::previous_tab_stop( int column ) const{
		for( int i = column - 1; i > 0; --i ){
			if( _tab_stops[static_cast<std::size_t>( i )] )
				return i;
		}
		return 0;
	}

	// ---------------------------------------------------------------------------
	// Reset
	// ---------------------------------------------------------------------------

	void Screen::reset( Attributes const& fill ){
		_lines.assign( static_cast<std::size_t>( _rows ), make_line( fill ) );
		_wrapped.assign( static_cast<std::size_t>( _rows ), false );
		_cursor = CursorState{};
		reset_scroll_region();
		reset_tab_stops();
		++_revision;
	}

	void Screen::fill_with( char32_t code_point, Attributes const& attributes ){
		Cell cell;
		cell.character  = code_point;
		cell.attributes = attributes;

		for( Line& target : _lines )
			std::fill( target.begin(), target.end(), cell );

		++_revision;
	}

} // namespace arterm::term
