#pragma once

#include <string>

namespace arterm
{

	/// Random (version 4) UUID in the canonical lowercase-hex form,
	/// e.g. "1b4e28ba-2fa1-11d2-883f-0016d3cca427".
	[[nodiscard]] std::string generate_uuid();

} // namespace arterm
