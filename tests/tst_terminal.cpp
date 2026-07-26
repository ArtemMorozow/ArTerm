#include "terminal/terminal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

	std::string row_text( Terminal const& terminal, int row ){
		std::string text;
		for( Cell const& cell : terminal.screen().line( row ) ){
			if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
				continue;
			append_utf8( text, cell.character );
		}
		while( !text.empty() && text.back() == ' ' )
			text.pop_back();
		return text;
	}

} // namespace

TEST_CASE( "prints plain output", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "hello world" );

	CHECK( row_text( terminal, 0 ) == "hello world" );
}

TEST_CASE( "cursor positioning", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "\033[3;5Hx" );

	CHECK( terminal.screen().cursor().row == 2 );
	CHECK( row_text( terminal, 2 ) == "    x" );
}

TEST_CASE( "SGR sets colours", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "\033[31mred\033[0mplain" );

	Cell const& red = terminal.screen().line( 0 )[0];
	CHECK( red.attributes.foreground.kind() == Color::Kind::INDEXED );
	CHECK( red.attributes.foreground.index() == 1 );

	Cell const& plain = terminal.screen().line( 0 )[3];
	CHECK( plain.attributes.foreground.is_default() );
}

TEST_CASE( "SGR 256 and true colour", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "\033[38;5;208mA\033[38;2;10;20;30mB" );

	Cell const& indexed = terminal.screen().line( 0 )[0];
	CHECK( indexed.attributes.foreground.kind() == Color::Kind::INDEXED );
	CHECK( indexed.attributes.foreground.index() == 208 );

	Cell const& rgb = terminal.screen().line( 0 )[1];
	CHECK( rgb.attributes.foreground.kind() == Color::Kind::RGB );
	CHECK( rgb.attributes.foreground.red() == 10 );
	CHECK( rgb.attributes.foreground.green() == 20 );
	CHECK( rgb.attributes.foreground.blue() == 30 );
}

TEST_CASE( "bold and reset are tracked", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "\033[1mB\033[22mN" );

	CHECK( has_flag( terminal.screen().line( 0 )[0].attributes.flags, CellFlag::BOLD ) );
	CHECK_FALSE( has_flag( terminal.screen().line( 0 )[1].attributes.flags, CellFlag::BOLD ) );
}

TEST_CASE( "the alternate screen is separate", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "normal" );

	terminal.receive( "\033[?1049h" );
	CHECK( terminal.modes().alternate_screen );
	CHECK( row_text( terminal, 0 ).empty() );

	terminal.receive( "alt" );
	CHECK( row_text( terminal, 0 ) == "alt" );

	terminal.receive( "\033[?1049l" );
	CHECK_FALSE( terminal.modes().alternate_screen );
	CHECK( row_text( terminal, 0 ) == "normal" );
}

TEST_CASE( "the title is extracted from OSC", "[terminal]" ){
	Terminal                 terminal( 20, 5 );
	std::vector<std::string> seen;
	terminal.title_changed.connect( [&seen]( std::string const& title ){ seen.push_back( title ); } );

	terminal.receive( "\033]0;build-01\007" );

	CHECK( seen.size() == 1 );
	CHECK( terminal.title() == "build-01" );
}

TEST_CASE( "a hostile title cannot smuggle an escape sequence", "[terminal]" ){
	Terminal terminal( 20, 5 );

	// An ESC inside the OSC abandons the sequence outright, so no title is set.
	terminal.receive( "\033]2;bad\033[31mtitle\007" );
	CHECK( terminal.title().empty() );

	// Control bytes that do reach the payload are filtered out of it instead.
	terminal.receive( "\033]2;ho\001st\177-01\007" );
	CHECK( terminal.title() == "host-01" );
}

TEST_CASE( "OSC 52 writes the clipboard, but never reads it", "[terminal]" ){
	Terminal    terminal( 20, 5 );
	std::string written;
	int         requests = 0;
	terminal.clipboard_write_requested.connect( [&]( std::string const& text ){
		written = text;
		++requests;
	} );

	terminal.receive( "\033]52;c;aGVsbG8gd29ybGQ=\007" );
	CHECK( requests == 1 );
	CHECK( written == "hello world" );

	// "?" asks for the local clipboard contents; that request is refused.
	terminal.receive( "\033]52;c;?\007" );
	CHECK( requests == 1 );
}

TEST_CASE( "the device status report is answered", "[terminal]" ){
	Terminal                 terminal( 20, 5 );
	std::vector<std::string> replies;
	terminal.reply.connect( [&replies]( std::string const& data ){ replies.push_back( data ); } );

	terminal.receive( "\033[3;7H\033[6n" );

	REQUIRE( replies.size() == 1 );
	CHECK( replies.front() == "\033[3;7R" );
}

TEST_CASE( "bracketed paste mode toggles", "[terminal]" ){
	Terminal terminal( 20, 5 );
	CHECK_FALSE( terminal.modes().bracketed_paste );

	terminal.receive( "\033[?2004h" );
	CHECK( terminal.modes().bracketed_paste );

	terminal.receive( "\033[?2004l" );
	CHECK_FALSE( terminal.modes().bracketed_paste );
}

TEST_CASE( "mouse tracking mode toggles", "[terminal]" ){
	Terminal terminal( 20, 5 );

	terminal.receive( "\033[?1002h\033[?1006h" );
	CHECK( terminal.modes().mouse_tracking == MouseTracking::BUTTON_EVENT );
	CHECK( terminal.modes().mouse_encoding == MouseEncoding::SGR );

	terminal.receive( "\033[?1002l" );
	CHECK( terminal.modes().mouse_tracking == MouseTracking::OFF );
}

TEST_CASE( "erase display clears everything", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "line one\r\nline two" );
	terminal.receive( "\033[2J" );

	CHECK( row_text( terminal, 0 ).empty() );
	CHECK( row_text( terminal, 1 ).empty() );
}

TEST_CASE( "the line drawing charset is applied", "[terminal]" ){
	Terminal terminal( 20, 5 );
	// ESC ( 0 selects DEC special graphics for G0; 'q' becomes a horizontal line.
	terminal.receive( "\033(0qqq\033(B" );

	CHECK( row_text( terminal, 0 ) == "───" );
}

TEST_CASE( "reset restores the defaults", "[terminal]" ){
	Terminal terminal( 20, 5 );
	terminal.receive( "\033[?1049h\033[31mtext\033[?7l" );

	terminal.reset();

	CHECK_FALSE( terminal.modes().alternate_screen );
	CHECK( terminal.modes().auto_wrap );
	CHECK( terminal.current_attributes().foreground.is_default() );
	CHECK( row_text( terminal, 0 ).empty() );
}
