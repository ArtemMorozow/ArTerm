#include "files/LocalFileModel.hpp"

#include "files/FileMimeData.hpp"
#include "ui/Icons.hpp"

#include <QDir>
#include <QFileSystemWatcher>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace arterm::files {

LocalFileModel::LocalFileModel(QObject *parent)
    : FileListModel(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    m_path = QDir::homePath();

    // Coalesce bursts of filesystem notifications: an unpacking archive would
    // otherwise trigger hundreds of reloads a second.
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(200);
    connect(debounce, &QTimer::timeout, this, &LocalFileModel::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [debounce](const QString &) { debounce->start(); });

    reload();
}

int LocalFileModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int LocalFileModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(FileColumn::Count);
}

QVariant LocalFileModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};

    const QFileInfo &info = m_entries.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (static_cast<FileColumn>(index.column())) {
        case FileColumn::Name:
            return info.fileName();
        case FileColumn::Size:
            return info.isDir() ? QString() : formatFileSize(static_cast<quint64>(info.size()));
        case FileColumn::Modified:
            return formatTimestamp(info.lastModified());
        case FileColumn::Permissions:
            return QStringLiteral("%1%2%3%4")
                .arg(info.isDir() ? QLatin1String("d") : QLatin1String("-"),
                     info.isReadable() ? QLatin1String("r") : QLatin1String("-"),
                     info.isWritable() ? QLatin1String("w") : QLatin1String("-"),
                     info.isExecutable() ? QLatin1String("x") : QLatin1String("-"));
        case FileColumn::Count:
            break;
        }
        return {};

    case Qt::DecorationRole:
        // Only the Name column carries an icon; a glyph in every cell would be
        // noise.
        if (static_cast<FileColumn>(index.column()) != FileColumn::Name)
            return {};
        return ui::fileIcon(info.fileName(), info.isDir(), info.isSymLink());

    case Qt::ToolTipRole:
        return info.absoluteFilePath();

    case Qt::TextAlignmentRole:
        if (static_cast<FileColumn>(index.column()) == FileColumn::Size)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        return QVariant(Qt::AlignLeft | Qt::AlignVCenter);

    case PathRole:
        return info.absoluteFilePath();
    case NameRole:
        return info.fileName();
    case IsDirectoryRole:
        return info.isDir();
    case IsSymlinkRole:
        return info.isSymLink();
    case IsExecutableRole:
        return info.isExecutable() && !info.isDir();
    case SizeRole:
        return static_cast<qulonglong>(info.size());
    case ModifiedRole:
        return info.lastModified();

    default:
        return {};
    }
}

QVariant LocalFileModel::headerData(int section, Qt::Orientation orientation, int role) const
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
        return tr("Access");
    case FileColumn::Count:
        break;
    }
    return {};
}

Qt::ItemFlags LocalFileModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags base = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return base | Qt::ItemIsDropEnabled;
    return base | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}

QStringList LocalFileModel::mimeTypes() const
{
    return {QLatin1String(kUriListMimeType), QLatin1String(kRemoteFilesMimeType)};
}

Qt::DropActions LocalFileModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

QMimeData *LocalFileModel::mimeData(const QModelIndexList &indexes) const
{
    QList<QUrl> urls;

    for (const QModelIndex &index : indexes) {
        if (index.column() != 0)
            continue;
        const QString path = index.data(PathRole).toString();
        if (!path.isEmpty())
            urls << QUrl::fromLocalFile(path);
    }

    if (urls.isEmpty())
        return nullptr;

    // Plain file URLs: this is what Finder and every other application expects,
    // so a drag out of ArTerm behaves like a drag out of a Finder window.
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}

QString LocalFileModel::parentPath() const
{
    QDir directory(m_path);
    if (directory.isRoot())
        return {};
    directory.cdUp();
    return directory.absolutePath();
}

QString LocalFileModel::childPath(const QString &name) const
{
    return QDir(m_path).absoluteFilePath(name);
}

void LocalFileModel::setCurrentPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir()) {
        Q_EMIT errorOccurred(tr("%1 is not a directory").arg(path));
        return;
    }
    if (!info.isReadable()) {
        Q_EMIT errorOccurred(tr("Cannot read %1: permission denied").arg(path));
        return;
    }

    m_path = info.absoluteFilePath();
    reload();
    Q_EMIT pathChanged(m_path);
}

void LocalFileModel::refresh()
{
    reload();
}

void LocalFileModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    reload();
}

void LocalFileModel::reload()
{
    setLoading(true);

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (m_showHidden)
        filters |= QDir::Hidden;

    QDir directory(m_path);
    auto entries = directory.entryInfoList(filters, QDir::NoSort);

    // Directories first, then case-insensitive by name, matching the remote pane.
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &a, const QFileInfo &b) {
        if (a.isDir() != b.isDir())
            return a.isDir();
        return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
    });

    beginResetModel();
    m_entries = QVector<QFileInfo>(entries.begin(), entries.end());
    endResetModel();

    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    m_watcher->addPath(m_path);

    setLoading(false);
}

void LocalFileModel::createDirectory(const QString &name)
{
    if (name.isEmpty())
        return;

    if (!QDir(m_path).mkdir(name)) {
        Q_EMIT errorOccurred(tr("Cannot create the folder %1").arg(name));
        return;
    }

    reload();
    Q_EMIT contentChanged();
}

void LocalFileModel::rename(const QString &path, const QString &newName)
{
    const QFileInfo info(path);
    const QString target = info.absoluteDir().absoluteFilePath(newName);

    if (QFileInfo::exists(target)) {
        Q_EMIT errorOccurred(tr("%1 already exists").arg(newName));
        return;
    }

    if (!QFile::rename(path, target)) {
        Q_EMIT errorOccurred(tr("Cannot rename %1").arg(info.fileName()));
        return;
    }

    reload();
    Q_EMIT contentChanged();
}

void LocalFileModel::remove(const QStringList &paths)
{
    QStringList failures;

    for (const QString &path : paths) {
        const QFileInfo info(path);
        const bool removed = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively()
                                                               : QFile::remove(path);
        if (!removed)
            failures << info.fileName();
    }

    if (!failures.isEmpty())
        Q_EMIT errorOccurred(tr("Cannot delete: %1").arg(failures.join(QLatin1String(", "))));

    reload();
    Q_EMIT contentChanged();
}

} // namespace arterm::files
