#include "terminal/box_drawing.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace arterm::term;

namespace
{

	constexpr Stroke N = Stroke::NONE;
	constexpr Stroke L = Stroke::LIGHT;
	constexpr Stroke H = Stroke::HEAVY;
	constexpr Stroke D = Stroke::DOUBLE;

} // namespace

TEST_CASE( "straight lines have two opposing arms", "[box]" ){
	CHECK( box_glyph_for( U'─' ) == BoxGlyph{ N, N, L, L } );
	CHECK( box_glyph_for( U'│' ) == BoxGlyph{ L, L, N, N } );
	CHECK( box_glyph_for( U'━' ) == BoxGlyph{ N, N, H, H } );
	CHECK( box_glyph_for( U'┃' ) == BoxGlyph{ H, H, N, N } );
	CHECK( box_glyph_for( U'═' ) == BoxGlyph{ N, N, D, D } );
	CHECK( box_glyph_for( U'║' ) == BoxGlyph{ D, D, N, N } );
}

TEST_CASE( "corners point inwards", "[box]" ){
	// A top-left corner reaches down and right, never up or left - that is what
	// makes neighbouring cells join up instead of leaving a gap.
	CHECK( box_glyph_for( U'┌' ) == BoxGlyph{ N, L, N, L } );
	CHECK( box_glyph_for( U'┐' ) == BoxGlyph{ N, L, L, N } );
	CHECK( box_glyph_for( U'└' ) == BoxGlyph{ L, N, N, L } );
	CHECK( box_glyph_for( U'┘' ) == BoxGlyph{ L, N, L, N } );

	CHECK( box_glyph_for( U'╔' ) == BoxGlyph{ N, D, N, D } );
	CHECK( box_glyph_for( U'╗' ) == BoxGlyph{ N, D, D, N } );
	CHECK( box_glyph_for( U'╚' ) == BoxGlyph{ D, N, N, D } );
	CHECK( box_glyph_for( U'╝' ) == BoxGlyph{ D, N, D, N } );
}

TEST_CASE( "rounded corners match their square counterparts", "[box]" ){
	CHECK( box_glyph_for( U'╭' ) == box_glyph_for( U'┌' ) );
	CHECK( box_glyph_for( U'╮' ) == box_glyph_for( U'┐' ) );
	CHECK( box_glyph_for( U'╯' ) == box_glyph_for( U'┘' ) );
	CHECK( box_glyph_for( U'╰' ) == box_glyph_for( U'└' ) );
}

TEST_CASE( "tees omit exactly one arm", "[box]" ){
	CHECK( box_glyph_for( U'├' ) == BoxGlyph{ L, L, N, L } );
	CHECK( box_glyph_for( U'┤' ) == BoxGlyph{ L, L, L, N } );
	CHECK( box_glyph_for( U'┬' ) == BoxGlyph{ N, L, L, L } );
	CHECK( box_glyph_for( U'┴' ) == BoxGlyph{ L, N, L, L } );

	CHECK( box_glyph_for( U'╠' ) == BoxGlyph{ D, D, N, D } );
	CHECK( box_glyph_for( U'╣' ) == BoxGlyph{ D, D, D, N } );
	CHECK( box_glyph_for( U'╦' ) == BoxGlyph{ N, D, D, D } );
	CHECK( box_glyph_for( U'╩' ) == BoxGlyph{ D, N, D, D } );
}

TEST_CASE( "crosses have all four arms", "[box]" ){
	CHECK( box_glyph_for( U'┼' ) == BoxGlyph{ L, L, L, L } );
	CHECK( box_glyph_for( U'╋' ) == BoxGlyph{ H, H, H, H } );
	CHECK( box_glyph_for( U'╬' ) == BoxGlyph{ D, D, D, D } );
	// The mixed crosses keep each axis at its own weight.
	CHECK( box_glyph_for( U'╪' ) == BoxGlyph{ L, L, D, D } );
	CHECK( box_glyph_for( U'╫' ) == BoxGlyph{ D, D, L, L } );
}

TEST_CASE( "mixed-weight junctions keep each arm's weight", "[box]" ){
	CHECK( box_glyph_for( U'┝' ) == BoxGlyph{ L, L, N, H } );
	CHECK( box_glyph_for( U'┩' ) == BoxGlyph{ H, L, H, N } );
	CHECK( box_glyph_for( U'╄' ) == BoxGlyph{ H, L, L, H } );
}

TEST_CASE( "stubs have a single arm", "[box]" ){
	CHECK( box_glyph_for( U'╴' ) == BoxGlyph{ N, N, L, N } );
	CHECK( box_glyph_for( U'╵' ) == BoxGlyph{ L, N, N, N } );
	CHECK( box_glyph_for( U'╶' ) == BoxGlyph{ N, N, N, L } );
	CHECK( box_glyph_for( U'╷' ) == BoxGlyph{ N, L, N, N } );
}

TEST_CASE( "the whole line-drawing range is covered", "[box]" ){
	// Nothing between U+2500 and U+257F may fall through to the font, or a TUI
	// would show a gap exactly where the table stops.
	for( char32_t code = 0x2500; code <= 0x257F; ++code ){
		if( code >= 0x2571 && code <= 0x2573 )
			continue; // Diagonals: no arms to describe, the font draws them.
		INFO( "code point U+" << std::hex << static_cast<std::uint32_t>( code ) );
		CHECK( box_glyph_for( code ).has_value() );
	}
}

TEST_CASE( "every glyph in the table has at least one arm", "[box]" ){
	for( char32_t code = 0x2500; code <= 0x256C; ++code ){
		auto const glyph = box_glyph_for( code );
		if( !glyph )
			continue;
		INFO( "code point U+" << std::hex << static_cast<std::uint32_t>( code ) );
		CHECK( ( glyph->up != N || glyph->down != N || glyph->left != N || glyph->right != N ) );
	}
}

TEST_CASE( "blocks are recognised and lines are not", "[box]" ){
	CHECK( block_glyph_for( U'█' ) == BlockGlyph::FULL );
	CHECK( block_glyph_for( U'▀' ) == BlockGlyph::UPPER_HALF );
	CHECK( block_glyph_for( U'▄' ) == BlockGlyph::LOWER_HALF );
	CHECK( block_glyph_for( U'▌' ) == BlockGlyph::LEFT_HALF );
	CHECK( block_glyph_for( U'▐' ) == BlockGlyph::RIGHT_HALF );
	CHECK( block_glyph_for( U'░' ) == BlockGlyph::LIGHT_SHADE );
	CHECK( block_glyph_for( U'▒' ) == BlockGlyph::MEDIUM_SHADE );
	CHECK( block_glyph_for( U'▓' ) == BlockGlyph::DARK_SHADE );

	CHECK_FALSE( block_glyph_for( U'─' ).has_value() );
	CHECK_FALSE( box_glyph_for( U'█' ).has_value() );
}

TEST_CASE( "ordinary text is left to the font", "[box]" ){
	CHECK_FALSE( box_glyph_for( U'A' ).has_value() );
	CHECK_FALSE( box_glyph_for( U' ' ).has_value() );
	CHECK_FALSE( box_glyph_for( U'日' ).has_value() );
	CHECK_FALSE( box_glyph_for( 0x24FF ).has_value() );
	CHECK_FALSE( box_glyph_for( 0x2580 ).has_value() );
}
