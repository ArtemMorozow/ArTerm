#pragma once

#include "core/Result.hpp"
#include "ssh/SftpSession.hpp"
#include "ssh/SshTypes.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QThread;

namespace arterm::ssh {
class SessionInteraction;
}

namespace arterm::session {

/// GUI-thread facade over `ssh::SftpSession`.
///
/// It owns the worker thread, forwards requests to it with queued invocations
/// and re-emits the results. Callers get a request id back and match it against
/// the signals; nothing here ever blocks the GUI thread.
class RemoteFileService : public QObject {
    Q_OBJECT

public:
    RemoteFileService(ssh::HostProfile profile,
                      std::shared_ptr<ssh::SessionInteraction> interaction,
                      QObject *parent = nullptr);
    ~RemoteFileService() override;

    /// Connect and initialise the SFTP subsystem.
    void start();

    /// Close the connection and join the worker thread.
    void stop();

    [[nodiscard]] bool isConnected() const noexcept { return m_connected; }
    [[nodiscard]] const QString &homeDirectory() const noexcept { return m_homeDirectory; }

    void setTransferBackend(ssh::TransferBackend backend);

    // Each call returns the request id the matching signal will carry.
    quint64 listDirectory(const QString &path);
    quint64 resolvePath(const QString &path);
    quint64 makeDirectory(const QString &path);
    quint64 removeEntry(const QString &path, bool recursive);
    quint64 renameEntry(const QString &from, const QString &to);
    quint64 changePermissions(const QString &path, quint32 mode);
    quint64 download(const QString &remotePath, const QString &localPath);
    quint64 upload(const QString &localPath, const QString &remotePath);

    /// Thread safe; a running transfer stops at its next chunk boundary.
    void cancel(quint64 requestId);

Q_SIGNALS:
    void ready(const QString &homeDirectory);
    void failed(const arterm::Error &error);
    void disconnected();

    void listingReady(quint64 requestId, const QString &path, const arterm::ssh::RemoteListing &entries);
    void pathResolved(quint64 requestId, const QString &path);
    void operationFinished(quint64 requestId);
    void operationFailed(quint64 requestId, const arterm::Error &error);

    void transferStarted(quint64 requestId, quint64 totalBytes);
    void transferProgress(quint64 requestId, const arterm::ssh::TransferProgress &progress);
    void transferFinished(quint64 requestId);

private:
    [[nodiscard]] quint64 nextRequestId();

    QThread *m_thread{nullptr};
    ssh::SftpSession *m_session{nullptr}; ///< Owned by the worker thread.

    QString m_homeDirectory;
    std::atomic<bool> m_connected{false};
    std::atomic<quint64> m_nextRequestId{1};
};

} // namespace arterm::session
