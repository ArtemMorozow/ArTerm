#pragma once

#include "ssh/ShellSession.hpp"
#include "ssh/SshTypes.hpp"

#include <QWidget>

#include <memory>

class QLabel;
class QSplitter;

namespace arterm::term {
class TerminalWidget;
}

namespace arterm::files {
class FileBrowser;
}

namespace arterm::session {
class RemoteFileService;
class ShellService;
} // namespace arterm::session

namespace arterm::ssh {
class SessionInteraction;
}

namespace arterm::ui {

/// One connected host: the terminal on top, the file browser below.
///
/// The tab owns both worker-backed services. Two SSH connections are opened per
/// host, one for the shell and one for SFTP, because a libssh2 session cannot be
/// driven from two threads and interleaving a large upload with interactive
/// typing on one connection makes the terminal stutter.
class SessionTab : public QWidget {
    Q_OBJECT

public:
    SessionTab(ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
               QWidget *parent = nullptr);
    ~SessionTab() override;

    /// Opens both connections.
    void connectToHost();

    /// Closes them and stops the worker threads.
    void disconnectFromHost();

    [[nodiscard]] const ssh::HostProfile &profile() const noexcept { return m_profile; }
    [[nodiscard]] const QString &sessionId() const noexcept { return m_sessionId; }

    /// Tab title: the remote title when the shell set one, the host otherwise.
    [[nodiscard]] QString displayTitle() const;

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool hasActiveTransfers() const;

    [[nodiscard]] term::TerminalWidget *terminal() const noexcept { return m_terminal; }
    [[nodiscard]] files::FileBrowser *fileBrowser() const noexcept { return m_browser; }

    void setFileBrowserVisible(bool visible);
    [[nodiscard]] bool isFileBrowserVisible() const;
    void focusTerminal();

Q_SIGNALS:
    void titleChanged(const QString &title);
    void statusMessage(const QString &message);
    void connectionStateChanged();
    /// The shell ended; the window may want to close the tab.
    void sessionClosed(int exitStatus);

private:
    void buildUi();
    void wireShell();
    void wireFiles();
    void showBanner(const QString &message, const QString &state);
    void hideBanner();

    ssh::HostProfile m_profile;
    QString m_sessionId;
    std::shared_ptr<ssh::SessionInteraction> m_interaction;

    session::ShellService *m_shell{nullptr};
    session::RemoteFileService *m_files{nullptr};

    term::TerminalWidget *m_terminal{nullptr};
    files::FileBrowser *m_browser{nullptr};
    QSplitter *m_splitter{nullptr};
    QLabel *m_banner{nullptr};

    QString m_remoteTitle;
    bool m_shellReady{false};
};

} // namespace arterm::ui
