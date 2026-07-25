#pragma once

#include "core/Result.hpp"
#include "ssh/SshTypes.hpp"

#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>

using LIBSSH2_SFTP = struct _LIBSSH2_SFTP;

namespace arterm::ssh {

class SshConnection;
class SessionInteraction;

/// Worker that owns the second SSH connection of a session and serves every
/// remote filesystem request.
///
/// Requests are addressed by an id chosen by the caller so results can be
/// matched to the widget that asked for them. All slots run on the worker
/// thread; `requestCancel` is the one method safe to call from anywhere while
/// a transfer is in flight.
class SftpSession : public QObject {
    Q_OBJECT

public:
    SftpSession(HostProfile profile, std::shared_ptr<SessionInteraction> interaction,
                QObject *parent = nullptr);
    ~SftpSession() override;

    void setTransferBackend(TransferBackend backend) noexcept { m_backend = backend; }
    [[nodiscard]] TransferBackend transferBackend() const noexcept { return m_backend; }

    /// Thread safe. Marks a queued or running operation for cancellation; a
    /// running transfer notices at the next chunk boundary.
    void requestCancel(quint64 requestId);

public Q_SLOTS:
    void start();
    void shutdown();

    void listDirectory(quint64 requestId, const QString &path);
    void resolvePath(quint64 requestId, const QString &path);
    void makeDirectory(quint64 requestId, const QString &path);
    void removeEntry(quint64 requestId, const QString &path, bool recursive);
    void renameEntry(quint64 requestId, const QString &from, const QString &to);
    void changePermissions(quint64 requestId, const QString &path, quint32 mode);

    /// Copies a remote file or directory tree to `localPath`.
    void download(quint64 requestId, const QString &remotePath, const QString &localPath);

    /// Copies a local file or directory tree to `remotePath`.
    void upload(quint64 requestId, const QString &localPath, const QString &remotePath);

Q_SIGNALS:
    void ready(const QString &homeDirectory);
    void failed(const arterm::Error &error);

    void listingReady(quint64 requestId, const QString &path, const arterm::ssh::RemoteListing &entries);
    void pathResolved(quint64 requestId, const QString &path);
    void operationFinished(quint64 requestId);
    void operationFailed(quint64 requestId, const arterm::Error &error);

    void transferStarted(quint64 requestId, quint64 totalBytes);
    void transferProgress(quint64 requestId, const arterm::ssh::TransferProgress &progress);
    void transferFinished(quint64 requestId);

private:
    [[nodiscard]] bool isCancelled(quint64 requestId) const;
    void clearCancel(quint64 requestId);

    [[nodiscard]] Error sftpError(const QString &context) const;
    [[nodiscard]] Result<RemoteListing> readDirectory(const QString &path);
    [[nodiscard]] Result<RemoteFileEntry> statEntry(const QString &path);

    /// Recursive helpers; `moved` accumulates across a whole directory tree so
    /// progress stays monotonic.
    [[nodiscard]] Status downloadTree(quint64 requestId, const QString &remotePath,
                                      const QString &localPath, quint64 total, quint64 &moved);
    [[nodiscard]] Status uploadTree(quint64 requestId, const QString &localPath,
                                    const QString &remotePath, quint64 total, quint64 &moved);
    [[nodiscard]] Status downloadFile(quint64 requestId, const QString &remotePath,
                                      const QString &localPath, quint64 total, quint64 &moved);
    [[nodiscard]] Status uploadFile(quint64 requestId, const QString &localPath,
                                    const QString &remotePath, quint64 total, quint64 &moved);
    [[nodiscard]] Status removeTree(quint64 requestId, const QString &path);

    [[nodiscard]] Result<quint64> remoteTreeSize(const QString &path);
    [[nodiscard]] static quint64 localTreeSize(const QString &path);

    /// Emits a throttled progress update for `requestId`.
    void publishProgress(quint64 requestId, quint64 moved, quint64 total);

    HostProfile m_profile;
    std::shared_ptr<SessionInteraction> m_interaction;
    std::unique_ptr<SshConnection> m_connection;
    LIBSSH2_SFTP *m_sftp{nullptr};

    TransferBackend m_backend{TransferBackend::Sftp};
    QString m_homeDirectory;

    mutable std::mutex m_cancelMutex;
    QSet<quint64> m_cancelRequests;

    qint64 m_lastProgressTick{0};
    quint64 m_lastProgressBytes{0};
};

} // namespace arterm::ssh
