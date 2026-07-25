#pragma once

#include "ssh/SshTypes.hpp"

#include <QMainWindow>

#include <memory>

class QLabel;
class QProgressBar;
class QTabWidget;

namespace arterm::model {
class HostStore;
}

namespace arterm::ssh {
class SessionInteraction;
}

namespace arterm::ui {

class HostSidebar;
class SessionTab;

/// The application window: host sidebar on the left, session tabs on the right.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Opens a session for a stored host.
    void openSession(const QString &hostId);

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void newHost();
    void editHost(const QString &hostId);
    void closeTab(int index);
    void closeCurrentTab();
    void reconnectCurrent();
    void toggleFileBrowser();
    void importSshConfig();

private:
    void buildUi();
    void buildMenus();
    void buildToolBar();
    void wireInteraction();
    void restoreWindowState();
    void saveWindowState() const;

    [[nodiscard]] SessionTab *currentSession() const;
    [[nodiscard]] SessionTab *sessionAt(int index) const;
    void updateTabTitle(SessionTab *session);
    void showStatus(const QString &message, int timeoutMs = 6000);

    model::HostStore *m_store{nullptr};
    std::shared_ptr<ssh::SessionInteraction> m_interaction;

    HostSidebar *m_sidebar{nullptr};
    QTabWidget *m_tabs{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_emptyState{nullptr};
};

} // namespace arterm::ui
