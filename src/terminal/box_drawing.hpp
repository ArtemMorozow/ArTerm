#pragma once

#include <cstdint>
#include <optional>

namespace arterm::term
{

	/// How heavy one arm of a box-drawing glyph is.
	enum class Stroke : std::uint8_t{
		NONE,
		LIGHT,
		HEAVY,
		DOUBLE,
	};

	/// A box-drawing character described as four arms reaching from the centre
	/// of the cell to its edges.
	///
	/// Terminals cannot use the font's own glyphs for these: a fallback face
	/// rarely advances by exactly one cell, so adjacent glyphs leave gaps and a
	/// TUI's frames come apart. Drawing the arms ourselves makes them tile.
	struct BoxGlyph
	{
		Stroke up{ Stroke::NONE };
		Stroke down{ Stroke::NONE };
		Stroke left{ Stroke::NONE };
		Stroke right{ Stroke::NONE };

		[[nodiscard]] constexpr bool operator==( BoxGlyph const& ) const noexcept = default;
	};

	/// Shaded and solid blocks, which are filled rather than stroked.
	enum class BlockGlyph : std::uint8_t{
		FULL,
		UPPER_HALF,
		LOWER_HALF,
		LEFT_HALF,
		RIGHT_HALF,
		LIGHT_SHADE,  ///< 25%.
		MEDIUM_SHADE, ///< 50%.
		DARK_SHADE,   ///< 75%.
	};

	/// The arms of `code_point`, or nothing when it is not a line-drawing glyph
	/// this renderer handles.
	[[nodiscard]] std::optional<BoxGlyph> box_glyph_for( char32_t code_point ) noexcept;

	/// The block `code_point` fills, or nothing when it is not one.
	[[nodiscard]] std::optional<BlockGlyph> block_glyph_for( char32_t code_point ) noexcept;

} // namespace arterm::term
