#include "terminal/color_scheme.hpp"

#include <catch2/catch_test_macros.hpp>

#include <format>

using namespace arterm;
using namespace arterm::term;

namespace
{

	/// Rgb has no "unset" state the way QColor did, so a populated slot is one
	/// that is opaque - `fill_cube` and the named schemes set every one of them.
	bool is_set( Rgb color ){
		return color.alpha == 0xFF;
	}

} // namespace

TEST_CASE( "default construction terminates", "[colorscheme]" ){
	// Regression: the default constructor used to call arterm_dark(), which
	// default-constructed another scheme, recursing until the stack overflowed.
	ColorScheme scheme;
	CHECK( scheme.name() == "ArTerm Dark" );
	CHECK( is_set( scheme.background() ) );
}

TEST_CASE( "named schemes are distinct", "[colorscheme]" ){
	ColorScheme const dark  = ColorScheme::arterm_dark();
	ColorScheme const light = ColorScheme::arterm_light();

	CHECK( dark.background() != light.background() );
	CHECK( dark.background().lightness() < light.background().lightness() );
	CHECK( dark.foreground().lightness() > light.foreground().lightness() );
}

TEST_CASE( "palette is fully populated", "[colorscheme]" ){
	ColorScheme const scheme = ColorScheme::arterm_dark();
	for( int i = 0; i < 256; ++i ){
		INFO( std::format( "palette slot {}", i ) );
		REQUIRE( is_set( scheme.indexed( static_cast<std::uint8_t>( i ) ) ) );
	}
}

TEST_CASE( "colour cube matches xterm", "[colorscheme]" ){
	ColorScheme const scheme = ColorScheme::arterm_dark();

	// Slot 16 is the corner of the cube: pure black.
	CHECK( scheme.indexed( 16 ) == Rgb{ 0, 0, 0 } );
	// Slot 231 is the opposite corner: pure white.
	CHECK( scheme.indexed( 231 ) == Rgb{ 255, 255, 255 } );
	// Slot 196 is 5,0,0 in the cube: full red.
	CHECK( scheme.indexed( 196 ) == Rgb{ 255, 0, 0 } );
}

TEST_CASE( "greyscale ramp is monotonic", "[colorscheme]" ){
	ColorScheme const scheme = ColorScheme::arterm_dark();

	for( int i = 233; i <= 255; ++i ){
		Rgb const previous = scheme.indexed( static_cast<std::uint8_t>( i - 1 ) );
		Rgb const current  = scheme.indexed( static_cast<std::uint8_t>( i ) );
		CHECK( current.red > previous.red );
		CHECK( current.red == current.green );
		CHECK( current.green == current.blue );
	}
}

TEST_CASE( "default colour follows the scheme", "[colorscheme]" ){
	ColorScheme const scheme = ColorScheme::arterm_dark();

	CHECK( scheme.resolve( Color::default_color(), /*is_foreground=*/true, false ) == scheme.foreground() );
	CHECK( scheme.resolve( Color::default_color(), /*is_foreground=*/false, false ) == scheme.background() );
}

TEST_CASE( "bold brightens the base colours", "[colorscheme]" ){
	ColorScheme const scheme = ColorScheme::arterm_dark();

	// "SGR 1;31" must produce bright red, i.e. slot 9 rather than slot 1.
	CHECK( scheme.resolve( Color::indexed( 1 ), true, /*bold=*/false ) == scheme.indexed( 1 ) );
	CHECK( scheme.resolve( Color::indexed( 1 ), true, /*bold=*/true ) == scheme.indexed( 9 ) );

	// Only the first eight slots brighten, and only in the foreground.
	CHECK( scheme.resolve( Color::indexed( 1 ), false, true ) == scheme.indexed( 1 ) );
	CHECK( scheme.resolve( Color::indexed( 120 ), true, true ) == scheme.indexed( 120 ) );
}

TEST_CASE( "rgb is passed through", "[colorscheme]" ){
	ColorScheme const scheme = ColorScheme::arterm_dark();
	CHECK( scheme.resolve( Color::rgb( 12, 34, 56 ), true, false ) == Rgb{ 12, 34, 56 } );
}

TEST_CASE( "by_name falls back to dark", "[colorscheme]" ){
	CHECK( ColorScheme::by_name( "ArTerm Light" ).name() == "ArTerm Light" );
	CHECK( ColorScheme::by_name( "does not exist" ).name() == "ArTerm Dark" );
}
