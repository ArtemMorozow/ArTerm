#pragma once

#include "core/Result.hpp"
#include "ssh/SshTypes.hpp"

#include <QByteArray>
#include <QObject>
#include <QSize>

#include <memory>

class QSocketNotifier;
class QTimer;

using LIBSSH2_CHANNEL = struct _LIBSSH2_CHANNEL;

namespace arterm::ssh {

class SshConnection;
class SessionInteraction;

/// Drives one interactive shell: connect, allocate a PTY, then pump bytes both
/// ways.
///
/// The object is moved onto a dedicated worker thread by `ShellSessionHandle`.
/// Every public slot is meant to be reached with a queued connection; only the
/// signals cross back to the GUI thread.
class ShellSession : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Connecting, Authenticating, Ready, Closing, Closed, Failed };
    Q_ENUM(State)

    ShellSession(HostProfile profile, std::shared_ptr<SessionInteraction> interaction,
                 QObject *parent = nullptr);
    ~ShellSession() override;

    [[nodiscard]] State state() const noexcept { return m_state; }

public Q_SLOTS:
    /// Open the connection and start the shell. Runs on the worker thread.
    void start();

    /// Queue bytes for the remote PTY (keyboard input, pasted text).
    void write(const QByteArray &data);

    /// Tell the remote side the terminal was resized.
    void resize(int columns, int rows, int pixelWidth, int pixelHeight);

    /// Politely close the channel and the connection.
    void shutdown();

Q_SIGNALS:
    void stateChanged(arterm::ssh::ShellSession::State state);
    void connected(const QString &banner, const QString &authMethod);
    void dataReceived(const QByteArray &data);
    void errorOutputReceived(const QByteArray &data);
    void failed(const arterm::Error &error);
    void closed(int exitStatus);

private:
    void setState(State state);
    void drainChannel();
    void sendKeepAlive();
    void teardown();
    [[nodiscard]] Status openShell(int columns, int rows);
    [[nodiscard]] Status writeAll(const QByteArray &data);

    HostProfile m_profile;
    std::shared_ptr<SessionInteraction> m_interaction;
    std::unique_ptr<SshConnection> m_connection;
    LIBSSH2_CHANNEL *m_channel{nullptr};

    QSocketNotifier *m_readNotifier{nullptr};
    QTimer *m_pollTimer{nullptr};
    QTimer *m_keepAliveTimer{nullptr};

    State m_state{State::Idle};
    QSize m_pendingSize{80, 24};
    QByteArray m_pendingWrite; ///< Bytes typed before the shell became ready.
    bool m_torndown{false};
};

} // namespace arterm::ssh

Q_DECLARE_METATYPE(arterm::ssh::ShellSession::State)
