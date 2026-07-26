#pragma once

#include "core/non_copyable.hpp"
#include "ssh/ssh_types.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace arterm::ssh
{

	/// Bridges the session queues back to the main queue when a connection needs a
	/// human decision (host key trust, a missing password).
	///
	/// The UI layer installs the two handlers; both are invoked on the main queue.
	/// Workers call `confirm_host_key` / `ask_credential` directly - those marshal
	/// to the main queue with `on_main_sync`, so the calling worker parks until
	/// the user answers. A missing handler answers "rejected"/"cancelled".
	class SessionInteraction : NonCopyable
	{
	public:
		/// Returns true when the user chose to trust the key.
		using HostKeyHandler = std::function<bool( HostKeyInfo const& )>;

		/// Returns std::nullopt when the user cancelled the prompt; an empty
		/// string is a legitimate submitted answer.
		using CredentialHandler = std::function<std::optional<std::string>( std::string const& prompt, bool echo )>;

		void set_host_key_handler( HostKeyHandler handler ) { _host_key_handler = std::move( handler ); }
		void set_credential_handler( CredentialHandler handler ) { _credential_handler = std::move( handler ); }

		/// Thread safe. Blocks the calling thread until the user decides.
		[[nodiscard]] bool confirm_host_key( HostKeyInfo const& info );

		/// Thread safe. Blocks until the user submits or cancels the prompt.
		///
		/// An answer is remembered for the process lifetime so a reconnect does not
		/// ask again; nothing is persisted to disk.
		[[nodiscard]] std::optional<std::string> ask_credential( std::string const& prompt, bool echo );

		/// Drops the remembered passwords, e.g. after one was rejected.
		void forget_cached_answers();

	private:
		[[nodiscard]] static std::string host_key_cache_key( HostKeyInfo const& info );

		HostKeyHandler    _host_key_handler;
		CredentialHandler _credential_handler;

		/// Serialises prompts and remembers the answers.
		///
		/// A session opens two connections at once, so without this both would race
		/// to put up their own dialog for the same host key and the user would be
		/// asked twice for the same decision. The mutex makes the second connection
		/// wait for the first answer, and the caches let it reuse that answer
		/// instead of prompting again.
		std::mutex                                   _prompt_mutex;
		std::unordered_map<std::string, bool>        _host_key_decisions;
		std::unordered_map<std::string, std::string> _credential_answers;
		/// Prompts the user cancelled, so a retry does not re-ask immediately.
		std::unordered_set<std::string> _declined_credentials;
	};

} // namespace arterm::ssh
