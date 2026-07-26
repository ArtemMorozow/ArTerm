#include "terminal/char_width.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace arterm::term;

TEST_CASE( "ASCII is single width", "[charwidth]" ){
	for( char32_t c = U' '; c <= U'~'; ++c )
		REQUIRE( character_width( c ) == 1 );
}

TEST_CASE( "combining marks are zero width", "[charwidth]" ){
	CHECK( character_width( 0x0301 ) == 0 ); // Combining acute accent.
	CHECK( character_width( 0x0300 ) == 0 ); // Combining grave accent.
	CHECK( character_width( 0xFE0F ) == 0 ); // Variation selector-16.
	CHECK( character_width( 0x200B ) == 0 ); // Zero-width space.
}

TEST_CASE( "CJK is double width", "[charwidth]" ){
	CHECK( character_width( U'\u65e5' ) == 2 );
	CHECK( character_width( U'\u672c' ) == 2 );
	CHECK( character_width( U'\ud55c' ) == 2 ); // Hangul syllable.
	CHECK( character_width( U'\u3042' ) == 2 ); // Hiragana.
	CHECK( character_width( 0xFF21 ) == 2 );     // Fullwidth A.
}

TEST_CASE( "emoji is double width", "[charwidth]" ){
	CHECK( character_width( 0x1F600 ) == 2 ); // Grinning face.
	CHECK( character_width( 0x1F680 ) == 2 ); // Rocket.
	CHECK( character_width( 0x2705 ) == 2 );  // White heavy check mark.
}

TEST_CASE( "latin accents are single width", "[charwidth]" ){
	CHECK( character_width( U'\u00e9' ) == 1 );
	CHECK( character_width( U'\u00fc' ) == 1 );
	CHECK( character_width( U'\u00f1' ) == 1 );
}

TEST_CASE( "cyrillic is single width", "[charwidth]" ){
	CHECK( character_width( U'\u0416' ) == 1 );
	CHECK( character_width( U'\u043f' ) == 1 );
}

TEST_CASE( "control characters are zero width", "[charwidth]" ){
	CHECK( character_width( 0x00 ) == 0 );
	CHECK( character_width( 0x07 ) == 0 );
	CHECK( character_width( 0x1B ) == 0 );
}

TEST_CASE( "box drawing is single width", "[charwidth]" ){
	// ncurses borders must line up, so these must never be reported as wide.
	CHECK( character_width( 0x2500 ) == 1 );
	CHECK( character_width( 0x2502 ) == 1 );
	CHECK( character_width( 0x253C ) == 1 );
	CHECK( character_width( 0x2592 ) == 1 );
}
