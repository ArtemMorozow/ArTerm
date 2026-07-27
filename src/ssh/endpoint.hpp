#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace arterm::ssh
{

	/// A destination typed by hand, the way `ssh` accepts one.
	struct Endpoint
	{
		std::string   username; ///< Empty when the text carried no user part.
		std::string   hostname;
		std::uint16_t port{ 22 };

		[[nodiscard]] bool operator==( Endpoint const& ) const noexcept = default;
	};

	/// Parses `[user@]host[:port]`, with `[…]` around an IPv6 literal.
	///
	/// Returns nothing when the text cannot be a destination - an empty
	/// hostname, a port that is not a number or is out of range, an unterminated
	/// bracket. Anything accepted here is safe to hand to getaddrinfo.
	[[nodiscard]] std::optional<Endpoint> parse_endpoint( std::string_view text );

} // namespace arterm::ssh
