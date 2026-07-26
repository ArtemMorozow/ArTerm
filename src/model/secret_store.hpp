#pragma once

#include <QString>

#include <optional>

namespace arterm::model
{

	/// Stores passwords and key passphrases outside the profile file.
	///
	/// On macOS this is the login keychain, reached through Security.framework's C
	/// API, so secrets are protected by the same mechanism the system uses and are
	/// never written to disk by ArTerm itself. On other platforms the store reports
	/// itself unavailable and ArTerm falls back to prompting on every connection -
	/// deliberately, because writing a password to a plain file would be worse than
	/// asking for it.
	class SecretStore
	{
	public:
		/// True when a real secure store backs this instance.
		[[nodiscard]] static bool is_available();

		/// Human-readable name of the backing store, for the settings UI.
		[[nodiscard]] static QString backend_name();

		/// `account` is the host profile id; `kind` distinguishes the password from
		/// the key passphrase.
		[[nodiscard]] static bool store( QString const& account, QString const& kind, QString const& secret );

		[[nodiscard]] static std::optional<QString> retrieve( QString const& account, QString const& kind );

		static bool remove( QString const& account, QString const& kind );

		/// Drops every secret belonging to a profile.
		static void remove_all( QString const& account );

		static constexpr char const* PASSWORD   = "password";
		static constexpr char const* PASSPHRASE = "passphrase";
	};

} // namespace arterm::model
