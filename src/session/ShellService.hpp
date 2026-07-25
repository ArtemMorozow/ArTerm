#pragma once

#include "core/Result.hpp"
#include "ssh/ShellSession.hpp"
#include "ssh/SshTypes.hpp"

#include <QObject>

#include <memory>

class QThread;

namespace arterm::ssh {
class SessionInteraction;
}

namespace arterm::session {

/// GUI-thread facade over `ssh::ShellSession`.
class ShellService : public QObject {
    Q_OBJECT

public:
    ShellService(ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
                 QObject *parent = nullptr);
    ~ShellService() override;

    void start();
    void stop();

    void write(const QByteArray &data);
    void resize(int columns, int rows, int pixelWidth, int pixelHeight);

    [[nodiscard]] ssh::ShellSession::State state() const noexcept { return m_state; }
    [[nodiscard]] bool isReady() const noexcept { return m_state == ssh::ShellSession::State::Ready; }

Q_SIGNALS:
    void stateChanged(arterm::ssh::ShellSession::State state);
    void connected(const QString &banner, const QString &authMethod);
    void dataReceived(const QByteArray &data);
    void failed(const arterm::Error &error);
    void closed(int exitStatus);

private:
    QThread *m_thread{nullptr};
    ssh::ShellSession *m_session{nullptr}; ///< Owned by the worker thread.
    ssh::ShellSession::State m_state{ssh::ShellSession::State::Idle};
};

} // namespace arterm::session
