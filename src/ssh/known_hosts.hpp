#pragma once

#include "core/result.hpp"
#include "ssh/ssh_types.hpp"

#include <cstdint>
#include <string>

using LIBSSH2_SESSION = struct _LIBSSH2_SESSION;

namespace arterm::ssh
{

	/// Reads and writes OpenSSH's `~/.ssh/known_hosts` through libssh2's
	/// knownhost API, so ArTerm and the `ssh` CLI trust the same set of keys.
	class KnownHosts
	{
	public:
		/// Defaults to `~/.ssh/known_hosts`.
		KnownHosts();
		explicit KnownHosts( std::string file_path );

		[[nodiscard]] std::string const& file_path() const noexcept { return _file_path; }

		/// Compare the key the server just presented against the file.
		[[nodiscard]] Result<HostKeyInfo> check( LIBSSH2_SESSION* session, std::string const& hostname,
												 std::uint16_t port ) const;

		/// Append (or replace) the entry for this host and rewrite the file.
		[[nodiscard]] Status store( LIBSSH2_SESSION* session, HostKeyInfo const& info ) const;

		/// Drop every entry for a host, e.g. after a key rotation.
		[[nodiscard]] Status remove( LIBSSH2_SESSION* session, std::string const& hostname, std::uint16_t port ) const;

		/// "SHA256:..." fingerprint in the format printed by ssh-keygen.
		[[nodiscard]] static std::string sha256_fingerprint( LIBSSH2_SESSION* session );
		[[nodiscard]] static std::string md5_fingerprint( LIBSSH2_SESSION* session );
		[[nodiscard]] static std::string key_type_name( int libssh2_host_key_type );

	private:
		/// libssh2 stores non-standard ports as "[host]:port".
		[[nodiscard]] static std::string known_hosts_host( std::string const& hostname, std::uint16_t port );

		std::string _file_path;
	};

} // namespace arterm::ssh
