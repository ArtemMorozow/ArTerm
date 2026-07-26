#pragma once

#include <cstdint>

namespace arterm
{

	/// A straight 8-bit-per-channel colour. The terminal and the theme both speak
	/// this; conversion to NSColor/CGColor happens only in the AppKit layer.
	struct Rgb
	{
		std::uint8_t red{ 0 };
		std::uint8_t green{ 0 };
		std::uint8_t blue{ 0 };
		std::uint8_t alpha{ 0xFF };

		/// `0xRRGGBB`, alpha implied opaque.
		[[nodiscard]] static constexpr Rgb from_hex( std::uint32_t value ) noexcept{
			return Rgb{ static_cast<std::uint8_t>( ( value >> 16 ) & 0xFFu ),
						static_cast<std::uint8_t>( ( value >> 8 ) & 0xFFu ), static_cast<std::uint8_t>( value & 0xFFu ),
						0xFF };
		}

		/// `0xAARRGGBB`.
		[[nodiscard]] static constexpr Rgb from_argb( std::uint32_t value ) noexcept{
			return Rgb{ static_cast<std::uint8_t>( ( value >> 16 ) & 0xFFu ),
						static_cast<std::uint8_t>( ( value >> 8 ) & 0xFFu ), static_cast<std::uint8_t>( value & 0xFFu ),
						static_cast<std::uint8_t>( ( value >> 24 ) & 0xFFu ) };
		}

		[[nodiscard]] constexpr Rgb with_alpha( std::uint8_t value ) const noexcept{
			return Rgb{ red, green, blue, value };
		}

		/// Perceptual lightness, 0-255. Used to decide dark vs light appearance.
		[[nodiscard]] constexpr int lightness() const noexcept{
			return ( 299 * red + 587 * green + 114 * blue ) / 1000;
		}

		[[nodiscard]] constexpr bool operator==( Rgb const& ) const noexcept = default;
	};

} // namespace arterm
