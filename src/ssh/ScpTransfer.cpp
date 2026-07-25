#include "ssh/ScpTransfer.hpp"

#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <libssh2.h>

#include <sys/stat.h>

namespace arterm::ssh::scp {
namespace {

constexpr qint64 kChunkSize = 64 * 1024;

Error sessionError(LIBSSH2_SESSION *session, const QString &context)
{
    char *message = nullptr;
    int length = 0;
    const int code = libssh2_session_last_error(session, &message, &length, 0);
    QString detail = QString::fromUtf8(message == nullptr ? "" : message, length);
    if (detail.isEmpty())
        detail = QObject::tr("libssh2 error %1").arg(code);
    return Error{ErrorKind::Sftp, context + QLatin1String(": ") + detail, code};
}

} // namespace

Status receiveFile(LIBSSH2_SESSION *session, const QString &remotePath, const QString &localPath,
                   const ProgressCallback &onProgress)
{
    libssh2_struct_stat fileInfo{};
    const QByteArray remote = remotePath.toUtf8();

    LIBSSH2_CHANNEL *channel = libssh2_scp_recv2(session, remote.constData(), &fileInfo);
    if (channel == nullptr)
        return std::unexpected(sessionError(session, QObject::tr("SCP download of %1 failed").arg(remotePath)));

    struct ChannelGuard {
        LIBSSH2_CHANNEL *channel;
        ~ChannelGuard()
        {
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
        }
    } guard{channel};

    QFile output(localPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(ErrorKind::LocalIo,
                    QObject::tr("Cannot write %1: %2").arg(localPath, output.errorString()));
    }

    const auto total = static_cast<quint64>(fileInfo.st_size);
    quint64 received = 0;
    QByteArray buffer(kChunkSize, Qt::Uninitialized);

    while (received < total) {
        const auto wanted = static_cast<qint64>(std::min<quint64>(kChunkSize, total - received));
        const auto count = libssh2_channel_read(channel, buffer.data(), static_cast<size_t>(wanted));

        if (count == LIBSSH2_ERROR_EAGAIN)
            continue;
        if (count < 0) {
            output.remove();
            return std::unexpected(sessionError(session, QObject::tr("SCP download of %1 failed").arg(remotePath)));
        }
        if (count == 0)
            break;

        if (output.write(buffer.constData(), static_cast<qint64>(count)) != static_cast<qint64>(count)) {
            output.remove();
            return fail(ErrorKind::LocalIo,
                        QObject::tr("Cannot write %1: %2").arg(localPath, output.errorString()));
        }

        received += static_cast<quint64>(count);

        if (onProgress && !onProgress(received, total)) {
            output.close();
            output.remove();
            return cancelled();
        }
    }

    if (!output.flush()) {
        return fail(ErrorKind::LocalIo,
                    QObject::tr("Cannot flush %1: %2").arg(localPath, output.errorString()));
    }
    output.close();

    QFile::setPermissions(localPath, QFile::permissions(localPath) | QFile::ReadOwner | QFile::WriteOwner);
    return {};
}

Status sendFile(LIBSSH2_SESSION *session, const QString &localPath, const QString &remotePath,
                const ProgressCallback &onProgress)
{
    QFile input(localPath);
    if (!input.open(QIODevice::ReadOnly)) {
        return fail(ErrorKind::LocalIo,
                    QObject::tr("Cannot read %1: %2").arg(localPath, input.errorString()));
    }

    const QFileInfo info(localPath);
    const auto total = static_cast<quint64>(info.size());

    // Carry over the executable bit; everything else uses a sane default.
    int mode = 0644;
    if (info.isExecutable())
        mode = 0755;

    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_CHANNEL *channel = libssh2_scp_send64(
        session, remote.constData(), mode, static_cast<libssh2_int64_t>(total),
        static_cast<time_t>(info.lastModified().toSecsSinceEpoch()),
        static_cast<time_t>(info.lastRead().toSecsSinceEpoch()));

    if (channel == nullptr)
        return std::unexpected(sessionError(session, QObject::tr("SCP upload of %1 failed").arg(localPath)));

    struct ChannelGuard {
        LIBSSH2_CHANNEL *channel;
        ~ChannelGuard()
        {
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
        }
    } guard{channel};

    quint64 sent = 0;
    QByteArray buffer(kChunkSize, Qt::Uninitialized);

    while (sent < total) {
        const qint64 read = input.read(buffer.data(), kChunkSize);
        if (read < 0) {
            return fail(ErrorKind::LocalIo,
                        QObject::tr("Cannot read %1: %2").arg(localPath, input.errorString()));
        }
        if (read == 0)
            break;

        qint64 offset = 0;
        while (offset < read) {
            const auto written = libssh2_channel_write(channel, buffer.constData() + offset,
                                                       static_cast<size_t>(read - offset));
            if (written == LIBSSH2_ERROR_EAGAIN)
                continue;
            if (written < 0) {
                return std::unexpected(
                    sessionError(session, QObject::tr("SCP upload of %1 failed").arg(localPath)));
            }
            offset += static_cast<qint64>(written);
        }

        sent += static_cast<quint64>(read);

        if (onProgress && !onProgress(sent, total))
            return cancelled();
    }

    // SCP requires the explicit EOF/ack exchange before the file is committed.
    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);

    return {};
}

} // namespace arterm::ssh::scp
