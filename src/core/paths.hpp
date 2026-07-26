#pragma once

#include <string>
#include <string_view>

namespace arterm
{

	/// $HOME, falling back to the passwd entry when the environment is stripped.
	[[nodiscard]] std::string home_directory();

	/// Expands a leading "~/" against the home directory; anything else is
	/// returned untouched.
	[[nodiscard]] std::string expand_home( std::string_view path );

	/// `~/Library/Application Support/ArTerm`, created on first use.
	[[nodiscard]] std::string application_data_directory();

} // namespace arterm
