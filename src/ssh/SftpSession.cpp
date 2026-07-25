#include "ssh/SftpSession.hpp"

#include "ssh/ScpTransfer.hpp"
#include "ssh/SessionInteraction.hpp"
#include "ssh/SshConnection.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcSsh)

namespace arterm::ssh {
namespace {

constexpr qint64 kChunkSize = 64 * 1024;

/// Minimum gap between two progress signals, so a fast local transfer does not
/// flood the GUI thread's event queue.
constexpr qint64 kProgressIntervalMs = 80;

QString joinRemote(const QString &directory, const QString &name)
{
    if (directory.isEmpty() || directory == QLatin1String("/"))
        return QLatin1Char('/') + name;
    if (directory.endsWith(QLatin1Char('/')))
        return directory + name;
    return directory + QLatin1Char('/') + name;
}

RemoteFileEntry entryFromAttributes(const QString &directory, const QString &name,
                                    const LIBSSH2_SFTP_ATTRIBUTES &attributes)
{
    RemoteFileEntry entry;
    entry.name = name;
    entry.path = joinRemote(directory, name);

    if (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE)
        entry.size = attributes.filesize;
    if (attributes.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
        entry.uid = static_cast<quint32>(attributes.uid);
        entry.gid = static_cast<quint32>(attributes.gid);
    }
    if (attributes.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
        entry.modified = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(attributes.mtime));
    if (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        entry.permissions = static_cast<quint32>(attributes.permissions & 0xFFF);
        entry.isDirectory = LIBSSH2_SFTP_S_ISDIR(attributes.permissions);
        entry.isSymlink = LIBSSH2_SFTP_S_ISLNK(attributes.permissions);
        entry.isExecutable = (attributes.permissions & 0111) != 0;
    }

    return entry;
}

} // namespace

SftpSession::SftpSession(HostProfile profile, std::shared_ptr<SessionInteraction> interaction,
                         QObject *parent)
    : QObject(parent)
    , m_profile(std::move(profile))
    , m_interaction(std::move(interaction))
{
}

SftpSession::~SftpSession()
{
    shutdown();
}

void SftpSession::start()
{
    m_connection = std::make_unique<SshConnection>(m_profile);

    if (m_interaction) {
        auto interaction = m_interaction;
        m_connection->setHostKeyPrompt(
            [interaction](const HostKeyInfo &info) { return interaction->confirmHostKey(info); });
        m_connection->setCredentialPrompt([interaction](const QString &prompt, bool echo) {
            return interaction->askCredential(prompt, echo);
        });
    }

    if (auto status = m_connection->open(); !status) {
        m_connection.reset();
        Q_EMIT failed(status.error());
        return;
    }

    // SFTP runs blocking: this worker thread exists precisely so long transfers
    // never touch the GUI thread.
    m_connection->setBlocking(true);

    m_sftp = libssh2_sftp_init(m_connection->session());
    if (m_sftp == nullptr) {
        auto error = m_connection->lastError(ErrorKind::Sftp,
                                             QObject::tr("The server refused the SFTP subsystem"));
        m_connection.reset();
        Q_EMIT failed(error);
        return;
    }

    // Resolve "." to learn the login directory.
    char resolved[1024] = {};
    const int length = libssh2_sftp_realpath(m_sftp, ".", resolved, sizeof(resolved) - 1);
    m_homeDirectory = length > 0 ? QString::fromUtf8(resolved, length) : QStringLiteral("/");

    if (!m_profile.startupDirectory.isEmpty())
        m_homeDirectory = m_profile.startupDirectory;

    Q_EMIT ready(m_homeDirectory);
}

void SftpSession::shutdown()
{
    if (m_sftp != nullptr) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    m_connection.reset();
}

void SftpSession::requestCancel(quint64 requestId)
{
    const std::lock_guard lock(m_cancelMutex);
    m_cancelRequests.insert(requestId);
}

bool SftpSession::isCancelled(quint64 requestId) const
{
    const std::lock_guard lock(m_cancelMutex);
    return m_cancelRequests.contains(requestId);
}

void SftpSession::clearCancel(quint64 requestId)
{
    const std::lock_guard lock(m_cancelMutex);
    m_cancelRequests.remove(requestId);
}

Error SftpSession::sftpError(const QString &context) const
{
    if (m_sftp == nullptr)
        return Error{ErrorKind::Sftp, context};

    const auto code = libssh2_sftp_last_error(m_sftp);

    QString reason;
    switch (code) {
    case LIBSSH2_FX_NO_SUCH_FILE:
    case LIBSSH2_FX_NO_SUCH_PATH:
        reason = QObject::tr("no such file or directory");
        break;
    case LIBSSH2_FX_PERMISSION_DENIED:
        reason = QObject::tr("permission denied");
        break;
    case LIBSSH2_FX_FILE_ALREADY_EXISTS:
        reason = QObject::tr("already exists");
        break;
    case LIBSSH2_FX_DIR_NOT_EMPTY:
        reason = QObject::tr("directory is not empty");
        break;
    case LIBSSH2_FX_QUOTA_EXCEEDED:
        reason = QObject::tr("quota exceeded");
        break;
    case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM:
        reason = QObject::tr("no space left on the remote filesystem");
        break;
    case LIBSSH2_FX_OP_UNSUPPORTED:
        reason = QObject::tr("operation not supported by the server");
        break;
    case 0:
        // The failure came from the transport rather than the SFTP layer.
        return m_connection != nullptr ? m_connection->lastError(ErrorKind::Sftp, context)
                                       : Error{ErrorKind::Sftp, context};
    default:
        reason = QObject::tr("SFTP status %1").arg(code);
        break;
    }

    return Error{ErrorKind::Sftp, context + QLatin1String(": ") + reason, static_cast<int>(code)};
}

// ---------------------------------------------------------------------------
// Browsing
// ---------------------------------------------------------------------------

Result<RemoteListing> SftpSession::readDirectory(const QString &path)
{
    if (m_sftp == nullptr)
        return fail(ErrorKind::Sftp, QObject::tr("Not connected"));

    const QByteArray utf8 = path.toUtf8();
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(m_sftp, utf8.constData());
    if (handle == nullptr)
        return std::unexpected(sftpError(QObject::tr("Cannot open %1").arg(path)));

    struct HandleGuard {
        LIBSSH2_SFTP_HANDLE *handle;
        ~HandleGuard() { libssh2_sftp_closedir(handle); }
    } guard{handle};

    RemoteListing entries;
    char name[1024];
    LIBSSH2_SFTP_ATTRIBUTES attributes{};

    while (true) {
        const int length = libssh2_sftp_readdir_ex(handle, name, sizeof(name), nullptr, 0, &attributes);
        if (length == 0)
            break;
        if (length < 0)
            return std::unexpected(sftpError(QObject::tr("Cannot read %1").arg(path)));

        const QString fileName = QString::fromUtf8(name, length);
        if (fileName == QLatin1String(".") || fileName == QLatin1String(".."))
            continue;

        RemoteFileEntry entry = entryFromAttributes(path, fileName, attributes);

        // readdir reports the link itself; resolve it so the browser can descend
        // into symlinked directories.
        if (entry.isSymlink) {
            LIBSSH2_SFTP_ATTRIBUTES target{};
            const QByteArray targetPath = entry.path.toUtf8();
            if (libssh2_sftp_stat_ex(m_sftp, targetPath.constData(),
                                     static_cast<unsigned int>(targetPath.size()), LIBSSH2_SFTP_STAT,
                                     &target)
                == 0) {
                if (target.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
                    entry.isDirectory = LIBSSH2_SFTP_S_ISDIR(target.permissions);
                if (target.flags & LIBSSH2_SFTP_ATTR_SIZE)
                    entry.size = target.filesize;
            }
        }

        entries.append(std::move(entry));
    }

    // Directories first, then case-insensitive by name - the ordering every
    // file manager uses.
    std::sort(entries.begin(), entries.end(), [](const RemoteFileEntry &a, const RemoteFileEntry &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    return entries;
}

Result<RemoteFileEntry> SftpSession::statEntry(const QString &path)
{
    if (m_sftp == nullptr)
        return fail(ErrorKind::Sftp, QObject::tr("Not connected"));

    LIBSSH2_SFTP_ATTRIBUTES attributes{};
    const QByteArray utf8 = path.toUtf8();
    if (libssh2_sftp_stat_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()),
                             LIBSSH2_SFTP_STAT, &attributes)
        != 0) {
        return std::unexpected(sftpError(QObject::tr("Cannot stat %1").arg(path)));
    }

    const int slash = path.lastIndexOf(QLatin1Char('/'));
    const QString directory = slash > 0 ? path.left(slash) : QStringLiteral("/");
    const QString name = slash >= 0 ? path.mid(slash + 1) : path;

    return entryFromAttributes(directory, name, attributes);
}

void SftpSession::listDirectory(quint64 requestId, const QString &path)
{
    auto entries = readDirectory(path);
    if (!entries) {
        Q_EMIT operationFailed(requestId, entries.error());
        return;
    }
    Q_EMIT listingReady(requestId, path, *entries);
}

void SftpSession::resolvePath(quint64 requestId, const QString &path)
{
    if (m_sftp == nullptr) {
        Q_EMIT operationFailed(requestId, Error{ErrorKind::Sftp, QObject::tr("Not connected")});
        return;
    }

    char resolved[1024] = {};
    const QByteArray utf8 = path.toUtf8();
    const int length = libssh2_sftp_realpath(m_sftp, utf8.constData(), resolved, sizeof(resolved) - 1);
    if (length < 0) {
        Q_EMIT operationFailed(requestId, sftpError(QObject::tr("Cannot resolve %1").arg(path)));
        return;
    }

    Q_EMIT pathResolved(requestId, QString::fromUtf8(resolved, length));
}

void SftpSession::makeDirectory(quint64 requestId, const QString &path)
{
    const QByteArray utf8 = path.toUtf8();
    if (libssh2_sftp_mkdir_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()), 0755)
        != 0) {
        Q_EMIT operationFailed(requestId, sftpError(QObject::tr("Cannot create %1").arg(path)));
        return;
    }
    Q_EMIT operationFinished(requestId);
}

void SftpSession::renameEntry(quint64 requestId, const QString &from, const QString &to)
{
    const QByteArray source = from.toUtf8();
    const QByteArray destination = to.toUtf8();

    const int rc = libssh2_sftp_rename_ex(
        m_sftp, source.constData(), static_cast<unsigned int>(source.size()), destination.constData(),
        static_cast<unsigned int>(destination.size()),
        LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE);

    if (rc != 0) {
        Q_EMIT operationFailed(requestId,
                               sftpError(QObject::tr("Cannot rename %1 to %2").arg(from, to)));
        return;
    }
    Q_EMIT operationFinished(requestId);
}

void SftpSession::changePermissions(quint64 requestId, const QString &path, quint32 mode)
{
    LIBSSH2_SFTP_ATTRIBUTES attributes{};
    attributes.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    attributes.permissions = mode;

    const QByteArray utf8 = path.toUtf8();
    if (libssh2_sftp_stat_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()),
                             LIBSSH2_SFTP_SETSTAT, &attributes)
        != 0) {
        Q_EMIT operationFailed(requestId, sftpError(QObject::tr("Cannot change the mode of %1").arg(path)));
        return;
    }
    Q_EMIT operationFinished(requestId);
}

void SftpSession::removeEntry(quint64 requestId, const QString &path, bool recursive)
{
    auto info = statEntry(path);
    if (!info) {
        Q_EMIT operationFailed(requestId, info.error());
        return;
    }

    Status result;
    if (info->isDirectory && !info->isSymlink) {
        if (recursive) {
            result = removeTree(requestId, path);
        } else {
            const QByteArray utf8 = path.toUtf8();
            if (libssh2_sftp_rmdir_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()))
                != 0)
                result = std::unexpected(sftpError(QObject::tr("Cannot remove %1").arg(path)));
        }
    } else {
        const QByteArray utf8 = path.toUtf8();
        if (libssh2_sftp_unlink_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size())) != 0)
            result = std::unexpected(sftpError(QObject::tr("Cannot delete %1").arg(path)));
    }

    clearCancel(requestId);

    if (!result) {
        Q_EMIT operationFailed(requestId, result.error());
        return;
    }
    Q_EMIT operationFinished(requestId);
}

Status SftpSession::removeTree(quint64 requestId, const QString &path)
{
    if (isCancelled(requestId))
        return cancelled();

    auto entries = readDirectory(path);
    if (!entries)
        return std::unexpected(entries.error());

    for (const RemoteFileEntry &entry : *entries) {
        if (isCancelled(requestId))
            return cancelled();

        if (entry.isDirectory && !entry.isSymlink) {
            if (auto status = removeTree(requestId, entry.path); !status)
                return status;
        } else {
            const QByteArray utf8 = entry.path.toUtf8();
            if (libssh2_sftp_unlink_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()))
                != 0)
                return std::unexpected(sftpError(QObject::tr("Cannot delete %1").arg(entry.path)));
        }
    }

    const QByteArray utf8 = path.toUtf8();
    if (libssh2_sftp_rmdir_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size())) != 0)
        return std::unexpected(sftpError(QObject::tr("Cannot remove %1").arg(path)));

    return {};
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

void SftpSession::publishProgress(quint64 requestId, quint64 moved, quint64 total)
{
    static thread_local QElapsedTimer timer;
    if (!timer.isValid())
        timer.start();

    const qint64 now = timer.elapsed();
    if (m_lastProgressTick != 0 && now - m_lastProgressTick < kProgressIntervalMs && moved < total)
        return;

    TransferProgress progress;
    progress.transferred = moved;
    progress.total = total;

    if (m_lastProgressTick != 0 && now > m_lastProgressTick) {
        const double seconds = static_cast<double>(now - m_lastProgressTick) / 1000.0;
        progress.bytesPerSecond = static_cast<double>(moved - m_lastProgressBytes) / seconds;
    }

    m_lastProgressTick = now;
    m_lastProgressBytes = moved;

    Q_EMIT transferProgress(requestId, progress);
}

Result<quint64> SftpSession::remoteTreeSize(const QString &path)
{
    auto info = statEntry(path);
    if (!info)
        return std::unexpected(info.error());

    if (!info->isDirectory)
        return info->size;

    auto entries = readDirectory(path);
    if (!entries)
        return std::unexpected(entries.error());

    quint64 total = 0;
    for (const RemoteFileEntry &entry : *entries) {
        if (entry.isDirectory && !entry.isSymlink) {
            auto sub = remoteTreeSize(entry.path);
            if (!sub)
                return sub;
            total += *sub;
        } else {
            total += entry.size;
        }
    }
    return total;
}

quint64 SftpSession::localTreeSize(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir())
        return static_cast<quint64>(info.size());

    quint64 total = 0;
    QDirIterator iterator(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        total += static_cast<quint64>(iterator.fileInfo().size());
    }
    return total;
}

void SftpSession::download(quint64 requestId, const QString &remotePath, const QString &localPath)
{
    m_lastProgressTick = 0;
    m_lastProgressBytes = 0;

    auto total = remoteTreeSize(remotePath);
    if (!total) {
        clearCancel(requestId);
        Q_EMIT operationFailed(requestId, total.error());
        return;
    }

    Q_EMIT transferStarted(requestId, *total);

    quint64 moved = 0;
    auto status = downloadTree(requestId, remotePath, localPath, *total, moved);

    clearCancel(requestId);

    if (!status) {
        Q_EMIT operationFailed(requestId, status.error());
        return;
    }

    publishProgress(requestId, *total, *total);
    Q_EMIT transferFinished(requestId);
}

Status SftpSession::downloadTree(quint64 requestId, const QString &remotePath, const QString &localPath,
                                 quint64 total, quint64 &moved)
{
    if (isCancelled(requestId))
        return cancelled();

    auto info = statEntry(remotePath);
    if (!info)
        return std::unexpected(info.error());

    if (!info->isDirectory)
        return downloadFile(requestId, remotePath, localPath, total, moved);

    if (!QDir().mkpath(localPath))
        return fail(ErrorKind::LocalIo, QObject::tr("Cannot create %1").arg(localPath));

    auto entries = readDirectory(remotePath);
    if (!entries)
        return std::unexpected(entries.error());

    for (const RemoteFileEntry &entry : *entries) {
        const QString target = QDir(localPath).filePath(entry.name);
        if (auto status = downloadTree(requestId, entry.path, target, total, moved); !status)
            return status;
    }

    return {};
}

Status SftpSession::downloadFile(quint64 requestId, const QString &remotePath, const QString &localPath,
                                 quint64 total, quint64 &moved)
{
    if (m_backend == TransferBackend::Scp) {
        const quint64 base = moved;
        auto status = scp::receiveFile(
            m_connection->session(), remotePath, localPath,
            [this, requestId, base, total, &moved](quint64 transferred, quint64) {
                moved = base + transferred;
                publishProgress(requestId, moved, total);
                return !isCancelled(requestId);
            });
        return status;
    }

    const QByteArray utf8 = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *handle =
        libssh2_sftp_open_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()),
                             LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    if (handle == nullptr)
        return std::unexpected(sftpError(QObject::tr("Cannot open %1").arg(remotePath)));

    struct HandleGuard {
        LIBSSH2_SFTP_HANDLE *handle;
        ~HandleGuard() { libssh2_sftp_close_handle(handle); }
    } guard{handle};

    QFile output(localPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(ErrorKind::LocalIo,
                    QObject::tr("Cannot write %1: %2").arg(localPath, output.errorString()));
    }

    QByteArray buffer(kChunkSize, Qt::Uninitialized);

    while (true) {
        if (isCancelled(requestId)) {
            output.close();
            output.remove();
            return cancelled();
        }

        const auto count = libssh2_sftp_read(handle, buffer.data(), kChunkSize);
        if (count == 0)
            break;
        if (count < 0) {
            output.close();
            output.remove();
            return std::unexpected(sftpError(QObject::tr("Cannot read %1").arg(remotePath)));
        }

        if (output.write(buffer.constData(), static_cast<qint64>(count)) != static_cast<qint64>(count)) {
            const QString reason = output.errorString();
            output.close();
            output.remove();
            return fail(ErrorKind::LocalIo, QObject::tr("Cannot write %1: %2").arg(localPath, reason));
        }

        moved += static_cast<quint64>(count);
        publishProgress(requestId, moved, total);
    }

    if (!output.flush()) {
        return fail(ErrorKind::LocalIo,
                    QObject::tr("Cannot flush %1: %2").arg(localPath, output.errorString()));
    }

    return {};
}

void SftpSession::upload(quint64 requestId, const QString &localPath, const QString &remotePath)
{
    m_lastProgressTick = 0;
    m_lastProgressBytes = 0;

    if (!QFileInfo::exists(localPath)) {
        clearCancel(requestId);
        Q_EMIT operationFailed(
            requestId, Error{ErrorKind::LocalIo, QObject::tr("%1 does not exist").arg(localPath)});
        return;
    }

    const quint64 total = localTreeSize(localPath);
    Q_EMIT transferStarted(requestId, total);

    quint64 moved = 0;
    auto status = uploadTree(requestId, localPath, remotePath, total, moved);

    clearCancel(requestId);

    if (!status) {
        Q_EMIT operationFailed(requestId, status.error());
        return;
    }

    publishProgress(requestId, total, total);
    Q_EMIT transferFinished(requestId);
}

Status SftpSession::uploadTree(quint64 requestId, const QString &localPath, const QString &remotePath,
                               quint64 total, quint64 &moved)
{
    if (isCancelled(requestId))
        return cancelled();

    const QFileInfo info(localPath);
    if (!info.isDir())
        return uploadFile(requestId, localPath, remotePath, total, moved);

    const QByteArray utf8 = remotePath.toUtf8();
    const int rc =
        libssh2_sftp_mkdir_ex(m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()), 0755);
    if (rc != 0 && libssh2_sftp_last_error(m_sftp) != LIBSSH2_FX_FILE_ALREADY_EXISTS)
        return std::unexpected(sftpError(QObject::tr("Cannot create %1").arg(remotePath)));

    const QDir directory(localPath);
    const auto children = directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo &child : children) {
        const QString target = joinRemote(remotePath, child.fileName());
        if (auto status = uploadTree(requestId, child.absoluteFilePath(), target, total, moved); !status)
            return status;
    }

    return {};
}

Status SftpSession::uploadFile(quint64 requestId, const QString &localPath, const QString &remotePath,
                               quint64 total, quint64 &moved)
{
    if (m_backend == TransferBackend::Scp) {
        const quint64 base = moved;
        return scp::sendFile(m_connection->session(), localPath, remotePath,
                             [this, requestId, base, total, &moved](quint64 transferred, quint64) {
                                 moved = base + transferred;
                                 publishProgress(requestId, moved, total);
                                 return !isCancelled(requestId);
                             });
    }

    QFile input(localPath);
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(ErrorKind::LocalIo,
                    QObject::tr("Cannot read %1: %2").arg(localPath, input.errorString()));
    }

    const long mode = QFileInfo(localPath).isExecutable() ? 0755 : 0644;

    const QByteArray utf8 = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open_ex(
        m_sftp, utf8.constData(), static_cast<unsigned int>(utf8.size()),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, mode, LIBSSH2_SFTP_OPENFILE);
    if (handle == nullptr)
        return std::unexpected(sftpError(QObject::tr("Cannot create %1").arg(remotePath)));

    struct HandleGuard {
        LIBSSH2_SFTP_HANDLE *handle;
        ~HandleGuard() { libssh2_sftp_close_handle(handle); }
    } guard{handle};

    QByteArray buffer(kChunkSize, Qt::Uninitialized);

    while (true) {
        if (isCancelled(requestId))
            return cancelled();

        const qint64 read = input.read(buffer.data(), kChunkSize);
        if (read < 0) {
            return fail(ErrorKind::LocalIo,
                        QObject::tr("Cannot read %1: %2").arg(localPath, input.errorString()));
        }
        if (read == 0)
            break;

        qint64 offset = 0;
        while (offset < read) {
            const auto written =
                libssh2_sftp_write(handle, buffer.constData() + offset, static_cast<size_t>(read - offset));
            if (written < 0)
                return std::unexpected(sftpError(QObject::tr("Cannot write %1").arg(remotePath)));

            offset += static_cast<qint64>(written);
            moved += static_cast<quint64>(written);
            publishProgress(requestId, moved, total);
        }
    }

    return {};
}

} // namespace arterm::ssh
