#pragma once

#include "core/signal.hpp"
#include "ssh/ssh_types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace arterm::model
{

	/// The saved host list.
	///
	/// Profiles live in a JSON file under the application data directory; secrets
	/// never appear in it and are looked up from `SecretStore` by profile id when a
	/// connection is made.
	///
	/// Lives on the main queue; signals fire synchronously from the mutators.
	class HostStore
	{
	public:
		HostStore();

		/// Default location: ~/Library/Application Support/ArTerm/hosts.json.
		[[nodiscard]] static std::string default_file_path();

		/// Redirects the store to another file. Used by the tests.
		void set_file_path( std::string file_path ) { _file_path = std::move( file_path ); }

		[[nodiscard]] std::vector<ssh::HostProfile> const& profiles() const noexcept { return _profiles; }
		[[nodiscard]] int count() const noexcept { return static_cast<int>( _profiles.size() ); }

		[[nodiscard]] std::optional<ssh::HostProfile> profile_by_id( std::string const& id ) const;

		/// The group names present, sorted case-insensitively.
		[[nodiscard]] std::vector<std::string> groups() const;

		/// Adds a profile, assigning an id when it has none. Returns the id.
		std::string add( ssh::HostProfile profile );
		void        update( ssh::HostProfile const& profile );
		void        remove( std::string const& id );

		/// Loads secrets from the keychain into a copy of the profile, ready to be
		/// handed to a connection.
		[[nodiscard]] ssh::HostProfile with_secrets( std::string const& id ) const;

		/// Persists `profile`'s secrets and strips them from the stored copy.
		void save_secrets( ssh::HostProfile const& profile );

		[[nodiscard]] bool load();
		[[nodiscard]] bool save() const;

		/// Imports hosts from ~/.ssh/config. Returns how many were added.
		int import_from_ssh_config( std::string const& config_path = {} );

		Signal<>                   changed;
		Signal<std::string const&> profile_added;   ///< id.
		Signal<std::string const&> profile_updated; ///< id.
		Signal<std::string const&> profile_removed; ///< id.

	private:
		[[nodiscard]] int index_of( std::string const& id ) const;

		std::vector<ssh::HostProfile> _profiles;
		std::string                   _file_path;
	};

} // namespace arterm::model
