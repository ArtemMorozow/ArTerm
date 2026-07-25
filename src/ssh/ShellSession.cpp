#include "ssh/ShellSession.hpp"

#include "ssh/SessionInteraction.hpp"
#include "ssh/SshConnection.hpp"

#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QTimer>

#include <libssh2.h>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY(lcSsh)

namespace arterm::ssh {
namespace {

/// libssh2 hands us decrypted data in chunks; 32 KiB keeps syscalls low without
/// making a single drain pass starve the event loop.
constexpr int kReadChunk = 32 * 1024;

/// The socket notifier can miss data that libssh2 already buffered internally,
/// so a short timer sweeps the channel as a safety net.
constexpr int kPollIntervalMs = 25;

} // namespace

ShellSession::ShellSession(HostProfile profile, std::shared_ptr<SessionInteraction> interaction,
                           QObject *parent)
    : QObject(parent)
    , m_profile(std::move(profile))
    , m_interaction(std::move(interaction))
{
}

ShellSession::~ShellSession()
{
    teardown();
}

void ShellSession::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

void ShellSession::start()
{
    if (m_state != State::Idle)
        return;

    setState(State::Connecting);

    m_connection = std::make_unique<SshConnection>(m_profile);

    if (m_interaction) {
        auto interaction = m_interaction;
        m_connection->setHostKeyPrompt(
            [interaction](const HostKeyInfo &info) { return interaction->confirmHostKey(info); });
        m_connection->setCredentialPrompt([interaction](const QString &prompt, bool echo) {
            return interaction->askCredential(prompt, echo);
        });
    }

    setState(State::Authenticating);
    if (auto status = m_connection->open(); !status) {
        m_connection.reset();
        setState(State::Failed);
        Q_EMIT failed(status.error());
        return;
    }

    if (auto status = openShell(m_pendingSize.width(), m_pendingSize.height()); !status) {
        teardown();
        setState(State::Failed);
        Q_EMIT failed(status.error());
        return;
    }

    // From here on the session is event driven, so it must not block.
    m_connection->setBlocking(false);

    m_readNotifier = new QSocketNotifier(m_connection->socketDescriptor(), QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, [this] { drainChannel(); });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this] { drainChannel(); });
    m_pollTimer->start();

    if (m_profile.keepAliveSeconds > 0) {
        m_keepAliveTimer = new QTimer(this);
        m_keepAliveTimer->setInterval(m_profile.keepAliveSeconds * 1000);
        connect(m_keepAliveTimer, &QTimer::timeout, this, [this] { sendKeepAlive(); });
        m_keepAliveTimer->start();
    }

    setState(State::Ready);
    Q_EMIT connected(m_connection->banner(), authMethodName(m_connection->usedAuthMethod()));

    if (!m_profile.startupCommand.isEmpty()) {
        QByteArray command = m_profile.startupCommand.toUtf8();
        if (!command.endsWith('\n'))
            command.append('\n');
        m_pendingWrite.append(command);
    }

    if (!m_pendingWrite.isEmpty()) {
        const QByteArray buffered = std::exchange(m_pendingWrite, QByteArray{});
        write(buffered);
    }

    drainChannel();
}

Status ShellSession::openShell(int columns, int rows)
{
    LIBSSH2_SESSION *session = m_connection->session();

    m_channel = libssh2_channel_open_session(session);
    if (m_channel == nullptr)
        return std::unexpected(m_connection->lastError(ErrorKind::Channel,
                                                       QObject::tr("Cannot open a session channel")));

    // xterm-256color matches what the bundled colour palette implements.
    static constexpr char kTerm[] = "xterm-256color";
    if (libssh2_channel_request_pty_ex(m_channel, kTerm, sizeof(kTerm) - 1, nullptr, 0, columns, rows,
                                       0, 0)
        != 0) {
        return std::unexpected(m_connection->lastError(ErrorKind::Channel,
                                                       QObject::tr("Cannot allocate a pseudo terminal")));
    }

    if (libssh2_channel_shell(m_channel) != 0) {
        return std::unexpected(
            m_connection->lastError(ErrorKind::Channel, QObject::tr("Cannot start the remote shell")));
    }

    return {};
}

void ShellSession::drainChannel()
{
    if (m_channel == nullptr || m_state != State::Ready)
        return;

    QByteArray buffer(kReadChunk, Qt::Uninitialized);
    bool sawEof = false;

    // Read both streams until libssh2 has nothing left; a single socket wakeup
    // often carries several channel windows worth of data.
    for (int stream = 0; stream < 2; ++stream) {
        const int streamId = (stream == 0) ? 0 : SSH_EXTENDED_DATA_STDERR;

        while (true) {
            const auto count = libssh2_channel_read_ex(m_channel, streamId, buffer.data(), kReadChunk);

            if (count > 0) {
                const QByteArray chunk(buffer.constData(), static_cast<qsizetype>(count));
                if (streamId == 0)
                    Q_EMIT dataReceived(chunk);
                else
                    Q_EMIT errorOutputReceived(chunk);
                continue;
            }

            if (count == LIBSSH2_ERROR_EAGAIN)
                break;

            if (count == 0) {
                // Zero only means "nothing right now"; EOF is reported separately.
                break;
            }

            qCWarning(lcSsh) << "channel read failed with" << count;
            sawEof = true;
            break;
        }

        if (sawEof)
            break;
    }

    if (sawEof || libssh2_channel_eof(m_channel) == 1) {
        const int exitStatus = libssh2_channel_get_exit_status(m_channel);
        teardown();
        setState(State::Closed);
        Q_EMIT closed(exitStatus);
    }
}

void ShellSession::write(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    if (m_state != State::Ready || m_channel == nullptr) {
        // Keep early keystrokes instead of dropping them on the floor.
        if (m_state == State::Connecting || m_state == State::Authenticating)
            m_pendingWrite.append(data);
        return;
    }

    if (auto status = writeAll(data); !status) {
        teardown();
        setState(State::Failed);
        Q_EMIT failed(status.error());
    }
}

Status ShellSession::writeAll(const QByteArray &data)
{
    // Bound the wait so a wedged peer cannot pin the worker thread forever.
    constexpr int kMaxStalledWaits = 60;

    qsizetype offset = 0;
    int stalledWaits = 0;

    while (offset < data.size()) {
        const auto written = libssh2_channel_write_ex(m_channel, 0, data.constData() + offset,
                                                      static_cast<size_t>(data.size() - offset));

        if (written == LIBSSH2_ERROR_EAGAIN) {
            // The remote window is full; wait for the socket instead of spinning.
            if (!m_connection->waitSocket(500) && ++stalledWaits >= kMaxStalledWaits) {
                return fail(ErrorKind::Channel,
                            QObject::tr("The remote shell stopped accepting input"));
            }
            continue;
        }

        stalledWaits = 0;

        if (written < 0) {
            return std::unexpected(
                m_connection->lastError(ErrorKind::Channel, QObject::tr("Cannot write to the shell")));
        }

        offset += static_cast<qsizetype>(written);
    }

    return {};
}

void ShellSession::resize(int columns, int rows, int pixelWidth, int pixelHeight)
{
    m_pendingSize = QSize(columns, rows);

    if (m_channel == nullptr || m_state != State::Ready)
        return;

    int rc = 0;
    do {
        rc = libssh2_channel_request_pty_size_ex(m_channel, columns, rows, pixelWidth, pixelHeight);
        if (rc == LIBSSH2_ERROR_EAGAIN && !m_connection->waitSocket(200))
            break;
    } while (rc == LIBSSH2_ERROR_EAGAIN);

    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN)
        qCDebug(lcSsh) << "pty resize rejected with" << rc;
}

void ShellSession::sendKeepAlive()
{
    if (m_connection == nullptr || !m_connection->isOpen())
        return;

    int secondsToNext = 0;
    const int rc = libssh2_keepalive_send(m_connection->session(), &secondsToNext);
    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN)
        qCDebug(lcSsh) << "keepalive failed with" << rc;
}

void ShellSession::shutdown()
{
    if (m_state == State::Closed || m_state == State::Failed) {
        teardown();
        return;
    }

    setState(State::Closing);
    teardown();
    setState(State::Closed);
    Q_EMIT closed(0);
}

void ShellSession::teardown()
{
    // Re-entrancy guard: drainChannel() can call teardown() while a signal
    // emitted from inside it is still unwinding.
    if (m_torndown)
        return;
    m_torndown = true;

    if (m_pollTimer != nullptr) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }
    if (m_keepAliveTimer != nullptr) {
        m_keepAliveTimer->stop();
        m_keepAliveTimer->deleteLater();
        m_keepAliveTimer = nullptr;
    }
    if (m_readNotifier != nullptr) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }

    if (m_channel != nullptr) {
        if (m_connection != nullptr)
            m_connection->setBlocking(true);
        libssh2_channel_close(m_channel);
        libssh2_channel_free(m_channel);
        m_channel = nullptr;
    }

    m_connection.reset();
    m_torndown = false;
}

} // namespace arterm::ssh
