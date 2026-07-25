#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <cstdint>

namespace arterm::ssh {

/// Authentication strategies offered by the host editor, tried in the order
/// listed by `HostProfile::authOrder()`.
enum class AuthMethod {
    Agent,               ///< ssh-agent / SSH_AUTH_SOCK.
    PublicKey,           ///< Explicit private key file (+ optional passphrase).
    Password,            ///< Plain password.
    KeyboardInteractive, ///< Challenge/response, answered with the password.
};

QString authMethodName(AuthMethod method);

/// Which wire protocol carries the bytes of a transfer.
///
/// Browsing is always SFTP because SCP cannot enumerate a directory. SCP is
/// offered for the actual copying because it is measurably faster on
/// high-latency links, and because some hardened hosts disable the SFTP
/// subsystem for shell users while leaving scp available.
enum class TransferBackend { Sftp, Scp };

QString transferBackendName(TransferBackend backend);

/// Everything needed to open a connection to one host.
struct HostProfile {
    QString id;    ///< Stable UUID, primary key in the host store.
    QString label; ///< Display name in the sidebar.
    QString hostname;
    quint16 port{22};
    QString username;
    QString group; ///< Optional folder name for sidebar grouping.
    QString colorTag;

    AuthMethod preferredAuth{AuthMethod::Agent};
    QString privateKeyPath;
    bool useAgent{true};

    /// Secrets are never serialised into the profile file; they live in the
    /// platform keychain and are looked up by `id`.
    QString password;
    QString keyPassphrase;

    QString startupDirectory; ///< Initial remote directory for the file pane.
    QString startupCommand;   ///< Optional command run right after login.
    bool openFileBrowser{true};

    int keepAliveSeconds{30};
    bool compression{false};
    bool strictHostKeyChecking{true};
    TransferBackend transferBackend{TransferBackend::Sftp};

    [[nodiscard]] QString displayName() const
    {
        if (!label.isEmpty())
            return label;
        if (username.isEmpty())
            return hostname;
        return username + QLatin1Char('@') + hostname;
    }

    [[nodiscard]] QString endpoint() const
    {
        return hostname + QLatin1Char(':') + QString::number(port);
    }

    /// Authentication methods to attempt, most preferred first.
    [[nodiscard]] QVector<AuthMethod> authOrder() const;
};

/// Outcome of the known_hosts check, surfaced to the user before authenticating.
enum class HostKeyVerdict {
    Match,     ///< Key already trusted.
    Unknown,   ///< Host absent from known_hosts.
    Mismatch,  ///< Host present but the key differs - possible MITM.
    Unusable,  ///< known_hosts could not be read.
};

struct HostKeyInfo {
    HostKeyVerdict verdict{HostKeyVerdict::Unknown};
    QString hostname;
    quint16 port{22};
    QString keyType;      ///< "ssh-ed25519", "ssh-rsa", ...
    QString sha256;       ///< "SHA256:base64", as printed by OpenSSH.
    QString md5;          ///< Legacy colon-separated fingerprint.
    QByteArray rawKey;    ///< Needed to persist the key on acceptance.
    int rawKeyType{0};    ///< libssh2 LIBSSH2_HOSTKEY_TYPE_*.
};

/// A single entry of a remote directory listing.
struct RemoteFileEntry {
    QString name;
    QString path; ///< Absolute remote path.
    quint64 size{0};
    QDateTime modified;
    quint32 permissions{0};
    quint32 uid{0};
    quint32 gid{0};
    bool isDirectory{false};
    bool isSymlink{false};
    bool isExecutable{false};

    /// "drwxr-xr-x" style rendering of `permissions`.
    [[nodiscard]] QString permissionString() const;
};

using RemoteListing = QVector<RemoteFileEntry>;

/// Direction and bookkeeping for one queued file transfer.
enum class TransferDirection { Upload, Download };

enum class TransferState { Queued, Running, Completed, Failed, Cancelled };

struct TransferProgress {
    quint64 transferred{0};
    quint64 total{0};
    double bytesPerSecond{0.0};

    [[nodiscard]] double fraction() const
    {
        return total == 0 ? 0.0 : static_cast<double>(transferred) / static_cast<double>(total);
    }
};

} // namespace arterm::ssh

Q_DECLARE_METATYPE(arterm::ssh::HostProfile)
Q_DECLARE_METATYPE(arterm::ssh::HostKeyInfo)
Q_DECLARE_METATYPE(arterm::ssh::RemoteFileEntry)
Q_DECLARE_METATYPE(arterm::ssh::RemoteListing)
Q_DECLARE_METATYPE(arterm::ssh::TransferProgress)
