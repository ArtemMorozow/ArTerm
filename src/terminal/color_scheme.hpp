#pragma once

#include "core/rgb.hpp"
#include "terminal/cell.hpp"

#include <array>
#include <string>
#include <vector>

namespace arterm::term
{

	/// The 256-entry ANSI palette plus the handful of colours a terminal needs
	/// beyond it (background, foreground, cursor, selection).
	///
	/// ArTerm ships one scheme by default - the same low-contrast dark palette the
	/// rest of the UI uses - but the type is a value so alternative schemes can be
	/// swapped in without touching the renderer.
	class ColorScheme
	{
	public:
		ColorScheme();

		static ColorScheme arterm_dark();
		static ColorScheme arterm_light();

		/// Named schemes available in the settings UI.
		[[nodiscard]] static std::vector<std::string> available_schemes();
		[[nodiscard]] static ColorScheme              by_name( std::string_view name );

		[[nodiscard]] std::string const& name() const noexcept { return _name; }

		[[nodiscard]] Rgb background() const noexcept { return _background; }
		[[nodiscard]] Rgb foreground() const noexcept { return _foreground; }
		[[nodiscard]] Rgb cursor() const noexcept { return _cursor; }
		[[nodiscard]] Rgb cursor_text() const noexcept { return _cursor_text; }
		[[nodiscard]] Rgb selection() const noexcept { return _selection; }
		[[nodiscard]] Rgb selection_text() const noexcept { return _selection_text; }

		void set_background( Rgb const& color ) { _background = color; }
		void set_foreground( Rgb const& color ) { _foreground = color; }

		/// Palette slot 0-255.
		[[nodiscard]] Rgb indexed( std::uint8_t index ) const { return _palette[index]; }
		void              set_indexed( std::uint8_t index, Rgb const& color ) { _palette[index] = color; }

		/// Resolve a cell colour, applying the bold-brightens-colours convention
		/// that every terminal inherits from the original xterm.
		[[nodiscard]] Rgb resolve( Color const& color, bool is_foreground, bool bold ) const;

	private:
		void fill_cube();

		// These populate an existing instance. The named constructors and the
		// default constructor both route through them, so no constructor ever calls
		// another named constructor - that recursed until the stack ran out.
		static void apply_dark( ColorScheme& scheme );
		static void apply_light( ColorScheme& scheme );

		std::string          _name;
		std::array<Rgb, 256> _palette;
		Rgb                  _background;
		Rgb                  _foreground;
		Rgb                  _cursor;
		Rgb                  _cursor_text;
		Rgb                  _selection;
		Rgb                  _selection_text;
	};

} // namespace arterm::term
