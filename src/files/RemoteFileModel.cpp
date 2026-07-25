#include "files/RemoteFileModel.hpp"

#include "files/FileMimeData.hpp"
#include "session/RemoteFileService.hpp"
#include "ui/Icons.hpp"

#include <QMimeData>

namespace arterm::files {
namespace {

QString joinRemote(const QString &directory, const QString &name)
{
    if (directory.isEmpty() || directory == QLatin1String("/"))
        return QLatin1Char('/') + name;
    if (directory.endsWith(QLatin1Char('/')))
        return directory + name;
    return directory + QLatin1Char('/') + name;
}

} // namespace

RemoteFileModel::RemoteFileModel(session::RemoteFileService *service, QString sessionId,
                                 QObject *parent)
    : FileListModel(parent)
    , m_service(service)
    , m_sessionId(std::move(sessionId))
{
    connect(m_service, &session::RemoteFileService::listingReady, this,
            [this](quint64 requestId, const QString &path, const ssh::RemoteListing &entries) {
                if (requestId != m_pendingListingId)
                    return; // A listing for a directory the user already left.
                m_pendingListingId = 0;
                applyListing(path, entries);
            });

    connect(m_service, &session::RemoteFileService::operationFailed, this,
            [this](quint64 requestId, const Error &error) {
                if (requestId == m_pendingListingId) {
                    m_pendingListingId = 0;
                    setLoading(false);
                }
                if (!error.isCancellation())
                    Q_EMIT errorOccurred(error.message);
            });

    connect(m_service, &session::RemoteFileService::operationFinished, this, [this](quint64) {
        // Mutations do not report what changed, so simply re-read the directory.
        refresh();
        Q_EMIT contentChanged();
    });
}

int RemoteFileModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int RemoteFileModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(FileColumn::Count);
}

QVariant RemoteFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const ssh::RemoteFileEntry &entry = m_entries.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (static_cast<FileColumn>(index.column())) {
        case FileColumn::Name:
            return entry.name;
        case FileColumn::Size:
            return entry.isDirectory ? QString() : formatFileSize(entry.size);
        case FileColumn::Modified:
            return formatTimestamp(entry.modified);
        case FileColumn::Permissions:
            return entry.permissionString();
        case FileColumn::Count:
            break;
        }
        return {};

    case Qt::DecorationRole:
        // Only the Name column carries an icon; a glyph in every cell would be
        // noise.
        if (static_cast<FileColumn>(index.column()) != FileColumn::Name)
            return {};
        return ui::fileIcon(entry.name, entry.isDirectory, entry.isSymlink);

    case Qt::ToolTipRole:
        return entry.path;

    case Qt::TextAlignmentRole:
        if (static_cast<FileColumn>(index.column()) == FileColumn::Size)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);

    case PathRole:
        return entry.path;
    case NameRole:
        return entry.name;
    case IsDirectoryRole:
        return entry.isDirectory;
    case IsSymlinkRole:
        return entry.isSymlink;
    case IsExecutableRole:
        return entry.isExecutable && !entry.isDirectory;
    case SizeRole:
        return static_cast<qulonglong>(entry.size);
    case ModifiedRole:
        return entry.modified;

    default:
        return {};
    }
}

QVariant RemoteFileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (static_cast<FileColumn>(section)) {
    case FileColumn::Name:
        return tr("Name");
    case FileColumn::Size:
        return tr("Size");
    case FileColumn::Modified:
        return tr("Modified");
    case FileColumn::Permissions:
        return tr("Mode");
    case FileColumn::Count:
        break;
    }
    return {};
}

Qt::ItemFlags RemoteFileModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags base = QAbstractTableModel::flags(index);
    if (!m_connected)
        return base & ~Qt::ItemIsEnabled;
    if (!index.isValid())
        return base | Qt::ItemIsDropEnabled;
    return base | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QStringList RemoteFileModel::mimeTypes() const
{
    return {QLatin1String(kRemoteFilesMimeType), QLatin1String(kUriListMimeType)};
}

Qt::DropActions RemoteFileModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

QMimeData *RemoteFileModel::mimeData(const QModelIndexList &indexes) const
{
    RemoteDragPayload payload;
    payload.sessionId = m_sessionId;

    for (const QModelIndex &index : indexes) {
        if (index.column() != 0)
            continue;
        const QString path = index.data(PathRole).toString();
        if (!path.isEmpty())
            payload.paths << path;
    }

    if (!payload.isValid())
        return nullptr;

    auto *mime = new QMimeData;
    setRemotePayload(mime, payload);
    return mime;
}

QString RemoteFileModel::parentPath() const
{
    if (m_path.isEmpty() || m_path == QLatin1String("/"))
        return {};

    QString trimmed = m_path;
    while (trimmed.endsWith(QLatin1Char('/')))
        trimmed.chop(1);

    const int slash = trimmed.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("/");
    return trimmed.left(slash);
}

QString RemoteFileModel::childPath(const QString &name) const
{
    return joinRemote(m_path, name);
}

void RemoteFileModel::setConnected(bool connected, const QString &homeDirectory)
{
    m_connected = connected;

    if (!connected) {
        beginResetModel();
        m_allEntries.clear();
        m_entries.clear();
        endResetModel();
        return;
    }

    setCurrentPath(homeDirectory.isEmpty() ? QStringLiteral("/") : homeDirectory);
}

void RemoteFileModel::setCurrentPath(const QString &path)
{
    if (!m_connected) {
        Q_EMIT errorOccurred(tr("Not connected"));
        return;
    }

    m_pendingPath = path.isEmpty() ? QStringLiteral("/") : path;
    setLoading(true);
    m_pendingListingId = m_service->listDirectory(m_pendingPath);
}

void RemoteFileModel::refresh()
{
    if (!m_connected || m_path.isEmpty())
        return;
    m_pendingPath = m_path;
    setLoading(true);
    m_pendingListingId = m_service->listDirectory(m_path);
}

void RemoteFileModel::applyListing(const QString &path, const ssh::RemoteListing &entries)
{
    const bool pathChanged = (path != m_path);

    m_path = path;
    m_allEntries = entries;
    rebuildVisible();

    setLoading(false);

    if (pathChanged)
        Q_EMIT FileListModel::pathChanged(m_path);
}

void RemoteFileModel::rebuildVisible()
{
    beginResetModel();

    m_entries.clear();
    m_entries.reserve(m_allEntries.size());
    for (const ssh::RemoteFileEntry &entry : m_allEntries) {
        if (!m_showHidden && entry.name.startsWith(QLatin1Char('.')))
            continue;
        m_entries.append(entry);
    }

    endResetModel();
}

void RemoteFileModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    rebuildVisible();
}

void RemoteFileModel::createDirectory(const QString &name)
{
    if (name.isEmpty() || !m_connected)
        return;
    m_service->makeDirectory(childPath(name));
}

void RemoteFileModel::rename(const QString &path, const QString &newName)
{
    if (newName.isEmpty() || !m_connected)
        return;

    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString directory = slash > 0 ? path.left(slash) : QStringLiteral("/");
    m_service->renameEntry(path, joinRemote(directory, newName));
}

void RemoteFileModel::remove(const QStringList &paths)
{
    if (!m_connected)
        return;
    for (const QString &path : paths)
        m_service->removeEntry(path, /*recursive=*/true);
}

} // namespace arterm::files
