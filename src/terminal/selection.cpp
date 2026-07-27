#include "terminal/selection.hpp"

#include <algorithm>

namespace arterm::term
{
	namespace
	{

		void append_utf8( std::string& out, char32_t code_point ){
			if( code_point < 0x80 ){
				out.push_back( static_cast<char>( code_point ) );
			}
			else if( code_point < 0x800 ){
				out.push_back( static_cast<char>( 0xC0 | ( code_point >> 6 ) ) );
				out.push_back( static_cast<char>( 0x80 | ( code_point & 0x3F ) ) );
			}
			else if( code_point < 0x10000 ){
				out.push_back( static_cast<char>( 0xE0 | ( code_point >> 12 ) ) );
				out.push_back( static_cast<char>( 0x80 | ( ( code_point >> 6 ) & 0x3F ) ) );
				out.push_back( static_cast<char>( 0x80 | ( code_point & 0x3F ) ) );
			}
			else{
				out.push_back( static_cast<char>( 0xF0 | ( code_point >> 18 ) ) );
				out.push_back( static_cast<char>( 0x80 | ( ( code_point >> 12 ) & 0x3F ) ) );
				out.push_back( static_cast<char>( 0x80 | ( ( code_point >> 6 ) & 0x3F ) ) );
				out.push_back( static_cast<char>( 0x80 | ( code_point & 0x3F ) ) );
			}
		}

	} // namespace

	bool is_word_character( char32_t character ) noexcept{
		if( character == U'\0' || character == U' ' )
			return false;

		switch( character ){
			// Punctuation that holds a path, URL or identifier together.
			case U'_':
			case U'-':
			case U'.':
			case U'/':
			case U'~':
			case U':':
			case U'@':
			case U'+':
			case U'=':
			case U'?':
			case U'%':
			case U'#':
			case U'&':
				return true;
			default:
				break;
		}

		// Anything above ASCII counts: CJK, Cyrillic and accented Latin are all
		// word content, and splitting them would make double-click useless.
		return character > 0x7F || ( character >= U'0' && character <= U'9' )
			   || ( character >= U'A' && character <= U'Z' ) || ( character >= U'a' && character <= U'z' );
	}

	void Selection::begin( Position position, SelectionUnit unit, Screen const& screen ){
		_unit   = unit;
		_active = true;
		_origin = position;
		_focus  = position;
		resolve( screen );
	}

	void Selection::extend_to( Position position, Screen const& screen ){
		if( !_active )
			return;

		_focus = position;
		resolve( screen );
	}

	void Selection::resolve( Screen const& screen ){
		// Snapping is applied to the ordered ends, not to the origin: dragging
		// back past the start of a word must still keep that word's far edge.
		Position const low  = _focus < _origin ? _focus : _origin;
		Position const high = _focus < _origin ? _origin : _focus;

		_first = snap_start( low, screen );
		_last  = snap_end( high, screen );
	}

	bool Selection::contains( int row, int column ) const noexcept{
		if( !_active )
			return false;

		Position const start = first();
		Position const end   = last();

		if( row < start.row || row > end.row )
			return false;
		if( row == start.row && column < start.column )
			return false;
		if( row == end.row && column > end.column )
			return false;
		return true;
	}

	Position Selection::snap_start( Position position, Screen const& screen ) const{
		if( _unit == SelectionUnit::LINE )
			return Position{ position.row, 0 };
		if( _unit == SelectionUnit::CHARACTER )
			return position;

		Line const* line = screen.history_line( position.row );
		if( line == nullptr || position.column >= static_cast<int>( line->size() ) )
			return position;

		if( !is_word_character( ( *line )[static_cast<std::size_t>( position.column )].character ) )
			return position;

		int column = position.column;
		while( column > 0 && is_word_character( ( *line )[static_cast<std::size_t>( column - 1 )].character ) )
			--column;
		return Position{ position.row, column };
	}

	Position Selection::snap_end( Position position, Screen const& screen ) const{
		Line const* line = screen.history_line( position.row );
		int const   width = line != nullptr ? static_cast<int>( line->size() ) : screen.columns();

		if( _unit == SelectionUnit::LINE )
			return Position{ position.row, width - 1 };
		if( _unit == SelectionUnit::CHARACTER )
			return position;

		if( line == nullptr || position.column >= width )
			return position;

		if( !is_word_character( ( *line )[static_cast<std::size_t>( position.column )].character ) )
			return position;

		int column = position.column;
		while( column + 1 < width && is_word_character( ( *line )[static_cast<std::size_t>( column + 1 )].character ) )
			++column;
		return Position{ position.row, column };
	}

	std::string Selection::text( Screen const& screen ) const{
		if( !_active )
			return {};

		Position const start = first();
		Position const end   = last();

		std::string out;

		for( int row = start.row; row <= end.row; ++row ){
			Line const* line = screen.history_line( row );
			if( line == nullptr )
				continue;

			int const width = static_cast<int>( line->size() );
			int const from  = row == start.row ? start.column : 0;
			int const to    = row == end.row ? std::min( end.column, width - 1 ) : width - 1;

			std::string row_text;
			for( int column = from; column <= to && column < width; ++column ){
				Cell const& cell = ( *line )[static_cast<std::size_t>( column )];
				// The trailing half of a wide glyph carries no character of its own.
				if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
					continue;
				append_utf8( row_text, cell.character == U'\0' ? U' ' : cell.character );
			}

			// Blanks running to the right edge are grid padding rather than
			// content, so they go. Blanks the user deliberately selected in the
			// middle of a row are content and stay.
			if( to >= width - 1 ){
				while( !row_text.empty() && row_text.back() == ' ' )
					row_text.pop_back();
			}

			out += row_text;

			// A row the emulator wrapped continues into the next one, so joining
			// them with a newline would break a pasted command in half.
			if( row != end.row && !screen.is_history_line_wrapped( row ) )
				out.push_back( '\n' );
		}

		return out;
	}

} // namespace arterm::term
