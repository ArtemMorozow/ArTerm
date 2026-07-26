#pragma once

namespace arterm::term
{

	/// Column count of a code point in a monospaced grid.
	///
	/// Returns 0 for combining marks and other zero-width code points, 2 for East
	/// Asian Wide/Fullwidth characters and emoji, and 1 for everything else. This
	/// is a self-contained equivalent of `wcwidth(3)`: the libc version depends on
	/// the process locale, which would make rendering differ between machines.
	[[nodiscard]] int character_width( char32_t code_point ) noexcept;

} // namespace arterm::term
