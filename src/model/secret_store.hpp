#pragma once

#include <optional>
#include <string>

namespace arterm::model
{

	/// Stores passwords and key passphrases outside the profile file.
	///
	/// This is the login keychain, reached through Security.framework's C API, so
	/// secrets are protected by the same mechanism the system uses and are never
	/// written to disk by ArTerm itself.
	class SecretStore
	{
	public:
		/// Human-readable name of the backing store, for the settings UI.
		[[nodiscard]] static std::string backend_name();

		/// `account` is the host profile id; `kind` distinguishes the password from
		/// the key passphrase.
		[[nodiscard]] static bool store( std::string const& account, std::string const& kind,
										 std::string const& secret );

		[[nodiscard]] static std::optional<std::string> retrieve( std::string const& account, std::string const& kind );

		static bool remove( std::string const& account, std::string const& kind );

		/// Drops every secret belonging to a profile.
		static void remove_all( std::string const& account );

		static constexpr char const* PASSWORD   = "password";
		static constexpr char const* PASSPHRASE = "passphrase";
	};

} // namespace arterm::model
