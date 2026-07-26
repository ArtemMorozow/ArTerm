#include "terminal/color_scheme.hpp"

#include <algorithm>
#include <cctype>

namespace arterm::term
{
	namespace
	{

		/// Steps of the 6x6x6 colour cube that occupies palette slots 16-231.
		constexpr int CUBE_STEPS[6] = { 0, 95, 135, 175, 215, 255 };

	} // namespace

	ColorScheme::ColorScheme(){
		// A default-constructed scheme is the dark one; `apply_dark` is a plain
		// mutator so this cannot recurse back into a named constructor.
		apply_dark( *this );
	}

	void ColorScheme::fill_cube(){
		for( int r = 0; r < 6; ++r ){
			for( int g = 0; g < 6; ++g ){
				for( int b = 0; b < 6; ++b ){
					int const index = 16 + 36 * r + 6 * g + b;
					_palette[static_cast<std::size_t>( index )] =
						Rgb{ static_cast<std::uint8_t>( CUBE_STEPS[r] ), static_cast<std::uint8_t>( CUBE_STEPS[g] ),
							 static_cast<std::uint8_t>( CUBE_STEPS[b] ) };
				}
			}
		}

		// Slots 232-255 are a 24-step greyscale ramp.
		for( int i = 0; i < 24; ++i ){
			int const level = 8 + i * 10;
			_palette[static_cast<std::size_t>( 232 + i )] =
				Rgb{ static_cast<std::uint8_t>( level ), static_cast<std::uint8_t>( level ),
					 static_cast<std::uint8_t>( level ) };
		}
	}

	ColorScheme ColorScheme::arterm_dark(){
		ColorScheme scheme;
		apply_dark( scheme );
		return scheme;
	}

	ColorScheme ColorScheme::arterm_light(){
		ColorScheme scheme;
		apply_light( scheme );
		return scheme;
	}

	void ColorScheme::apply_dark( ColorScheme& scheme ){
		scheme._name = "ArTerm Dark";

		// Normal colours (0-7), tuned to stay legible on the #12141A background
		// used by the rest of the application chrome.
		scheme._palette[0] = Rgb{ 0x1B, 0x1E, 0x26 }; // black
		scheme._palette[1] = Rgb{ 0xF2, 0x64, 0x6C }; // red
		scheme._palette[2] = Rgb{ 0x5C, 0xC9, 0x8A }; // green
		scheme._palette[3] = Rgb{ 0xE5, 0xB5, 0x67 }; // yellow
		scheme._palette[4] = Rgb{ 0x5B, 0xA8, 0xF7 }; // blue
		scheme._palette[5] = Rgb{ 0xC0, 0x84, 0xF0 }; // magenta
		scheme._palette[6] = Rgb{ 0x4F, 0xC1, 0xC8 }; // cyan
		scheme._palette[7] = Rgb{ 0xC5, 0xCA, 0xD6 }; // white

		// Bright colours (8-15).
		scheme._palette[8]  = Rgb{ 0x54, 0x5A, 0x6B };
		scheme._palette[9]  = Rgb{ 0xFF, 0x83, 0x8A };
		scheme._palette[10] = Rgb{ 0x7A, 0xE3, 0xA6 };
		scheme._palette[11] = Rgb{ 0xFF, 0xCE, 0x7F };
		scheme._palette[12] = Rgb{ 0x7F, 0xBE, 0xFF };
		scheme._palette[13] = Rgb{ 0xD5, 0xA1, 0xFF };
		scheme._palette[14] = Rgb{ 0x6E, 0xDA, 0xE1 };
		scheme._palette[15] = Rgb{ 0xEE, 0xF1, 0xF7 };

		scheme.fill_cube();

		scheme._background     = Rgb{ 0x12, 0x14, 0x1A };
		scheme._foreground     = Rgb{ 0xC5, 0xCA, 0xD6 };
		scheme._cursor         = Rgb{ 0x5B, 0xA8, 0xF7 };
		scheme._cursor_text    = Rgb{ 0x0B, 0x0D, 0x12 };
		scheme._selection      = Rgb{ 0x2C, 0x3A, 0x52 };
		scheme._selection_text = Rgb{ 0xEE, 0xF1, 0xF7 };
	}

	void ColorScheme::apply_light( ColorScheme& scheme ){
		scheme._name = "ArTerm Light";

		scheme._palette[0] = Rgb{ 0x2B, 0x30, 0x3B };
		scheme._palette[1] = Rgb{ 0xC7, 0x35, 0x3F };
		scheme._palette[2] = Rgb{ 0x2A, 0x8C, 0x5A };
		scheme._palette[3] = Rgb{ 0xA1, 0x71, 0x0F };
		scheme._palette[4] = Rgb{ 0x1E, 0x66, 0xC4 };
		scheme._palette[5] = Rgb{ 0x8B, 0x45, 0xC7 };
		scheme._palette[6] = Rgb{ 0x11, 0x7D, 0x89 };
		scheme._palette[7] = Rgb{ 0x5A, 0x61, 0x70 };

		scheme._palette[8]  = Rgb{ 0x8C, 0x93, 0xA3 };
		scheme._palette[9]  = Rgb{ 0xE2, 0x50, 0x5A };
		scheme._palette[10] = Rgb{ 0x38, 0xA8, 0x6E };
		scheme._palette[11] = Rgb{ 0xC0, 0x8C, 0x22 };
		scheme._palette[12] = Rgb{ 0x36, 0x81, 0xE0 };
		scheme._palette[13] = Rgb{ 0xA6, 0x5F, 0xE0 };
		scheme._palette[14] = Rgb{ 0x1E, 0x99, 0xA6 };
		scheme._palette[15] = Rgb{ 0x2B, 0x30, 0x3B };

		scheme.fill_cube();

		scheme._background     = Rgb{ 0xFB, 0xFC, 0xFE };
		scheme._foreground     = Rgb{ 0x2B, 0x30, 0x3B };
		scheme._cursor         = Rgb{ 0x1E, 0x66, 0xC4 };
		scheme._cursor_text    = Rgb{ 0xFF, 0xFF, 0xFF };
		scheme._selection      = Rgb{ 0xCF, 0xE1, 0xFB };
		scheme._selection_text = Rgb{ 0x18, 0x1B, 0x22 };
	}

	std::vector<std::string> ColorScheme::available_schemes(){
		return { "ArTerm Dark", "ArTerm Light" };
	}

	ColorScheme ColorScheme::by_name( std::string_view name ){
		auto const equals_ignoring_case = []( std::string_view lhs, std::string_view rhs ){
			return std::ranges::equal( lhs, rhs, []( char a, char b ){
				return std::tolower( static_cast<unsigned char>( a ) ) ==
					   std::tolower( static_cast<unsigned char>( b ) );
			} );
		};

		if( equals_ignoring_case( name, "ArTerm Light" ) )
			return arterm_light();
		return arterm_dark();
	}

	Rgb ColorScheme::resolve( Color const& color, bool is_foreground, bool bold ) const{
		switch( color.kind() ){
			case Color::Kind::DEFAULT:
				return is_foreground ? _foreground : _background;

			case Color::Kind::RGB:
				return Rgb{ color.red(), color.green(), color.blue() };

			case Color::Kind::INDEXED:{
				std::uint8_t index = color.index();
				// Bold text picks the bright variant of the eight base colours, which
				// is what applications assume when they emit "SGR 1;31".
				if( bold && is_foreground && index < 8 )
					index = static_cast<std::uint8_t>( index + 8 );
				return _palette[index];
			}
		}

		return is_foreground ? _foreground : _background;
	}

} // namespace arterm::term
