#include "ui/HostKeyDialog.hpp"

#include "ui/Theme.hpp"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace arterm::ui {

HostKeyDialog::HostKeyDialog(const ssh::HostKeyInfo &info, QWidget *parent)
    : QDialog(parent)
{
    const bool changed = (info.verdict == ssh::HostKeyVerdict::Mismatch);

    setWindowTitle(changed ? tr("Host Key Changed") : tr("Unknown Host"));
    setModal(true);
    setMinimumWidth(480);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 16);
    root->setSpacing(14);

    auto *heading = new QLabel(this);
    heading->setProperty("role", QStringLiteral("heading"));
    heading->setWordWrap(true);
    heading->setText(changed ? tr("The identity of %1 has changed").arg(info.hostname)
                             : tr("%1 is not in your known_hosts").arg(info.hostname));
    root->addWidget(heading);

    auto *body = new QLabel(this);
    body->setWordWrap(true);
    body->setText(
        changed
            ? tr("The key this server presented is different from the one stored in "
                 "~/.ssh/known_hosts. This happens after a legitimate reinstall or key rotation, "
                 "but it is also exactly what a machine-in-the-middle attack looks like.\n\n"
                 "Only continue if you know the key was changed on purpose. Verify the fingerprint "
                 "below out of band before accepting it.")
            : tr("This is the first connection to this server, so there is nothing to compare its "
                 "key against. Check the fingerprint below against the one shown on the server "
                 "(ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub) before accepting it."));
    root->addWidget(body);

    auto *details = new QFormLayout;
    details->setSpacing(8);
    details->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    const QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    const auto addRow = [&](const QString &label, const QString &value) {
        auto *field = new QLabel(value, this);
        field->setFont(monospace);
        field->setTextInteractionFlags(Qt::TextSelectableByMouse);
        details->addRow(label, field);
    };

    addRow(tr("Host"), QStringLiteral("%1:%2").arg(info.hostname).arg(info.port));
    addRow(tr("Key type"), info.keyType);
    addRow(tr("SHA256"), info.sha256);
    if (!info.md5.isEmpty())
        addRow(tr("MD5"), info.md5);

    root->addLayout(details);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    QPushButton *accept = buttons->addButton(changed ? tr("Accept the New Key") : tr("Trust and Connect"),
                                             QDialogButtonBox::AcceptRole);

    // A changed key gets the dangerous styling and Cancel keeps the focus, so
    // pressing Return does not silently trust an unexpected key.
    if (changed) {
        accept->setProperty("danger", true);
        cancel->setDefault(true);
        cancel->setFocus();
    } else {
        accept->setProperty("accent", true);
        accept->setDefault(true);
    }

    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool HostKeyDialog::ask(const ssh::HostKeyInfo &info, QWidget *parent)
{
    HostKeyDialog dialog(info, parent);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace arterm::ui
