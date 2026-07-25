#include "session/ShellService.hpp"

#include "ssh/SessionInteraction.hpp"

#include <QMetaObject>
#include <QThread>

namespace arterm::session {

ShellService::ShellService(ssh::HostProfile profile,
                           std::shared_ptr<ssh::SessionInteraction> interaction, QObject *parent)
    : QObject(parent)
    , m_thread(new QThread)
    , m_session(new ssh::ShellSession(std::move(profile), std::move(interaction)))
{
    m_thread->setObjectName(QStringLiteral("arterm-shell"));
    m_session->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_session, &QObject::deleteLater);

    connect(m_session, &ssh::ShellSession::stateChanged, this,
            [this](ssh::ShellSession::State state) {
                m_state = state;
                Q_EMIT stateChanged(state);
            });
    connect(m_session, &ssh::ShellSession::connected, this, &ShellService::connected);
    connect(m_session, &ssh::ShellSession::dataReceived, this, &ShellService::dataReceived);
    connect(m_session, &ssh::ShellSession::failed, this, &ShellService::failed);
    connect(m_session, &ssh::ShellSession::closed, this, &ShellService::closed);

    // stderr from the channel is interleaved into the terminal exactly as a
    // local PTY would present it.
    connect(m_session, &ssh::ShellSession::errorOutputReceived, this, &ShellService::dataReceived);

    m_thread->start();
}

ShellService::~ShellService()
{
    stop();

    if (m_thread != nullptr) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void ShellService::start()
{
    QMetaObject::invokeMethod(m_session, "start", Qt::QueuedConnection);
}

void ShellService::stop()
{
    if (m_thread == nullptr || !m_thread->isRunning())
        return;

    QMetaObject::invokeMethod(m_session, "shutdown", Qt::BlockingQueuedConnection);

    m_thread->quit();
    m_thread->wait(5000);
}

void ShellService::write(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    QMetaObject::invokeMethod(m_session, "write", Qt::QueuedConnection, Q_ARG(QByteArray, data));
}

void ShellService::resize(int columns, int rows, int pixelWidth, int pixelHeight)
{
    QMetaObject::invokeMethod(m_session, "resize", Qt::QueuedConnection, Q_ARG(int, columns),
                              Q_ARG(int, rows), Q_ARG(int, pixelWidth), Q_ARG(int, pixelHeight));
}

} // namespace arterm::session
