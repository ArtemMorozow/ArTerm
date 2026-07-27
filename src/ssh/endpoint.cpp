#include "ssh/endpoint.hpp"

#include <charconv>

namespace arterm::ssh
{
	namespace
	{

		std::string_view trimmed( std::string_view text ){
			auto const begin = text.find_first_not_of( " \t\r\n" );
			if( begin == std::string_view::npos )
				return {};
			auto const end = text.find_last_not_of( " \t\r\n" );
			return text.substr( begin, end - begin + 1 );
		}

		std::optional<std::uint16_t> parse_port( std::string_view text ){
			if( text.empty() )
				return std::nullopt;

			unsigned   value  = 0;
			auto const parsed = std::from_chars( text.data(), text.data() + text.size(), value );
			if( parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() )
				return std::nullopt;
			if( value == 0 || value > 65535 )
				return std::nullopt;

			return static_cast<std::uint16_t>( value );
		}

		/// A hostname must not contain characters that would mean something else
		/// further down - a slash would be a path, whitespace a second argument.
		bool is_plausible_hostname( std::string_view text ){
			if( text.empty() )
				return false;

			for( char const c : text ){
				if( c == '/' || c == '\\' || c == ' ' || c == '\t' || c == '@' || c == '[' || c == ']' )
					return false;
			}
			return true;
		}

	} // namespace

	std::optional<Endpoint> parse_endpoint( std::string_view text ){
		std::string_view rest = trimmed( text );
		if( rest.empty() )
			return std::nullopt;

		// A scheme is accepted so a pasted ssh:// URL works, then discarded.
		if( rest.starts_with( "ssh://" ) )
			rest.remove_prefix( 6 );

		Endpoint endpoint;

		// The last '@' splits user from host: a password is not accepted here, but
		// a username may legitimately contain one.
		if( auto const at = rest.rfind( '@' ); at != std::string_view::npos ){
			endpoint.username = std::string( rest.substr( 0, at ) );
			rest              = rest.substr( at + 1 );
			if( endpoint.username.empty() )
				return std::nullopt;
		}

		// A trailing slash is what a pasted URL leaves behind.
		if( rest.ends_with( '/' ) )
			rest.remove_suffix( 1 );

		if( rest.starts_with( '[' ) ){
			// IPv6 literal: the brackets are what separate the address from the
			// port, since the address itself is full of colons.
			auto const close = rest.find( ']' );
			if( close == std::string_view::npos || close == 1 )
				return std::nullopt;

			endpoint.hostname = std::string( rest.substr( 1, close - 1 ) );

			std::string_view const tail = rest.substr( close + 1 );
			if( !tail.empty() ){
				if( !tail.starts_with( ':' ) )
					return std::nullopt;
				auto const port = parse_port( tail.substr( 1 ) );
				if( !port )
					return std::nullopt;
				endpoint.port = *port;
			}

			return endpoint.hostname.empty() ? std::nullopt : std::optional( endpoint );
		}

		if( auto const colon = rest.rfind( ':' ); colon != std::string_view::npos ){
			auto const port = parse_port( rest.substr( colon + 1 ) );
			if( !port )
				return std::nullopt;
			endpoint.port = *port;
			rest          = rest.substr( 0, colon );
		}

		if( !is_plausible_hostname( rest ) )
			return std::nullopt;

		endpoint.hostname = std::string( rest );
		return endpoint;
	}

} // namespace arterm::ssh
