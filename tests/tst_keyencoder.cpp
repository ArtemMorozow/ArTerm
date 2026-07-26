#include "terminal/key_encoder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace arterm::term;

namespace
{

	std::string encode( Key key, KeyModifier modifiers = KeyModifier::NONE, std::string text = {},
						char32_t base = 0, KeyEncoder::Options const& options = {} ){
		KeyEvent event;
		event.key            = key;
		event.modifiers      = modifiers;
		event.text           = std::move( text );
		event.base_character = base;
		return KeyEncoder::encode( event, options );
	}

	std::size_t count_of( std::string_view haystack, std::string_view needle ){
		std::size_t found = 0;
		for( std::size_t at = haystack.find( needle ); at != std::string_view::npos;
			 at             = haystack.find( needle, at + 1 ) )
			++found;
		return found;
	}

} // namespace

TEST_CASE( "cursor keys use CSI by default", "[keyencoder]" ){
	CHECK( encode( Key::UP ) == "\033[A" );
	CHECK( encode( Key::DOWN ) == "\033[B" );
	CHECK( encode( Key::RIGHT ) == "\033[C" );
	CHECK( encode( Key::LEFT ) == "\033[D" );
}

TEST_CASE( "cursor keys use SS3 in application mode", "[keyencoder]" ){
	KeyEncoder::Options options;
	options.application_cursor_keys = true;

	CHECK( encode( Key::UP, KeyModifier::NONE, {}, 0, options ) == "\033OA" );
	CHECK( encode( Key::HOME, KeyModifier::NONE, {}, 0, options ) == "\033OH" );
}

TEST_CASE( "modified cursor keys use the CSI form", "[keyencoder]" ){
	// Modifier parameter: 1 + shift(1) = 2, 1 + ctrl(4) = 5.
	CHECK( encode( Key::RIGHT, KeyModifier::SHIFT ) == "\033[1;2C" );
	CHECK( encode( Key::LEFT, KeyModifier::CONTROL ) == "\033[1;5D" );
}

TEST_CASE( "function keys", "[keyencoder]" ){
	CHECK( encode( Key::F1 ) == "\033OP" );
	CHECK( encode( Key::F5 ) == "\033[15~" );
	CHECK( encode( Key::F12 ) == "\033[24~" );
	CHECK( encode( Key::PAGE_UP ) == "\033[5~" );
	CHECK( encode( Key::DELETE_FORWARD ) == "\033[3~" );
}

TEST_CASE( "control letters become C0", "[keyencoder]" ){
	CHECK( encode( Key::CHARACTER, KeyModifier::CONTROL, "c", U'c' ) == std::string( 1, '\x03' ) );
	CHECK( encode( Key::CHARACTER, KeyModifier::CONTROL, "d", U'd' ) == std::string( 1, '\x04' ) );
	CHECK( encode( Key::CHARACTER, KeyModifier::CONTROL, "a", U'a' ) == std::string( 1, '\x01' ) );
}

TEST_CASE( "control punctuation", "[keyencoder]" ){
	CHECK( encode( Key::CHARACTER, KeyModifier::CONTROL, " ", U' ' ) == std::string( 1, '\0' ) );
	CHECK( encode( Key::CHARACTER, KeyModifier::CONTROL, {}, U'[' ) == std::string( 1, '\x1B' ) );
	CHECK( encode( Key::CHARACTER, KeyModifier::CONTROL, {}, U'\\' ) == std::string( 1, '\x1C' ) );
}

TEST_CASE( "alt prefixes escape", "[keyencoder]" ){
	CHECK( encode( Key::CHARACTER, KeyModifier::ALT, "f", U'f' ) == "\033f" );
}

TEST_CASE( "command alone is left to the menu bar", "[keyencoder]" ){
	CHECK( encode( Key::CHARACTER, KeyModifier::COMMAND, "k", U'k' ).empty() );
}

TEST_CASE( "backspace sends delete", "[keyencoder]" ){
	CHECK( encode( Key::BACKSPACE ) == std::string( 1, '\x7F' ) );

	// Ctrl inverts the choice so the other byte is still reachable.
	CHECK( encode( Key::BACKSPACE, KeyModifier::CONTROL ) == std::string( 1, '\x08' ) );

	KeyEncoder::Options options;
	options.backspace_sends_delete = false;
	CHECK( encode( Key::BACKSPACE, KeyModifier::NONE, {}, 0, options ) == std::string( 1, '\x08' ) );
}

TEST_CASE( "enter respects new line mode", "[keyencoder]" ){
	CHECK( encode( Key::RETURN ) == "\r" );

	KeyEncoder::Options options;
	options.new_line_mode = true;
	CHECK( encode( Key::RETURN, KeyModifier::NONE, {}, 0, options ) == "\r\n" );
}

TEST_CASE( "modifier keys alone produce nothing", "[keyencoder]" ){
	CHECK( encode( Key::MODIFIER ).empty() );
	CHECK( encode( Key::MODIFIER, KeyModifier::SHIFT ).empty() );
}

TEST_CASE( "bracketed paste wraps the text", "[keyencoder]" ){
	CHECK( KeyEncoder::encode_paste( "ls -la", true ) == "\033[200~ls -la\033[201~" );
}

TEST_CASE( "bracketed paste cannot be escaped", "[keyencoder]" ){
	// Clipboard content containing the end marker must not be able to close the
	// paste early and have the remainder executed as typed input.
	std::string const encoded = KeyEncoder::encode_paste( "safe\033[201~rm -rf /", true );

	CHECK( count_of( encoded, "\033[201~" ) == 1 );
	CHECK( encoded.ends_with( "\033[201~" ) );
}

TEST_CASE( "unbracketed paste strips controls", "[keyencoder]" ){
	std::string const encoded = KeyEncoder::encode_paste( "a\033[31mb\x07" "c", false );

	CHECK( encoded.find( '\033' ) == std::string::npos );
	CHECK( encoded.find( '\x07' ) == std::string::npos );

	// Newlines survive, because a multi-line paste is legitimate.
	CHECK( KeyEncoder::encode_paste( "one\ntwo", false ) == "one\rtwo" );
	CHECK( KeyEncoder::encode_paste( "one\r\ntwo", false ) == "one\rtwo" );
}
