#include "files/FileBrowser.hpp"

#include "files/FilePane.hpp"
#include "files/LocalFileModel.hpp"
#include "files/RemoteFileModel.hpp"
#include "files/TransferManager.hpp"
#include "files/TransferPanel.hpp"
#include "session/RemoteFileService.hpp"

#include <QSplitter>
#include <QVBoxLayout>

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

} // namespace

FileBrowser::FileBrowser(session::RemoteFileService *service, QString sessionId, QWidget *parent)
    : QWidget(parent)
    , m_service(service)
    , m_sessionId(std::move(sessionId))
{
    buildUi();
    connectPanes();
}

void FileBrowser::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_localModel = new LocalFileModel(this);
    m_remoteModel = new RemoteFileModel(m_service, m_sessionId, this);
    m_transfers = new TransferManager(m_service, this);

    m_localPane = new FilePane(this);
    m_localPane->setModel(m_localModel);
    m_localPane->setTitle(tr("This Mac"));

    m_remotePane = new FilePane(this);
    m_remotePane->setModel(m_remoteModel);
    m_remotePane->setTitle(tr("Remote"));
    m_remotePane->setPlaceholder(tr("Connecting…"));

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->addWidget(m_localPane);
    m_splitter->addWidget(m_remotePane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);

    m_transferPanel = new TransferPanel(m_transfers, this);

    // The panel shares a splitter with the panes instead of pinning its own
    // height, so expanding it steals space the user can drag back.
    m_verticalSplitter = new QSplitter(Qt::Vertical, this);
    m_verticalSplitter->addWidget(m_splitter);
    m_verticalSplitter->addWidget(m_transferPanel);
    m_verticalSplitter->setStretchFactor(0, 1);
    m_verticalSplitter->setStretchFactor(1, 0);
    m_verticalSplitter->setChildrenCollapsible(false);
    m_verticalSplitter->setHandleWidth(1);

    connect(m_transferPanel, &TransferPanel::expandedChanged, this, [this](bool expanded) {
        const int total = m_verticalSplitter->height();
        const int panelHeight = expanded ? std::min(150, total / 3) : TransferPanel::headerHeight();
        m_verticalSplitter->setSizes({total - panelHeight, panelHeight});
    });

    root->addWidget(m_verticalSplitter, 1);
}

void FileBrowser::connectPanes()
{
    // -- Explicit transfer requests (double click, context menu) -------------

    connect(m_localPane, &FilePane::transferRequested, this, [this](const QStringList &paths) {
        uploadInto(paths, m_remotePane->currentDirectory());
    });
    connect(m_remotePane, &FilePane::transferRequested, this, [this](const QStringList &paths) {
        downloadInto(paths, m_localPane->currentDirectory());
    });

    // -- Drops onto the remote pane -----------------------------------------

    connect(m_remotePane, &FilePane::localFilesDropped, this,
            [this](const QStringList &paths, const QString &target) { uploadInto(paths, target); });

    connect(m_remotePane, &FilePane::remoteFilesDropped, this,
            [this](const RemoteDragPayload &payload, const QString &target) {
                if (payload.sessionId != m_sessionId) {
                    // Cross-session moves would need a second connection to
                    // relay through; refuse rather than half-doing it.
                    Q_EMIT statusMessage(tr("Files can only be moved within the same session"));
                    return;
                }
                moveRemote(payload.paths, target);
            });

    // -- Drops onto the local pane ------------------------------------------

    connect(m_localPane, &FilePane::remoteFilesDropped, this,
            [this](const RemoteDragPayload &payload, const QString &target) {
                if (payload.sessionId != m_sessionId) {
                    Q_EMIT statusMessage(tr("This drag came from another session"));
                    return;
                }
                downloadInto(payload.paths, target);
            });

    connect(m_localPane, &FilePane::localFilesDropped, this,
            [this](const QStringList &, const QString &) {
                // Local-to-local copying belongs to Finder, not to an SSH client.
                Q_EMIT statusMessage(tr("Drop files onto the remote pane to upload them"));
            });

    // -- Refresh the affected side once a transfer lands --------------------

    connect(m_transfers, &TransferManager::transferCompleted, this,
            [this](ssh::TransferDirection direction, const QString &) {
                if (direction == ssh::TransferDirection::Upload)
                    m_remotePane->refresh();
                else
                    m_localPane->refresh();
            });

    connect(m_transfers, &TransferManager::transferFailed, this,
            [this](const QString &name, const QString &message) {
                Q_EMIT statusMessage(tr("%1: %2").arg(name, message));
            });

    connect(m_remoteModel, &FileListModel::errorOccurred, this, &FileBrowser::statusMessage);
    connect(m_localModel, &FileListModel::errorOccurred, this, &FileBrowser::statusMessage);
}

void FileBrowser::setConnected(bool connected, const QString &homeDirectory)
{
    if (connected)
        m_remotePane->clearPlaceholder();
    else
        m_remotePane->setPlaceholder(tr("Disconnected"));

    m_remoteModel->setConnected(connected, homeDirectory);
}

void FileBrowser::setRemoteTitle(const QString &title)
{
    m_remotePane->setTitle(title);
}

void FileBrowser::uploadToCurrentRemoteDirectory(const QStringList &paths)
{
    uploadInto(paths, m_remotePane->currentDirectory());
}

void FileBrowser::uploadInto(const QStringList &localPaths, const QString &remoteDirectory)
{
    if (remoteDirectory.isEmpty()) {
        Q_EMIT statusMessage(tr("The remote pane is not connected yet"));
        return;
    }

    for (const QString &path : localPaths)
        m_transfers->enqueueUpload(path, remoteDirectory);

    m_transferPanel->expand();
}

void FileBrowser::downloadInto(const QStringList &remotePaths, const QString &localDirectory)
{
    if (localDirectory.isEmpty())
        return;

    for (const QString &path : remotePaths)
        m_transfers->enqueueDownload(path, localDirectory);

    m_transferPanel->expand();
}

void FileBrowser::moveRemote(const QStringList &remotePaths, const QString &remoteDirectory)
{
    for (const QString &path : remotePaths) {
        const QString name = remoteBaseName(path);
        const QString target = joinRemote(remoteDirectory, name);

        if (target == path)
            continue; // Dropped back where it came from.

        m_service->renameEntry(path, target);
    }
}

} // namespace arterm::files
