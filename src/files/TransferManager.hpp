#pragma once

#include "ssh/SshTypes.hpp"

#include <QAbstractTableModel>
#include <QElapsedTimer>
#include <QList>
#include <QString>

namespace arterm::session {
class RemoteFileService;
}

namespace arterm::files {

/// One queued or running copy between the local and the remote side.
struct TransferJob {
    quint64 id{0};
    quint64 requestId{0}; ///< Id used by RemoteFileService while running.
    ssh::TransferDirection direction{ssh::TransferDirection::Download};
    ssh::TransferState state{ssh::TransferState::Queued};

    QString sourcePath;
    QString destinationPath;
    QString displayName;

    ssh::TransferProgress progress;
    QString errorMessage;

    QElapsedTimer clock;
};

/// The transfer queue behind the file browser's bottom panel.
///
/// Jobs run strictly one at a time: they all share a single SFTP connection, so
/// running two in parallel would only interleave them on the same socket while
/// making progress reporting meaningless.
class TransferManager : public QAbstractTableModel {
    Q_OBJECT

public:
    enum class Column { Name = 0, Direction, Progress, Speed, Status, Count };

    explicit TransferManager(session::RemoteFileService *service, QObject *parent = nullptr);

    // QAbstractTableModel.
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// Roles the progress delegate reads.
    enum Roles {
        FractionRole = Qt::UserRole + 100,
        StateRole,
        JobIdRole,
    };

    /// Queue a download of `remotePath` into the local directory `localDirectory`.
    void enqueueDownload(const QString &remotePath, const QString &localDirectory);

    /// Queue an upload of `localPath` into the remote directory `remoteDirectory`.
    void enqueueUpload(const QString &localPath, const QString &remoteDirectory);

    void cancel(quint64 jobId);
    void cancelAll();
    /// Drop finished, failed and cancelled rows.
    void clearCompleted();

    [[nodiscard]] bool hasActiveTransfers() const;
    [[nodiscard]] int activeCount() const;

Q_SIGNALS:
    /// A job finished successfully; the panes refresh the affected side.
    void transferCompleted(arterm::ssh::TransferDirection direction, const QString &destinationPath);
    void transferFailed(const QString &displayName, const QString &message);
    /// Aggregate progress for the status bar: 0.0-1.0, or -1 when idle.
    void queueProgressChanged(double fraction, int remaining);

private:
    void startNext();
    void finishCurrent(ssh::TransferState state, const QString &message);
    [[nodiscard]] int indexOfRequest(quint64 requestId) const;
    void emitRowChanged(int row);
    void publishQueueProgress();

    session::RemoteFileService *m_service;
    QList<TransferJob> m_jobs;
    quint64 m_nextJobId{1};
    int m_runningRow{-1};
};

} // namespace arterm::files
