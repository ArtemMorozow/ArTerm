#pragma once

#include "core/result.hpp"
#include "ssh/ssh_types.hpp"

#include <functional>
#include <optional>
#include <string>

using LIBSSH2_SESSION = struct _LIBSSH2_SESSION;

namespace arterm::ssh
{

	/// Owns one TCP socket plus its libssh2 session: resolve, connect, verify the
	/// host key, authenticate.
	///
	/// Every method must be called from the queue that created the object.
	/// ArTerm opens two connections per host - one driving the interactive shell,
	/// one driving SFTP - because a libssh2 session may not be used concurrently
	/// from several threads.
	class SshConnection
	{
	public:
		/// Invoked when the host key is unknown or has changed. Returning true
		/// accepts (and persists) the key, false aborts the connection.
		using HostKeyPrompt = std::function<bool( HostKeyInfo const& )>;

		/// Invoked when an interactive credential is required and the profile does
		/// not carry one. Returns std::nullopt when the user cancels.
		using CredentialPrompt = std::function<std::optional<std::string>( std::string const& prompt, bool echo )>;

		explicit SshConnection( HostProfile profile );
		~SshConnection();

		SshConnection( SshConnection const& )            = delete;
		SshConnection& operator=( SshConnection const& ) = delete;

		void set_host_key_prompt( HostKeyPrompt prompt ) { _host_key_prompt = std::move( prompt ); }
		void set_credential_prompt( CredentialPrompt prompt ) { _credential_prompt = std::move( prompt ); }

		/// Resolve, connect, handshake, verify the host key and authenticate.
		[[nodiscard]] Status open();

		/// Send a disconnect message and tear everything down. Safe to call twice.
		void close();

		[[nodiscard]] bool is_open() const noexcept { return _session != nullptr && _socket >= 0; }

		[[nodiscard]] LIBSSH2_SESSION*   session() const noexcept { return _session; }
		[[nodiscard]] int                socket_descriptor() const noexcept { return _socket; }
		[[nodiscard]] HostProfile const& profile() const noexcept { return _profile; }

		/// Blocking mode is used for setup and for SFTP; the interactive shell
		/// switches the session to non-blocking so reads can be event driven.
		void set_blocking( bool blocking );

		/// Wait until the socket is ready for whatever libssh2 last blocked on.
		/// Returns false on timeout. Used to drive EAGAIN retry loops.
		[[nodiscard]] bool wait_socket( int timeout_ms = 250 ) const;

		/// Build an `Error` from the session's last error string.
		[[nodiscard]] Error last_error( ErrorKind kind, std::string const& context ) const;

		/// The authentication method that actually succeeded.
		[[nodiscard]] AuthMethod used_auth_method() const noexcept { return _used_auth; }

		/// Server banner, if the host sent one.
		[[nodiscard]] std::string const& banner() const noexcept { return _banner; }

	private:
		[[nodiscard]] Status open_socket();
		[[nodiscard]] Status handshake();
		[[nodiscard]] Status verify_host_key();
		[[nodiscard]] Status authenticate();

		[[nodiscard]] Status auth_agent();
		[[nodiscard]] Status auth_public_key();
		[[nodiscard]] Status auth_password();
		[[nodiscard]] Status auth_keyboard_interactive();

		/// Resolve the password lazily so we only prompt when a method needs it.
		[[nodiscard]] std::optional<std::string> resolve_password();
		[[nodiscard]] std::optional<std::string> resolve_passphrase();

		HostProfile      _profile;
		HostKeyPrompt    _host_key_prompt;
		CredentialPrompt _credential_prompt;

		int              _socket{ -1 };
		LIBSSH2_SESSION* _session{ nullptr };
		AuthMethod       _used_auth{ AuthMethod::AGENT };
		std::string      _banner;

		std::optional<std::string> _cached_password;
		std::optional<std::string> _cached_passphrase;

		/// Consumed by the keyboard-interactive callback through the session
		/// abstract pointer.
		std::string _kbd_response;
	};

} // namespace arterm::ssh
