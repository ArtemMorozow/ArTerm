#include "ui/SessionTab.hpp"

#include "files/FileBrowser.hpp"
#include "files/TransferManager.hpp"
#include "session/RemoteFileService.hpp"
#include "session/ShellService.hpp"
#include "ssh/SessionInteraction.hpp"
#include "terminal/TerminalWidget.hpp"
#include "ui/Theme.hpp"

#include <QLabel>
#include <QSplitter>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>

namespace arterm::ui {

SessionTab::SessionTab(ssh::HostProfile profile,
                       std::shared_ptr<ssh::SessionInteraction> interaction, QWidget *parent)
    : QWidget(parent)
    , m_profile(std::move(profile))
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_interaction(std::move(interaction))
{
    buildUi();
}

SessionTab::~SessionTab()
{
    disconnectFromHost();
}

void SessionTab::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_banner = new QLabel(this);
    m_banner->setObjectName(QStringLiteral("connectionBanner"));
    m_banner->setWordWrap(true);
    m_banner->hide();

    m_terminal = new term::TerminalWidget(this);

    const Theme &theme = Theme::current();
    term::ColorScheme scheme = theme.mode() == Theme::Mode::Dark ? term::ColorScheme::arTermDark()
                                                                 : term::ColorScheme::arTermLight();
    scheme.setBackground(theme.terminalBackground);
    m_terminal->setColorScheme(scheme);

    m_shell = new session::ShellService(m_profile, m_interaction, this);
    m_files = new session::RemoteFileService(m_profile, m_interaction, this);
    m_files->setTransferBackend(m_profile.transferBackend);

    m_browser = new files::FileBrowser(m_files, m_sessionId, this);
    m_browser->setRemoteTitle(m_profile.username.isEmpty()
                                  ? m_profile.hostname
                                  : m_profile.username + QLatin1Char('@') + m_profile.hostname);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_terminal);
    m_splitter->addWidget(m_browser);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);

    // Stretch factors alone leave the browser squeezed, because the terminal's
    // size hint is much larger. Seed explicit sizes so both panes start usable.
    m_splitter->setSizes({560, 380});
    m_browser->setMinimumHeight(220);

    root->addWidget(m_banner);
    root->addWidget(m_splitter, 1);

    m_browser->setVisible(m_profile.openFileBrowser);

    wireShell();
    wireFiles();
}

void SessionTab::wireShell()
{
    connect(m_terminal, &term::TerminalWidget::dataEntered, m_shell, &session::ShellService::write);

    connect(m_terminal, &term::TerminalWidget::terminalResized, m_shell,
            &session::ShellService::resize);

    connect(m_terminal, &term::TerminalWidget::titleChanged, this, [this](const QString &title) {
        m_remoteTitle = title;
        Q_EMIT titleChanged(displayTitle());
    });

    // Files dropped on the terminal are uploaded into whatever directory the
    // remote pane is showing, which is the least surprising interpretation.
    connect(m_terminal, &term::TerminalWidget::filesDropped, this, [this](const QStringList &paths) {
        if (!m_browser->isVisible())
            setFileBrowserVisible(true);
        m_browser->uploadToCurrentRemoteDirectory(paths);
    });

    connect(m_shell, &session::ShellService::dataReceived, m_terminal,
            &term::TerminalWidget::receive);

    connect(m_shell, &session::ShellService::connected, this,
            [this](const QString &, const QString &authMethod) {
                m_shellReady = true;
                hideBanner();
                Q_EMIT statusMessage(tr("Connected to %1 (%2)").arg(m_profile.endpoint(), authMethod));
                Q_EMIT connectionStateChanged();

                // The PTY was opened with the default 80x24; push the real size.
                m_terminal->terminal()->resize(m_terminal->columns(), m_terminal->rows());
                m_shell->resize(m_terminal->columns(), m_terminal->rows(), m_terminal->width(),
                                m_terminal->height());
            });

    connect(m_shell, &session::ShellService::stateChanged, this,
            [this](ssh::ShellSession::State state) {
                switch (state) {
                case ssh::ShellSession::State::Connecting:
                    showBanner(tr("Connecting to %1…").arg(m_profile.endpoint()),
                               QStringLiteral("connecting"));
                    break;
                case ssh::ShellSession::State::Authenticating:
                    showBanner(tr("Authenticating as %1…").arg(m_profile.username),
                               QStringLiteral("connecting"));
                    break;
                default:
                    break;
                }
                Q_EMIT connectionStateChanged();
            });

    connect(m_shell, &session::ShellService::failed, this, [this](const Error &error) {
        m_shellReady = false;
        showBanner(error.message, QStringLiteral("error"));
        Q_EMIT statusMessage(error.message);
        Q_EMIT connectionStateChanged();
    });

    connect(m_shell, &session::ShellService::closed, this, [this](int exitStatus) {
        m_shellReady = false;
        showBanner(tr("The session ended."), QStringLiteral("error"));
        Q_EMIT connectionStateChanged();
        Q_EMIT sessionClosed(exitStatus);
    });
}

void SessionTab::wireFiles()
{
    connect(m_files, &session::RemoteFileService::ready, this, [this](const QString &home) {
        m_browser->setConnected(true, home);
        Q_EMIT connectionStateChanged();
    });

    connect(m_files, &session::RemoteFileService::failed, this, [this](const Error &error) {
        m_browser->setConnected(false, {});
        // A failing SFTP subsystem must not look like a failed session: the
        // shell may well be fine, so this is reported as a status message.
        Q_EMIT statusMessage(tr("File browser unavailable: %1").arg(error.message));
    });

    connect(m_browser, &files::FileBrowser::statusMessage, this, &SessionTab::statusMessage);
}

void SessionTab::connectToHost()
{
    showBanner(tr("Connecting to %1…").arg(m_profile.endpoint()), QStringLiteral("connecting"));

    m_shell->start();

    if (m_profile.openFileBrowser)
        m_files->start();
}

void SessionTab::disconnectFromHost()
{
    if (m_shell != nullptr)
        m_shell->stop();
    if (m_files != nullptr)
        m_files->stop();
}

bool SessionTab::isConnected() const
{
    return m_shellReady;
}

bool SessionTab::hasActiveTransfers() const
{
    return m_browser != nullptr && m_browser->transfers()->hasActiveTransfers();
}

QString SessionTab::displayTitle() const
{
    if (!m_remoteTitle.isEmpty())
        return m_remoteTitle;
    return m_profile.displayName();
}

void SessionTab::setFileBrowserVisible(bool visible)
{
    m_browser->setVisible(visible);

    // Start the SFTP connection lazily: a profile with the browser turned off
    // should not open a second connection until the user asks for one.
    if (visible && !m_files->isConnected())
        m_files->start();
}

bool SessionTab::isFileBrowserVisible() const
{
    return m_browser->isVisible();
}

void SessionTab::focusTerminal()
{
    m_terminal->setFocus(Qt::OtherFocusReason);
}

void SessionTab::showBanner(const QString &message, const QString &state)
{
    m_banner->setText(message);
    m_banner->setProperty("state", state);
    m_banner->style()->unpolish(m_banner);
    m_banner->style()->polish(m_banner);
    m_banner->show();
}

void SessionTab::hideBanner()
{
    m_banner->hide();
}

} // namespace arterm::ui
