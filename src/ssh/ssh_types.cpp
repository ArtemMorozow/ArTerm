#include "ssh/ssh_types.hpp"

#include <algorithm>

#include <sys/stat.h>

namespace arterm::ssh
{

	std::string auth_method_name( AuthMethod method ){
		switch( method ){
			case AuthMethod::AGENT:
				return "SSH agent";
			case AuthMethod::PUBLIC_KEY:
				return "Public key";
			case AuthMethod::PASSWORD:
				return "Password";
			case AuthMethod::KEYBOARD_INTERACTIVE:
				return "Keyboard interactive";
		}
		return "Unknown";
	}

	std::string transfer_backend_name( TransferBackend backend ){
		switch( backend ){
			case TransferBackend::SFTP:
				return "SFTP";
			case TransferBackend::SCP:
				return "SCP";
		}
		return "SFTP";
	}

	std::vector<AuthMethod> HostProfile::auth_order() const{
		std::vector<AuthMethod> order;
		order.reserve( 4 );

		auto const push = [&order]( AuthMethod method ){
			if( std::ranges::find( order, method ) == order.end() )
				order.push_back( method );
		};

		push( preferred_auth );

		// Fall back through the remaining methods that this profile can satisfy.
		if( use_agent )
			push( AuthMethod::AGENT );
		if( !private_key_path.empty() )
			push( AuthMethod::PUBLIC_KEY );

		// The interactive methods come last whether or not a password is stored:
		// one that is not stored can still be asked for, which is what ssh does.
		// Gating these on a saved password meant a host whose key the agent does
		// not hold could not be reached at all.
		push( AuthMethod::PASSWORD );
		push( AuthMethod::KEYBOARD_INTERACTIVE );

		// Drop the preferred method again if the profile cannot actually satisfy it,
		// otherwise we waste a round trip on every connect.
		if( !use_agent )
			std::erase( order, AuthMethod::AGENT );
		if( private_key_path.empty() )
			std::erase( order, AuthMethod::PUBLIC_KEY );

		if( order.empty() )
			order.push_back( AuthMethod::PASSWORD );

		return order;
	}

	std::string RemoteFileEntry::permission_string() const{
		std::string result;
		result.reserve( 10 );

		if( is_symlink )
			result += 'l';
		else if( is_directory )
			result += 'd';
		else
			result += '-';

		static constexpr std::uint32_t BITS[9]    = { S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
													  S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH };
		static constexpr char          LETTERS[9] = { 'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x' };

		for( int i = 0; i < 9; ++i )
			result += ( permissions & BITS[i] ) ? LETTERS[i] : '-';

		return result;
	}

} // namespace arterm::ssh
