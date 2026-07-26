#pragma once

#include "core/result.hpp"
#include "ssh/ssh_types.hpp"

#include <QString>

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
		explicit KnownHosts( QString file_path );

		[[nodiscard]] QString const& file_path() const noexcept { return _file_path; }

		/// Compare the key the server just presented against the file.
		[[nodiscard]] Result<HostKeyInfo> check( LIBSSH2_SESSION* session, QString const& hostname,
												 quint16 port ) const;

		/// Append (or replace) the entry for this host and rewrite the file.
		[[nodiscard]] Status store( LIBSSH2_SESSION* session, HostKeyInfo const& info ) const;

		/// Drop every entry for a host, e.g. after a key rotation.
		[[nodiscard]] Status remove( LIBSSH2_SESSION* session, QString const& hostname, quint16 port ) const;

		/// "SHA256:..." fingerprint in the format printed by ssh-keygen.
		[[nodiscard]] static QString sha256_fingerprint( LIBSSH2_SESSION* session );
		[[nodiscard]] static QString md5_fingerprint( LIBSSH2_SESSION* session );
		[[nodiscard]] static QString key_type_name( int libssh2_host_key_type );

	private:
		/// libssh2 stores non-standard ports as "[host]:port".
		[[nodiscard]] static QByteArray known_hosts_host( QString const& hostname, quint16 port );

		QString _file_path;
	};

} // namespace arterm::ssh
