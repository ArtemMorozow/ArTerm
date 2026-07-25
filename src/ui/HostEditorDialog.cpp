#include "ui/HostEditorDialog.hpp"

#include "model/SecretStore.hpp"
#include "ui/Icons.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace arterm::ui {
namespace {

/// Order must match the entries added to `m_authMethod`.
constexpr ssh::AuthMethod kAuthOrder[] = {
    ssh::AuthMethod::Agent,
    ssh::AuthMethod::PublicKey,
    ssh::AuthMethod::Password,
};

int indexForAuth(ssh::AuthMethod method)
{
    for (int i = 0; i < 3; ++i) {
        if (kAuthOrder[i] == method)
            return i;
    }
    // Keyboard-interactive shares the password page: both are answered with the
    // same secret, and the connection tries them in turn.
    return method == ssh::AuthMethod::KeyboardInteractive ? 2 : 0;
}

} // namespace

HostEditorDialog::HostEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
}

void HostEditorDialog::buildUi()
{
    setWindowTitle(tr("Host"));
    setModal(true);
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    auto *heading = new QLabel(tr("Connection"), this);
    heading->setProperty("role", QStringLiteral("heading"));
    root->addWidget(heading);

    auto *tabs = new QTabWidget(this);

    // -- General -----------------------------------------------------------
    auto *general = new QWidget(tabs);
    auto *generalLayout = new QFormLayout(general);
    generalLayout->setContentsMargins(4, 14, 4, 4);
    generalLayout->setSpacing(10);
    generalLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_label = new QLineEdit(general);
    m_label->setPlaceholderText(tr("Optional display name"));

    m_hostname = new QLineEdit(general);
    m_hostname->setPlaceholderText(tr("example.com or 10.0.0.5"));

    m_port = new QSpinBox(general);
    m_port->setRange(1, 65535);
    m_port->setValue(22);

    m_username = new QLineEdit(general);
    m_username->setPlaceholderText(qEnvironmentVariable("USER"));

    m_group = new QComboBox(general);
    m_group->setEditable(true);
    m_group->addItems({tr("Hosts"), tr("Production"), tr("Staging"), tr("Personal")});

    generalLayout->addRow(tr("Label"), m_label);
    generalLayout->addRow(tr("Host"), m_hostname);
    generalLayout->addRow(tr("Port"), m_port);
    generalLayout->addRow(tr("Username"), m_username);
    generalLayout->addRow(tr("Group"), m_group);

    // -- Authentication ----------------------------------------------------
    auto *auth = new QWidget(tabs);
    auto *authLayout = new QVBoxLayout(auth);
    authLayout->setContentsMargins(4, 14, 4, 4);
    authLayout->setSpacing(10);

    auto *methodRow = new QFormLayout;
    m_authMethod = new QComboBox(auth);
    m_authMethod->addItem(tr("SSH agent"));
    m_authMethod->addItem(tr("Private key"));
    m_authMethod->addItem(tr("Password"));
    methodRow->addRow(tr("Method"), m_authMethod);
    authLayout->addLayout(methodRow);

    m_authPages = new QStackedWidget(auth);

    // Agent page.
    auto *agentPage = new QWidget(m_authPages);
    auto *agentLayout = new QVBoxLayout(agentPage);
    agentLayout->setContentsMargins(0, 4, 0, 0);
    auto *agentHint = new QLabel(
        tr("Uses the keys loaded in ssh-agent (SSH_AUTH_SOCK). Add one with\n"
           "ssh-add ~/.ssh/id_ed25519."),
        agentPage);
    agentHint->setProperty("role", QStringLiteral("caption"));
    agentHint->setWordWrap(true);
    agentLayout->addWidget(agentHint);
    agentLayout->addStretch(1);
    m_authPages->addWidget(agentPage);

    // Key page.
    auto *keyPage = new QWidget(m_authPages);
    auto *keyLayout = new QFormLayout(keyPage);
    keyLayout->setContentsMargins(0, 4, 0, 0);

    auto *keyRow = new QWidget(keyPage);
    auto *keyRowLayout = new QHBoxLayout(keyRow);
    keyRowLayout->setContentsMargins(0, 0, 0, 0);
    keyRowLayout->setSpacing(6);

    m_keyPath = new QLineEdit(keyRow);
    m_keyPath->setPlaceholderText(QDir::homePath() + QLatin1String("/.ssh/id_ed25519"));

    auto *browseButton = new QToolButton(keyRow);
    browseButton->setIcon(icon(QStringLiteral("folder")));
    browseButton->setToolTip(tr("Choose a key file"));

    keyRowLayout->addWidget(m_keyPath, 1);
    keyRowLayout->addWidget(browseButton);

    m_keyPassphrase = new QLineEdit(keyPage);
    m_keyPassphrase->setEchoMode(QLineEdit::Password);
    m_keyPassphrase->setPlaceholderText(tr("Leave empty to be asked when needed"));

    keyLayout->addRow(tr("Private key"), keyRow);
    keyLayout->addRow(tr("Passphrase"), m_keyPassphrase);
    m_authPages->addWidget(keyPage);

    // Password page.
    auto *passwordPage = new QWidget(m_authPages);
    auto *passwordLayout = new QFormLayout(passwordPage);
    passwordLayout->setContentsMargins(0, 4, 0, 0);

    m_password = new QLineEdit(passwordPage);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("Leave empty to be asked on every connection"));

    passwordLayout->addRow(tr("Password"), m_password);
    m_authPages->addWidget(passwordPage);

    authLayout->addWidget(m_authPages, 1);

    m_savePassword = new QCheckBox(tr("Save secrets in the %1").arg(model::SecretStore::backendName()),
                                   auth);
    m_savePassword->setChecked(model::SecretStore::isAvailable());
    m_savePassword->setEnabled(model::SecretStore::isAvailable());
    authLayout->addWidget(m_savePassword);

    // -- Advanced ----------------------------------------------------------
    auto *advanced = new QWidget(tabs);
    auto *advancedLayout = new QFormLayout(advanced);
    advancedLayout->setContentsMargins(4, 14, 4, 4);
    advancedLayout->setSpacing(10);
    advancedLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_startupDirectory = new QLineEdit(advanced);
    m_startupDirectory->setPlaceholderText(tr("Login directory"));

    m_startupCommand = new QLineEdit(advanced);
    m_startupCommand->setPlaceholderText(tr("e.g. tmux attach || tmux new"));

    m_openFileBrowser = new QCheckBox(tr("Open the file browser with this session"), advanced);
    m_openFileBrowser->setChecked(true);

    m_compression = new QCheckBox(tr("Enable compression"), advanced);

    m_strictHostKey = new QCheckBox(tr("Verify the host key against known_hosts"), advanced);
    m_strictHostKey->setChecked(true);

    m_transferBackend = new QComboBox(advanced);
    m_transferBackend->addItem(tr("SFTP (recommended)"), static_cast<int>(ssh::TransferBackend::Sftp));
    m_transferBackend->addItem(tr("SCP"), static_cast<int>(ssh::TransferBackend::Scp));
    m_transferBackend->setToolTip(
        tr("Directory browsing always uses SFTP, because SCP cannot list a directory.\n"
           "This setting only chooses how file contents are copied. Pick SCP for hosts\n"
           "that disable the SFTP subsystem, or on very high-latency links."));

    m_keepAlive = new QSpinBox(advanced);
    m_keepAlive->setRange(0, 600);
    m_keepAlive->setValue(30);
    m_keepAlive->setSuffix(tr(" s"));
    m_keepAlive->setSpecialValueText(tr("off"));

    advancedLayout->addRow(tr("Remote directory"), m_startupDirectory);
    advancedLayout->addRow(tr("Startup command"), m_startupCommand);
    advancedLayout->addRow(QString(), m_openFileBrowser);
    advancedLayout->addRow(QString(), m_compression);
    advancedLayout->addRow(QString(), m_strictHostKey);
    advancedLayout->addRow(tr("Keep-alive"), m_keepAlive);
    advancedLayout->addRow(tr("Transfers via"), m_transferBackend);

    tabs->addTab(general, tr("General"));
    tabs->addTab(auth, tr("Authentication"));
    tabs->addTab(advanced, tr("Advanced"));

    root->addWidget(tabs, 1);

    // -- Buttons -----------------------------------------------------------
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setProperty("accent", true);
    root->addWidget(buttons);

    connect(browseButton, &QToolButton::clicked, this, &HostEditorDialog::browseForKey);
    connect(m_authMethod, &QComboBox::currentIndexChanged, this, &HostEditorDialog::updateAuthPage);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (validate())
            accept();
    });

    updateAuthPage();
}

void HostEditorDialog::updateAuthPage()
{
    m_authPages->setCurrentIndex(m_authMethod->currentIndex());
}

void HostEditorDialog::browseForKey()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Select a private key"),
                                                      QDir::homePath() + QLatin1String("/.ssh"),
                                                      tr("All files (*)"));
    if (!path.isEmpty())
        m_keyPath->setText(path);
}

bool HostEditorDialog::validate()
{
    if (m_hostname->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Host"), tr("Enter a hostname or an IP address."));
        m_hostname->setFocus();
        return false;
    }

    if (m_username->text().trimmed().isEmpty() && qEnvironmentVariable("USER").isEmpty()) {
        QMessageBox::warning(this, tr("Host"), tr("Enter a username."));
        m_username->setFocus();
        return false;
    }

    if (kAuthOrder[m_authMethod->currentIndex()] == ssh::AuthMethod::PublicKey
        && m_keyPath->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("Host"), tr("Choose a private key file."));
        m_keyPath->setFocus();
        return false;
    }

    return true;
}

void HostEditorDialog::setProfile(const ssh::HostProfile &profile)
{
    m_profile = profile;

    setWindowTitle(profile.id.isEmpty() ? tr("New Host") : tr("Edit Host"));

    m_label->setText(profile.label);
    m_hostname->setText(profile.hostname);
    m_port->setValue(profile.port);
    m_username->setText(profile.username);
    m_group->setCurrentText(profile.group.isEmpty() ? tr("Hosts") : profile.group);

    m_authMethod->setCurrentIndex(indexForAuth(profile.preferredAuth));
    m_keyPath->setText(profile.privateKeyPath);
    m_keyPassphrase->setText(profile.keyPassphrase);
    m_password->setText(profile.password);

    m_startupDirectory->setText(profile.startupDirectory);
    m_startupCommand->setText(profile.startupCommand);
    m_openFileBrowser->setChecked(profile.openFileBrowser);
    m_compression->setChecked(profile.compression);
    m_strictHostKey->setChecked(profile.strictHostKeyChecking);
    m_keepAlive->setValue(profile.keepAliveSeconds);
    m_transferBackend->setCurrentIndex(
        m_transferBackend->findData(static_cast<int>(profile.transferBackend)));

    updateAuthPage();
}

ssh::HostProfile HostEditorDialog::profile() const
{
    ssh::HostProfile profile = m_profile;

    profile.label = m_label->text().trimmed();
    profile.hostname = m_hostname->text().trimmed();
    profile.port = static_cast<quint16>(m_port->value());
    profile.username = m_username->text().trimmed();
    if (profile.username.isEmpty())
        profile.username = qEnvironmentVariable("USER");
    profile.group = m_group->currentText().trimmed();

    profile.preferredAuth = kAuthOrder[m_authMethod->currentIndex()];
    profile.privateKeyPath = m_keyPath->text().trimmed();
    profile.useAgent = (profile.preferredAuth == ssh::AuthMethod::Agent);

    // Secrets are only carried out of the dialog when the user asked for them
    // to be saved; otherwise the connection prompts each time.
    if (m_savePassword->isChecked()) {
        profile.password = m_password->text();
        profile.keyPassphrase = m_keyPassphrase->text();
    } else {
        profile.password.clear();
        profile.keyPassphrase.clear();
    }

    profile.startupDirectory = m_startupDirectory->text().trimmed();
    profile.startupCommand = m_startupCommand->text().trimmed();
    profile.openFileBrowser = m_openFileBrowser->isChecked();
    profile.compression = m_compression->isChecked();
    profile.strictHostKeyChecking = m_strictHostKey->isChecked();
    profile.keepAliveSeconds = m_keepAlive->value();
    profile.transferBackend =
        static_cast<ssh::TransferBackend>(m_transferBackend->currentData().toInt());

    return profile;
}

} // namespace arterm::ui
