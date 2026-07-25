#include "session/RemoteFileService.hpp"

#include "ssh/SessionInteraction.hpp"

#include <QMetaObject>
#include <QThread>

namespace arterm::session {

RemoteFileService::RemoteFileService(ssh::HostProfile profile,
                                     std::shared_ptr<ssh::SessionInteraction> interaction,
                                     QObject *parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_session(new ssh::SftpSession(std::move(profile), std::move(interaction)))
{
    m_thread->setObjectName(QStringLiteral("arterm-sftp"));
    m_session->moveToThread(m_thread);

    // The worker is deleted on its own thread once the event loop stops, which
    // is the only place its libssh2 handles may be touched.
    connect(m_thread, &QThread::finished, m_session, &QObject::deleteLater);

    connect(m_session, &ssh::SftpSession::ready, this, [this](const QString &home) {
        m_homeDirectory = home;
        m_connected = true;
        Q_EMIT ready(home);
    });
    connect(m_session, &ssh::SftpSession::failed, this, [this](const Error &error) {
        m_connected = false;
        Q_EMIT failed(error);
    });

    connect(m_session, &ssh::SftpSession::listingReady, this, &RemoteFileService::listingReady);
    connect(m_session, &ssh::SftpSession::pathResolved, this, &RemoteFileService::pathResolved);
    connect(m_session, &ssh::SftpSession::operationFinished, this, &RemoteFileService::operationFinished);
    connect(m_session, &ssh::SftpSession::operationFailed, this, &RemoteFileService::operationFailed);
    connect(m_session, &ssh::SftpSession::transferStarted, this, &RemoteFileService::transferStarted);
    connect(m_session, &ssh::SftpSession::transferProgress, this, &RemoteFileService::transferProgress);
    connect(m_session, &ssh::SftpSession::transferFinished, this, &RemoteFileService::transferFinished);

    m_thread->start();
}

RemoteFileService::~RemoteFileService()
{
    stop();

    if (m_thread != nullptr) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

quint64 RemoteFileService::nextRequestId()
{
    return m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
}

void RemoteFileService::start()
{
    QMetaObject::invokeMethod(m_session, "start", Qt::QueuedConnection);
}

void RemoteFileService::stop()
{
    if (m_thread == nullptr || !m_thread->isRunning())
        return;

    m_connected = false;

    // Blocking so the session is torn down before the event loop quits;
    // otherwise deleteLater would run against a live libssh2 handle.
    QMetaObject::invokeMethod(m_session, "shutdown", Qt::BlockingQueuedConnection);

    m_thread->quit();
    m_thread->wait(5000);

    Q_EMIT disconnected();
}

void RemoteFileService::setTransferBackend(ssh::TransferBackend backend)
{
    QMetaObject::invokeMethod(
        m_session, [session = m_session, backend] { session->setTransferBackend(backend); },
        Qt::QueuedConnection);
}

void RemoteFileService::cancel(quint64 requestId)
{
    // Called directly rather than queued: a queued call would sit behind the
    // very transfer it is meant to interrupt.
    m_session->requestCancel(requestId);
}

quint64 RemoteFileService::listDirectory(const QString &path)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "listDirectory", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, path));
    return id;
}

quint64 RemoteFileService::resolvePath(const QString &path)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "resolvePath", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, path));
    return id;
}

quint64 RemoteFileService::makeDirectory(const QString &path)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "makeDirectory", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, path));
    return id;
}

quint64 RemoteFileService::removeEntry(const QString &path, bool recursive)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "removeEntry", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, path), Q_ARG(bool, recursive));
    return id;
}

quint64 RemoteFileService::renameEntry(const QString &from, const QString &to)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "renameEntry", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, from), Q_ARG(QString, to));
    return id;
}

quint64 RemoteFileService::changePermissions(const QString &path, quint32 mode)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "changePermissions", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, path), Q_ARG(quint32, mode));
    return id;
}

quint64 RemoteFileService::download(const QString &remotePath, const QString &localPath)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "download", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, remotePath), Q_ARG(QString, localPath));
    return id;
}

quint64 RemoteFileService::upload(const QString &localPath, const QString &remotePath)
{
    const quint64 id = nextRequestId();
    QMetaObject::invokeMethod(m_session, "upload", Qt::QueuedConnection, Q_ARG(quint64, id),
                              Q_ARG(QString, localPath), Q_ARG(QString, remotePath));
    return id;
}

} // namespace arterm::session
