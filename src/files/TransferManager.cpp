#include "files/TransferManager.hpp"

#include "files/FileListModel.hpp"
#include "session/RemoteFileService.hpp"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace arterm::files {
namespace {

QString remoteBaseName(const QString &path)
{
    QString trimmed = path;
    while (trimmed.size() > 1 && trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);
    const int slash = trimmed.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? trimmed.mid(slash + 1) : trimmed;
}

QString joinRemote(const QString &directory, const QString &name)
{
    if (directory.isEmpty() || directory == QLatin1String("/"))
        return QLatin1Char('/') + name;
    if (directory.endsWith(QLatin1Char('/')))
        return directory + name;
    return directory + QLatin1Char('/') + name;
}

QString stateText(ssh::TransferState state)
{
    switch (state) {
    case ssh::TransferState::Queued:
        return QObject::tr("Queued");
    case ssh::TransferState::Running:
        return QObject::tr("Transferring");
    case ssh::TransferState::Completed:
        return QObject::tr("Done");
    case ssh::TransferState::Failed:
        return QObject::tr("Failed");
    case ssh::TransferState::Cancelled:
        return QObject::tr("Cancelled");
    }
    return {};
}

} // namespace

TransferManager::TransferManager(session::RemoteFileService *service, QObject *parent)
    : QAbstractTableModel(parent)
    , m_service(service)
{
    connect(m_service, &session::RemoteFileService::transferStarted, this,
            [this](quint64 requestId, quint64 total) {
                const int row = indexOfRequest(requestId);
                if (row < 0)
                    return;
                m_jobs[row].progress.total = total;
                emitRowChanged(row);
            });

    connect(m_service, &session::RemoteFileService::transferProgress, this,
            [this](quint64 requestId, const ssh::TransferProgress &progress) {
                const int row = indexOfRequest(requestId);
                if (row < 0)
                    return;
                m_jobs[row].progress = progress;
                emitRowChanged(row);
                publishQueueProgress();
            });

    connect(m_service, &session::RemoteFileService::transferFinished, this,
            [this](quint64 requestId) {
                if (indexOfRequest(requestId) < 0)
                    return;
                finishCurrent(ssh::TransferState::Completed, {});
            });

    connect(m_service, &session::RemoteFileService::operationFailed, this,
            [this](quint64 requestId, const Error &error) {
                // operationFailed also fires for listings and mkdir; only react
                // when the id belongs to a transfer we started.
                if (indexOfRequest(requestId) < 0)
                    return;
                finishCurrent(error.isCancellation() ? ssh::TransferState::Cancelled
                                                     : ssh::TransferState::Failed,
                              error.message);
            });
}

int TransferManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

int TransferManager::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(Column::Count);
}

QVariant TransferManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_jobs.size())
        return {};

    const TransferJob &job = m_jobs.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (static_cast<Column>(index.column())) {
        case Column::Name:
            return job.displayName;
        case Column::Direction:
            return job.direction == ssh::TransferDirection::Upload ? tr("Upload") : tr("Download");
        case Column::Progress:
            if (job.progress.total == 0)
                return QString();
            return tr("%1 / %2")
                .arg(formatFileSize(job.progress.transferred), formatFileSize(job.progress.total));
        case Column::Speed:
            if (job.state != ssh::TransferState::Running || job.progress.bytesPerSecond <= 0.0)
                return QString();
            return tr("%1/s").arg(formatFileSize(static_cast<quint64>(job.progress.bytesPerSecond)));
        case Column::Status:
            return job.state == ssh::TransferState::Failed && !job.errorMessage.isEmpty()
                       ? job.errorMessage
                       : stateText(job.state);
        case Column::Count:
            break;
        }
        return {};

    case Qt::ToolTipRole:
        return tr("%1\nto %2").arg(job.sourcePath, job.destinationPath);

    case FractionRole:
        return job.progress.fraction();
    case StateRole:
        return static_cast<int>(job.state);
    case JobIdRole:
        return static_cast<qulonglong>(job.id);

    default:
        return {};
    }
}

QVariant TransferManager::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (static_cast<Column>(section)) {
    case Column::Name:
        return tr("File");
    case Column::Direction:
        return tr("Direction");
    case Column::Progress:
        return tr("Progress");
    case Column::Speed:
        return tr("Speed");
    case Column::Status:
        return tr("Status");
    case Column::Count:
        break;
    }
    return {};
}

void TransferManager::enqueueDownload(const QString &remotePath, const QString &localDirectory)
{
    TransferJob job;
    job.id = m_nextJobId++;
    job.direction = ssh::TransferDirection::Download;
    job.sourcePath = remotePath;
    job.displayName = remoteBaseName(remotePath);
    job.destinationPath = QDir(localDirectory).absoluteFilePath(job.displayName);

    beginInsertRows({}, static_cast<int>(m_jobs.size()), static_cast<int>(m_jobs.size()));
    m_jobs.append(std::move(job));
    endInsertRows();

    startNext();
}

void TransferManager::enqueueUpload(const QString &localPath, const QString &remoteDirectory)
{
    TransferJob job;
    job.id = m_nextJobId++;
    job.direction = ssh::TransferDirection::Upload;
    job.sourcePath = localPath;
    job.displayName = QFileInfo(localPath).fileName();
    job.destinationPath = joinRemote(remoteDirectory, job.displayName);

    beginInsertRows({}, static_cast<int>(m_jobs.size()), static_cast<int>(m_jobs.size()));
    m_jobs.append(std::move(job));
    endInsertRows();

    startNext();
}

void TransferManager::startNext()
{
    if (m_runningRow >= 0)
        return;

    const auto it = std::find_if(m_jobs.begin(), m_jobs.end(), [](const TransferJob &job) {
        return job.state == ssh::TransferState::Queued;
    });
    if (it == m_jobs.end()) {
        publishQueueProgress();
        return;
    }

    const int row = static_cast<int>(std::distance(m_jobs.begin(), it));

    TransferJob &job = m_jobs[row];
    job.state = ssh::TransferState::Running;
    job.clock.start();

    job.requestId = (job.direction == ssh::TransferDirection::Download)
                        ? m_service->download(job.sourcePath, job.destinationPath)
                        : m_service->upload(job.sourcePath, job.destinationPath);

    m_runningRow = row;
    emitRowChanged(row);
    publishQueueProgress();
}

void TransferManager::finishCurrent(ssh::TransferState state, const QString &message)
{
    if (m_runningRow < 0 || m_runningRow >= m_jobs.size())
        return;

    const int row = m_runningRow;
    m_runningRow = -1;

    TransferJob &job = m_jobs[row];
    job.state = state;
    job.errorMessage = message;
    job.requestId = 0;

    if (state == ssh::TransferState::Completed)
        job.progress.transferred = job.progress.total;

    emitRowChanged(row);

    if (state == ssh::TransferState::Completed)
        Q_EMIT transferCompleted(job.direction, job.destinationPath);
    else if (state == ssh::TransferState::Failed)
        Q_EMIT transferFailed(job.displayName, message);

    startNext();
}

int TransferManager::indexOfRequest(quint64 requestId) const
{
    if (requestId == 0)
        return -1;

    for (int row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).requestId == requestId && m_jobs.at(row).state == ssh::TransferState::Running)
            return row;
    }
    return -1;
}

void TransferManager::emitRowChanged(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    Q_EMIT dataChanged(index(row, 0), index(row, static_cast<int>(Column::Count) - 1));
}

void TransferManager::cancel(quint64 jobId)
{
    for (int row = 0; row < m_jobs.size(); ++row) {
        TransferJob &job = m_jobs[row];
        if (job.id != jobId)
            continue;

        if (job.state == ssh::TransferState::Running) {
            m_service->cancel(job.requestId);
        } else if (job.state == ssh::TransferState::Queued) {
            job.state = ssh::TransferState::Cancelled;
            emitRowChanged(row);
            publishQueueProgress();
        }
        return;
    }
}

void TransferManager::cancelAll()
{
    for (int row = 0; row < m_jobs.size(); ++row) {
        TransferJob &job = m_jobs[row];
        if (job.state == ssh::TransferState::Running) {
            m_service->cancel(job.requestId);
        } else if (job.state == ssh::TransferState::Queued) {
            job.state = ssh::TransferState::Cancelled;
            emitRowChanged(row);
        }
    }
    publishQueueProgress();
}

void TransferManager::clearCompleted()
{
    for (int row = static_cast<int>(m_jobs.size()) - 1; row >= 0; --row) {
        const ssh::TransferState state = m_jobs.at(row).state;
        if (state == ssh::TransferState::Running || state == ssh::TransferState::Queued)
            continue;

        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();

        if (m_runningRow > row)
            --m_runningRow;
    }
}

bool TransferManager::hasActiveTransfers() const
{
    return activeCount() > 0;
}

int TransferManager::activeCount() const
{
    return static_cast<int>(std::count_if(m_jobs.begin(), m_jobs.end(), [](const TransferJob &job) {
        return job.state == ssh::TransferState::Running || job.state == ssh::TransferState::Queued;
    }));
}

void TransferManager::publishQueueProgress()
{
    quint64 transferred = 0;
    quint64 total = 0;
    int remaining = 0;

    for (const TransferJob &job : m_jobs) {
        if (job.state != ssh::TransferState::Running && job.state != ssh::TransferState::Queued)
            continue;
        ++remaining;
        transferred += job.progress.transferred;
        total += job.progress.total;
    }

    if (remaining == 0) {
        Q_EMIT queueProgressChanged(-1.0, 0);
        return;
    }

    const double fraction = total == 0 ? 0.0 : static_cast<double>(transferred) / static_cast<double>(total);
    Q_EMIT queueProgressChanged(fraction, remaining);
}

} // namespace arterm::files
