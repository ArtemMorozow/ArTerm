#include "terminal/screen.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace arterm::term;

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

	/// Reads a row back as text so assertions stay readable.
	std::string row_text( Screen const& screen, int row ){
		std::string text;
		for( Cell const& cell : screen.line( row ) ){
			if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
				continue;
			append_utf8( text, cell.character );
		}
		while( !text.empty() && text.back() == ' ' )
			text.pop_back();
		return text;
	}

	void write( Screen& screen, std::string_view text, bool auto_wrap = true ){
		for( char const character : text )
			screen.write_character( static_cast<char32_t>( character ), 1, Attributes{}, /*insert_mode=*/false,
									auto_wrap );
	}

} // namespace

TEST_CASE( "writes advance the cursor", "[screen]" ){
	Screen screen( 10, 4, 100 );
	write( screen, "abc" );

	CHECK( row_text( screen, 0 ) == "abc" );
	CHECK( screen.cursor().column == 3 );
	CHECK( screen.cursor().row == 0 );
}

TEST_CASE( "wrapping is deferred to the next character", "[screen]" ){
	Screen screen( 3, 3, 100 );
	write( screen, "abc" );

	// After filling the row the cursor stays put with the wrap pending, so a
	// sequence that repositions the cursor does not lose a line.
	CHECK( screen.cursor().row == 0 );
	CHECK( screen.cursor().column == 2 );
	CHECK( screen.pending_wrap() );

	write( screen, "d" );
	CHECK( screen.cursor().row == 1 );
	CHECK( row_text( screen, 1 ) == "d" );
	CHECK( screen.is_line_wrapped( 0 ) );
}

TEST_CASE( "auto wrap off overwrites the last column", "[screen]" ){
	Screen screen( 3, 3, 100 );
	write( screen, "abcXY", /*auto_wrap=*/false );

	CHECK( row_text( screen, 0 ) == "abY" );
	CHECK( row_text( screen, 1 ).empty() );
}

TEST_CASE( "index scrolls at the bottom", "[screen]" ){
	Screen screen( 5, 2, 100 );
	write( screen, "one" );
	screen.index( Attributes{} );
	screen.carriage_return();
	write( screen, "two" );
	screen.index( Attributes{} );
	screen.carriage_return();
	write( screen, "three" );

	CHECK( row_text( screen, 0 ) == "two" );
	CHECK( row_text( screen, 1 ) == "three" );
}

TEST_CASE( "scroll up feeds scrollback", "[screen]" ){
	Screen screen( 5, 2, 100 );
	write( screen, "one" );
	screen.scroll_up( 1, Attributes{} );

	CHECK( screen.scrollback_size() == 1 );
	Line const* history = screen.history_line( -1 );
	REQUIRE( history != nullptr );
	CHECK( history->at( 0 ).character == U'o' );
}

TEST_CASE( "a scroll region does not feed scrollback", "[screen]" ){
	Screen screen( 5, 4, 100 );
	screen.set_scroll_region( 1, 2 );
	screen.scroll_up( 1, Attributes{} );

	// A partial region is a pane being redrawn, not history being produced.
	CHECK( screen.scrollback_size() == 0 );
}

TEST_CASE( "erase in line respects the mode", "[screen]" ){
	Screen screen( 6, 2, 10 );
	write( screen, "abcdef" );
	screen.move_cursor( 0, 3 );
	screen.erase_in_line( Screen::EraseMode::TO_END, Attributes{} );
	CHECK( row_text( screen, 0 ) == "abc" );

	write( screen, "XYZ" );
	screen.move_cursor( 0, 3 );
	screen.erase_in_line( Screen::EraseMode::TO_START, Attributes{} );
	CHECK( row_text( screen, 0 ) == "    YZ" );
}

TEST_CASE( "insert and delete characters", "[screen]" ){
	Screen screen( 6, 2, 10 );
	write( screen, "abcdef" );

	screen.move_cursor( 0, 2 );
	screen.delete_characters( 2, Attributes{} );
	CHECK( row_text( screen, 0 ) == "abef" );

	screen.move_cursor( 0, 2 );
	screen.insert_characters( 1, Attributes{} );
	CHECK( row_text( screen, 0 ) == "ab ef" );
}

TEST_CASE( "insert and delete lines", "[screen]" ){
	Screen screen( 4, 3, 10 );
	write( screen, "aaa" );
	screen.move_cursor( 1, 0 );
	write( screen, "bbb" );
	screen.move_cursor( 2, 0 );
	write( screen, "ccc" );

	screen.move_cursor( 1, 0 );
	screen.insert_lines( 1, Attributes{} );
	CHECK( row_text( screen, 0 ) == "aaa" );
	CHECK( row_text( screen, 1 ).empty() );
	CHECK( row_text( screen, 2 ) == "bbb" );

	screen.move_cursor( 1, 0 );
	screen.delete_lines( 1, Attributes{} );
	CHECK( row_text( screen, 1 ) == "bbb" );
}

TEST_CASE( "resize preserves content", "[screen]" ){
	Screen screen( 10, 4, 100 );
	write( screen, "hello" );

	screen.resize( 20, 4 );
	CHECK( row_text( screen, 0 ) == "hello" );
	CHECK( screen.columns() == 20 );
}

TEST_CASE( "growing pulls back from scrollback", "[screen]" ){
	Screen screen( 6, 2, 100 );
	write( screen, "first" );
	screen.scroll_up( 1, Attributes{} );
	REQUIRE( screen.scrollback_size() == 1 );

	screen.resize( 6, 3 );

	// The line that had scrolled off comes back rather than being replaced by
	// an empty row.
	CHECK( screen.scrollback_size() == 0 );
	CHECK( row_text( screen, 0 ) == "first" );
}

TEST_CASE( "tab stops advance by eight", "[screen]" ){
	Screen screen( 40, 2, 10 );
	CHECK( screen.next_tab_stop( 0 ) == 8 );
	CHECK( screen.next_tab_stop( 8 ) == 16 );
	CHECK( screen.previous_tab_stop( 20 ) == 16 );

	screen.clear_all_tab_stops();
	CHECK( screen.next_tab_stop( 0 ) == 39 );
}

TEST_CASE( "wide characters occupy two columns", "[screen]" ){
	Screen screen( 6, 2, 10 );
	screen.write_character( U'日', 2, Attributes{}, false, true );

	CHECK( screen.cursor().column == 2 );
	CHECK( has_flag( screen.line( 0 )[0].attributes.flags, CellFlag::WIDE_LEAD ) );
	CHECK( has_flag( screen.line( 0 )[1].attributes.flags, CellFlag::WIDE_TRAIL ) );

	// Overwriting the lead must clear its orphaned trailer.
	screen.move_cursor( 0, 0 );
	screen.write_character( U'x', 1, Attributes{}, false, true );
	CHECK_FALSE( has_flag( screen.line( 0 )[1].attributes.flags, CellFlag::WIDE_TRAIL ) );
}
