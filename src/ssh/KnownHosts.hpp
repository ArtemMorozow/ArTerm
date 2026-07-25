#pragma once

#include "core/Result.hpp"
#include "ssh/SshTypes.hpp"

#include <QString>

using LIBSSH2_SESSION = struct _LIBSSH2_SESSION;

namespace arterm::ssh {

/// Reads and writes OpenSSH's `~/.ssh/known_hosts` through libssh2's
/// knownhost API, so ArTerm and the `ssh` CLI trust the same set of keys.
class KnownHosts {
public:
    /// Defaults to `~/.ssh/known_hosts`.
    KnownHosts();
    explicit KnownHosts(QString filePath);

    [[nodiscard]] const QString &filePath() const noexcept { return m_filePath; }

    /// Compare the key the server just presented against the file.
    [[nodiscard]] Result<HostKeyInfo> check(LIBSSH2_SESSION *session, const QString &hostname,
                                            quint16 port) const;

    /// Append (or replace) the entry for this host and rewrite the file.
    [[nodiscard]] Status store(LIBSSH2_SESSION *session, const HostKeyInfo &info) const;

    /// Drop every entry for a host, e.g. after a key rotation.
    [[nodiscard]] Status remove(LIBSSH2_SESSION *session, const QString &hostname, quint16 port) const;

    /// "SHA256:..." fingerprint in the format printed by ssh-keygen.
    [[nodiscard]] static QString sha256Fingerprint(LIBSSH2_SESSION *session);
    [[nodiscard]] static QString md5Fingerprint(LIBSSH2_SESSION *session);
    [[nodiscard]] static QString keyTypeName(int libssh2HostKeyType);

private:
    /// libssh2 stores non-standard ports as "[host]:port".
    [[nodiscard]] static QByteArray knownHostsHost(const QString &hostname, quint16 port);

    QString m_filePath;
};

} // namespace arterm::ssh
