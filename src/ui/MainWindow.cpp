#include "ui/MainWindow.hpp"

#include "files/FileBrowser.hpp"
#include "model/HostStore.hpp"
#include "ssh/SessionInteraction.hpp"
#include "terminal/TerminalWidget.hpp"
#include "ui/HostEditorDialog.hpp"
#include "ui/HostKeyDialog.hpp"
#include "ui/HostSidebar.hpp"
#include "ui/Icons.hpp"
#include "ui/SessionTab.hpp"
#include "ui/Theme.hpp"

#include <QCloseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace arterm::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_store(new model::HostStore(this))
    , m_interaction(std::make_shared<ssh::SessionInteraction>())
{
    static_cast<void>(m_store->load());

    buildUi();
    buildMenus();
    buildToolBar();
    wireInteraction();
    restoreWindowState();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("ArTerm"));
    setMinimumSize(940, 620);

    m_sidebar = new HostSidebar(m_store, this);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("sessionTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);

    // Shown instead of the tab widget when nothing is connected.
    m_emptyState = new QLabel(this);
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setWordWrap(true);
    m_emptyState->setText(tr("<div style='line-height:1.7'>"
                             "<span style='font-size:19px;font-weight:600'>No open sessions</span><br>"
                             "Pick a host on the left, or press ⌘N to add one."
                             "</div>"));

    auto *stack = new QStackedWidget(this);
    stack->addWidget(m_emptyState);
    stack->addWidget(m_tabs);

    const auto updateStack = [this, stack] {
        stack->setCurrentIndex(m_tabs->count() > 0 ? 1 : 0);
    };
    connect(m_tabs, &QTabWidget::currentChanged, this, updateStack);
    updateStack();

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(0, true);
    splitter->setCollapsible(1, false);
    splitter->setHandleWidth(1);
    splitter->setObjectName(QStringLiteral("mainSplitter"));

    setCentralWidget(splitter);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->setSizeGripEnabled(false);

    connect(m_sidebar, &HostSidebar::connectRequested, this, &MainWindow::openSession);
    connect(m_sidebar, &HostSidebar::editRequested, this, &MainWindow::editHost);
    connect(m_sidebar, &HostSidebar::newHostRequested, this, &MainWindow::newHost);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("New Host…"), QKeySequence(QKeySequence::New), this, &MainWindow::newHost);
    fileMenu->addAction(tr("Import from ~/.ssh/config…"), this, &MainWindow::importSshConfig);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Close Session"), QKeySequence(QKeySequence::Close), this,
                        &MainWindow::closeCurrentTab);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("Copy"), QKeySequence(QKeySequence::Copy), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->copySelection();
    });
    editMenu->addAction(tr("Paste"), QKeySequence(QKeySequence::Paste), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->pasteFromClipboard();
    });
    editMenu->addAction(tr("Select All"), QKeySequence(QKeySequence::SelectAll), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->selectAll();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Find Host"), QKeySequence(QKeySequence::Find), m_sidebar,
                        &HostSidebar::focusSearch);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Toggle File Browser"), QKeySequence(Qt::CTRL | Qt::Key_B), this,
                        &MainWindow::toggleFileBrowser);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Bigger Text"), QKeySequence(Qt::CTRL | Qt::Key_Plus), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->increaseFontSize();
    });
    viewMenu->addAction(tr("Smaller Text"), QKeySequence(Qt::CTRL | Qt::Key_Minus), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->decreaseFontSize();
    });
    viewMenu->addAction(tr("Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->resetFontSize();
    });
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Clear Buffer"), QKeySequence(Qt::CTRL | Qt::Key_K), this, [this] {
        if (SessionTab *session = currentSession())
            session->terminal()->clearScreen();
    });

    QMenu *sessionMenu = menuBar()->addMenu(tr("&Session"));
    sessionMenu->addAction(tr("Reconnect"), QKeySequence(Qt::CTRL | Qt::Key_R), this,
                           &MainWindow::reconnectCurrent);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("About ArTerm"), this, [this] {
        QMessageBox::about(
            this, tr("About ArTerm"),
            tr("<b>ArTerm %1</b><br><br>"
               "An SSH client and SFTP/SCP file manager for macOS.<br>"
               "Built with Qt 6 and libssh2.")
                .arg(QStringLiteral(ARTERM_VERSION)));
    });
}

void MainWindow::buildToolBar()
{
    auto *toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("appToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->setIconSize(QSize(16, 16));

    toolBar->addAction(icon(QStringLiteral("plus")), tr("New Host"), this, &MainWindow::newHost);
    toolBar->addAction(icon(QStringLiteral("refresh")), tr("Reconnect"), this,
                       &MainWindow::reconnectCurrent);

    QAction *browserAction = toolBar->addAction(icon(QStringLiteral("split")), tr("Files"), this,
                                                &MainWindow::toggleFileBrowser);
    browserAction->setCheckable(true);
    browserAction->setChecked(true);

    connect(m_tabs, &QTabWidget::currentChanged, this, [this, browserAction] {
        SessionTab *session = currentSession();
        browserAction->setEnabled(session != nullptr);
        if (session != nullptr)
            browserAction->setChecked(session->isFileBrowserVisible());
    });
}

void MainWindow::wireInteraction()
{
    connect(m_interaction.get(), &ssh::SessionInteraction::hostKeyDecisionRequested, this,
            [this](const ssh::HostKeyInfo &info, bool *accepted) {
                *accepted = HostKeyDialog::ask(info, this);
            });

    connect(m_interaction.get(), &ssh::SessionInteraction::credentialRequested, this,
            [this](const QString &prompt, bool echo, QString *answer, bool *provided) {
                bool ok = false;
                const QString value =
                    QInputDialog::getText(this, tr("Authentication"), prompt,
                                          echo ? QLineEdit::Normal : QLineEdit::Password, QString(),
                                          &ok);
                *provided = ok;
                if (ok)
                    *answer = value;
            });
}

SessionTab *MainWindow::currentSession() const
{
    return sessionAt(m_tabs->currentIndex());
}

SessionTab *MainWindow::sessionAt(int index) const
{
    if (index < 0 || index >= m_tabs->count())
        return nullptr;
    return qobject_cast<SessionTab *>(m_tabs->widget(index));
}

void MainWindow::openSession(const QString &hostId)
{
    const auto stored = m_store->profileById(hostId);
    if (!stored) {
        showStatus(tr("That host no longer exists."));
        return;
    }

    // Secrets are pulled from the keychain only at connect time, so they exist
    // in memory for as long as the session and no longer.
    const ssh::HostProfile profile = m_store->withSecrets(hostId);

    auto *session = new SessionTab(profile, m_interaction, this);

    const int index = m_tabs->addTab(session, profile.displayName());
    m_tabs->setTabIcon(index, icon(QStringLiteral("terminal")));
    m_tabs->setCurrentIndex(index);

    connect(session, &SessionTab::titleChanged, this,
            [this, session](const QString &) { updateTabTitle(session); });
    connect(session, &SessionTab::statusMessage, this,
            [this](const QString &message) { showStatus(message); });
    connect(session, &SessionTab::connectionStateChanged, this,
            [this, session] { updateTabTitle(session); });

    session->connectToHost();
    session->focusTerminal();
}

void MainWindow::updateTabTitle(SessionTab *session)
{
    const int index = m_tabs->indexOf(session);
    if (index < 0)
        return;

    m_tabs->setTabText(index, session->displayTitle());
    m_tabs->setTabIcon(index, icon(QStringLiteral("terminal"),
                                   session->isConnected() ? Theme::current().success
                                                          : Theme::current().textMuted));
}

void MainWindow::newHost()
{
    HostEditorDialog dialog(this);

    ssh::HostProfile fresh;
    fresh.username = qEnvironmentVariable("USER");
    dialog.setProfile(fresh);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString id = m_store->add(dialog.profile());
    showStatus(tr("Host saved."));
    openSession(id);
}

void MainWindow::editHost(const QString &hostId)
{
    const auto stored = m_store->profileById(hostId);
    if (!stored)
        return;

    HostEditorDialog dialog(this);
    dialog.setProfile(m_store->withSecrets(hostId));

    if (dialog.exec() != QDialog::Accepted)
        return;

    m_store->update(dialog.profile());
    showStatus(tr("Host updated. Reconnect for the changes to take effect."));
}

void MainWindow::importSshConfig()
{
    const int imported = m_store->importFromSshConfig();

    if (imported == 0) {
        showStatus(tr("Nothing new to import from ~/.ssh/config."));
        return;
    }
    showStatus(tr("Imported %n host(s) from ~/.ssh/config.", nullptr, imported));
}

void MainWindow::closeTab(int index)
{
    SessionTab *session = sessionAt(index);
    if (session == nullptr)
        return;

    if (session->hasActiveTransfers()) {
        const auto answer = QMessageBox::warning(
            this, tr("Close Session"),
            tr("Transfers are still running on this session. Closing it cancels them."),
            QMessageBox::Cancel | QMessageBox::Close, QMessageBox::Cancel);
        if (answer != QMessageBox::Close)
            return;
    }

    m_tabs->removeTab(index);
    session->disconnectFromHost();
    session->deleteLater();
}

void MainWindow::closeCurrentTab()
{
    if (m_tabs->count() > 0)
        closeTab(m_tabs->currentIndex());
}

void MainWindow::reconnectCurrent()
{
    SessionTab *session = currentSession();
    if (session == nullptr)
        return;

    const ssh::HostProfile profile = session->profile();
    const int index = m_tabs->indexOf(session);

    m_tabs->removeTab(index);
    session->disconnectFromHost();
    session->deleteLater();

    auto *replacement = new SessionTab(profile, m_interaction, this);
    m_tabs->insertTab(index, replacement, profile.displayName());
    m_tabs->setCurrentIndex(index);

    connect(replacement, &SessionTab::titleChanged, this,
            [this, replacement](const QString &) { updateTabTitle(replacement); });
    connect(replacement, &SessionTab::statusMessage, this,
            [this](const QString &message) { showStatus(message); });
    connect(replacement, &SessionTab::connectionStateChanged, this,
            [this, replacement] { updateTabTitle(replacement); });

    replacement->connectToHost();
    replacement->focusTerminal();
}

void MainWindow::toggleFileBrowser()
{
    SessionTab *session = currentSession();
    if (session == nullptr)
        return;
    session->setFileBrowserVisible(!session->isFileBrowserVisible());
}

void MainWindow::showStatus(const QString &message, int timeoutMs)
{
    m_statusLabel->setText(message);

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, this, [this, message] {
            if (m_statusLabel->text() == message)
                m_statusLabel->clear();
        });
    }
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(1280, 800);

    const QByteArray state = settings.value(QStringLiteral("window/state")).toByteArray();
    if (!state.isEmpty())
        restoreState(state);

    if (auto *splitter = findChild<QSplitter *>(QStringLiteral("mainSplitter"))) {
        const QByteArray splitterState =
            settings.value(QStringLiteral("window/splitter")).toByteArray();
        if (!splitterState.isEmpty())
            splitter->restoreState(splitterState);
        else
            splitter->setSizes({240, 1040});
    }
}

void MainWindow::saveWindowState() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());

    if (auto *splitter = findChild<QSplitter *>(QStringLiteral("mainSplitter")))
        settings.setValue(QStringLiteral("window/splitter"), splitter->saveState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    int transferring = 0;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (SessionTab *session = sessionAt(i); session != nullptr && session->hasActiveTransfers())
            ++transferring;
    }

    if (transferring > 0) {
        const auto answer = QMessageBox::warning(
            this, tr("Quit ArTerm"),
            tr("%n session(s) still have transfers running. Quitting cancels them.", nullptr,
               transferring),
            QMessageBox::Cancel | QMessageBox::Close, QMessageBox::Cancel);
        if (answer != QMessageBox::Close) {
            event->ignore();
            return;
        }
    }

    saveWindowState();

    // Tear the sessions down explicitly so the worker threads are joined before
    // the Qt event loop goes away.
    while (m_tabs->count() > 0) {
        SessionTab *session = sessionAt(0);
        m_tabs->removeTab(0);
        if (session != nullptr) {
            session->disconnectFromHost();
            delete session;
        }
    }

    event->accept();
}

} // namespace arterm::ui
