#include "terminal/vt_parser.hpp"

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

	/// Records every event so a test can assert on the exact sequence produced.
	class RecordingHandler : public VtHandler
	{
	public:
		std::string               printed;
		std::vector<std::uint8_t> executed;
		std::vector<CsiSequence>  csi;
		std::vector<EscSequence>  esc;
		std::vector<std::string>  osc;

		void print( char32_t code_point ) override { append_utf8( printed, code_point ); }
		void execute( std::uint8_t control ) override { executed.push_back( control ); }
		void csi_dispatch( CsiSequence const& sequence ) override { csi.push_back( sequence ); }
		void esc_dispatch( EscSequence const& sequence ) override { esc.push_back( sequence ); }
		void osc_dispatch( std::string const& payload ) override { osc.push_back( payload ); }
	};

	constexpr std::string_view REPLACEMENT = "\xEF\xBF\xBD";

} // namespace

TEST_CASE( "plain text is printed", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "hello" ) );

	CHECK( handler.printed == "hello" );
	CHECK( handler.csi.empty() );
}

TEST_CASE( "control characters are executed", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "a\r\nb\t" ) );

	CHECK( handler.printed == "ab" );
	CHECK( handler.executed == std::vector<std::uint8_t>{ 0x0D, 0x0A, 0x09 } );
}

TEST_CASE( "CSI parameters are parsed", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "\033[12;34H" ) );

	REQUIRE( handler.csi.size() == 1 );
	CHECK( handler.csi.front().final == 'H' );
	CHECK( handler.csi.front().parameter_count == 2 );
	CHECK( handler.csi.front().parameter( 0 ) == 12 );
	CHECK( handler.csi.front().parameter( 1 ) == 34 );
}

TEST_CASE( "an omitted parameter uses the fallback", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	// "CSI ;5H" means row default, column 5.
	parser.parse( std::string( "\033[;5H" ) );

	REQUIRE( handler.csi.size() == 1 );
	CsiSequence const& sequence = handler.csi.front();
	CHECK( sequence.positive_parameter( 0 ) == 1 );
	CHECK( sequence.positive_parameter( 1 ) == 5 );
}

TEST_CASE( "the private marker is captured", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "\033[?1049h" ) );

	REQUIRE( handler.csi.size() == 1 );
	CHECK( handler.csi.front().private_marker == '?' );
	CHECK( handler.csi.front().parameter( 0 ) == 1049 );
	CHECK( handler.csi.front().final == 'h' );
}

TEST_CASE( "OSC ends on BEL", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "\033]0;my title\007rest" ) );

	REQUIRE( handler.osc.size() == 1 );
	CHECK( handler.osc.front() == "0;my title" );
	CHECK( handler.printed == "rest" );
}

TEST_CASE( "OSC ends on the string terminator", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	// The "ESC \" form must not lose the payload collected so far.
	parser.parse( std::string( "\033]2;title\033\\ok" ) );

	REQUIRE( handler.osc.size() == 1 );
	CHECK( handler.osc.front() == "2;title" );
	CHECK( handler.printed == "ok" );
}

TEST_CASE( "escape aborts an incomplete sequence", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "\033[12\033[5A" ) );

	REQUIRE( handler.csi.size() == 1 );
	CHECK( handler.csi.front().final == 'A' );
	CHECK( handler.csi.front().parameter( 0 ) == 5 );
}

TEST_CASE( "UTF-8 is decoded", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	std::string const text = "héllo — 日本 🙂";
	parser.parse( text );

	CHECK( handler.printed == text );
}

TEST_CASE( "invalid UTF-8 becomes the replacement character", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	// Two separate errors: a lone continuation byte, then a two-byte lead whose
	// continuation never arrives. Each produces its own replacement character,
	// and the byte that broke the sequence is still printed.
	parser.parse( std::string( "\x80\xC3", 2 ) );
	parser.parse( std::string( "A" ) );

	CHECK( handler.printed == std::string( REPLACEMENT ) + std::string( REPLACEMENT ) + "A" );

	// An overlong encoding of '/' must not decode back to '/', which is the
	// classic path-traversal trick.
	RecordingHandler overlong;
	VtParser         overlong_parser( overlong );
	overlong_parser.parse( std::string( "\xC0\xAF", 2 ) );
	CHECK( overlong.printed == REPLACEMENT );
}

TEST_CASE( "split input is resumed", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	// A sequence arriving in three separate reads must still parse as one.
	parser.parse( std::string( "\033[" ) );
	parser.parse( std::string( "31" ) );
	parser.parse( std::string( "m" ) );

	REQUIRE( handler.csi.size() == 1 );
	CHECK( handler.csi.front().final == 'm' );
	CHECK( handler.csi.front().parameter( 0 ) == 31 );
}

TEST_CASE( "parameter overflow is clamped", "[vtparser]" ){
	RecordingHandler handler;
	VtParser         parser( handler );

	parser.parse( std::string( "\033[99999999999999999999A" ) );

	REQUIRE( handler.csi.size() == 1 );
	// The exact ceiling does not matter; not overflowing does.
	CHECK( handler.csi.front().parameter( 0 ) > 0 );
}
