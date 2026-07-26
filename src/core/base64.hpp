#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace arterm
{

	/// Standard base64 with padding, as used by OSC 52 and by the SSH host key
	/// fingerprints. Qt provided this; libssh2 does not.
	[[nodiscard]] std::string base64_encode( std::string_view data );

	/// Returns nothing when `text` is not valid base64. Whitespace is rejected
	/// rather than skipped: every producer we read from emits it unwrapped.
	[[nodiscard]] std::optional<std::string> base64_decode( std::string_view text );

} // namespace arterm
