#pragma once

#include <cstdint>

namespace arterm::term
{

	/// A colour as it appears in an SGR sequence: either "whatever the theme says",
	/// one of the 256 palette slots, or a literal RGB triple.
	class Color
	{
	public:
		enum class Kind : std::uint8_t { DEFAULT, INDEXED, RGB };

		constexpr Color() = default;

		static constexpr Color default_color() { return Color{}; }

		static constexpr Color indexed( std::uint8_t index ){
			Color color;
			color._kind  = Kind::INDEXED;
			color._index = index;
			return color;
		}

		static constexpr Color rgb( std::uint8_t r, std::uint8_t g, std::uint8_t b ){
			Color color;
			color._kind = Kind::RGB;
			color._r    = r;
			color._g    = g;
			color._b    = b;
			return color;
		}

		[[nodiscard]] constexpr Kind         kind() const noexcept { return _kind; }
		[[nodiscard]] constexpr std::uint8_t index() const noexcept { return _index; }
		[[nodiscard]] constexpr std::uint8_t red() const noexcept { return _r; }
		[[nodiscard]] constexpr std::uint8_t green() const noexcept { return _g; }
		[[nodiscard]] constexpr std::uint8_t blue() const noexcept { return _b; }
		[[nodiscard]] constexpr bool         is_default() const noexcept { return _kind == Kind::DEFAULT; }

		friend constexpr bool operator==( Color const&, Color const& ) = default;

	private:
		Kind         _kind{ Kind::DEFAULT };
		std::uint8_t _index{ 0 };
		std::uint8_t _r{ 0 };
		std::uint8_t _g{ 0 };
		std::uint8_t _b{ 0 };
	};

	/// Rendition bits carried by every cell.
	enum class CellFlag : std::uint16_t{
		NONE             = 0,
		BOLD             = 1u << 0,
		FAINT            = 1u << 1,
		ITALIC           = 1u << 2,
		UNDERLINE        = 1u << 3,
		BLINK            = 1u << 4,
		INVERSE          = 1u << 5,
		HIDDEN           = 1u << 6,
		STRIKEOUT        = 1u << 7,
		DOUBLE_UNDERLINE = 1u << 8,
		/// First column of a double-width glyph (CJK, some emoji).
		WIDE_LEAD = 1u << 9,
		/// The placeholder column that follows a `WIDE_LEAD` cell.
		WIDE_TRAIL = 1u << 10,
	};

	constexpr CellFlag operator|( CellFlag a, CellFlag b ){
		return static_cast<CellFlag>( static_cast<std::uint16_t>( a ) | static_cast<std::uint16_t>( b ) );
	}

	constexpr CellFlag operator&( CellFlag a, CellFlag b ){
		return static_cast<CellFlag>( static_cast<std::uint16_t>( a ) & static_cast<std::uint16_t>( b ) );
	}

	constexpr CellFlag operator~( CellFlag a ){
		return static_cast<CellFlag>( ~static_cast<std::uint16_t>( a ) );
	}

	constexpr CellFlag& operator|=( CellFlag& a, CellFlag b ){
		a = a | b;
		return a;
	}

	constexpr CellFlag& operator&=( CellFlag& a, CellFlag b ){
		a = a & b;
		return a;
	}

	constexpr bool has_flag( CellFlag value, CellFlag flag ){
		return ( static_cast<std::uint16_t>( value ) & static_cast<std::uint16_t>( flag ) ) != 0;
	}

	/// The graphic rendition currently in effect, i.e. the state SGR mutates.
	struct Attributes
	{
		Color    foreground;
		Color    background;
		Color    underline_color;
		CellFlag flags{ CellFlag::NONE };

		friend constexpr bool operator==( Attributes const&, Attributes const& ) = default;

		/// SGR 0.
		constexpr void reset() { *this = Attributes{}; }
	};

	/// One character cell of the screen grid.
	struct Cell
	{
		char32_t   character{ U' ' };
		Attributes attributes;

		[[nodiscard]] constexpr bool is_blank() const { return character == U' ' || character == U'\0'; }

		/// True when the cell holds nothing that needs painting beyond its
		/// background - lets the renderer skip glyph shaping entirely.
		[[nodiscard]] constexpr bool is_empty() const{
			return is_blank() && !has_flag( attributes.flags, CellFlag::UNDERLINE ) &&
				   !has_flag( attributes.flags, CellFlag::STRIKEOUT ) &&
				   !has_flag( attributes.flags, CellFlag::DOUBLE_UNDERLINE );
		}

		friend constexpr bool operator==( Cell const&, Cell const& ) = default;
	};

} // namespace arterm::term
