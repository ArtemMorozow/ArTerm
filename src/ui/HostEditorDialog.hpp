#pragma once

#include "ssh/SshTypes.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

namespace arterm::ui {

/// Create or edit one host profile.
class HostEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit HostEditorDialog(QWidget *parent = nullptr);

    void setProfile(const ssh::HostProfile &profile);
    [[nodiscard]] ssh::HostProfile profile() const;

private:
    void buildUi();
    void updateAuthPage();
    void browseForKey();
    [[nodiscard]] bool validate();

    QLineEdit *m_label{nullptr};
    QLineEdit *m_hostname{nullptr};
    QSpinBox *m_port{nullptr};
    QLineEdit *m_username{nullptr};
    QComboBox *m_group{nullptr};

    QComboBox *m_authMethod{nullptr};
    QStackedWidget *m_authPages{nullptr};
    QLineEdit *m_password{nullptr};
    QLineEdit *m_keyPath{nullptr};
    QLineEdit *m_keyPassphrase{nullptr};
    QCheckBox *m_savePassword{nullptr};

    QLineEdit *m_startupDirectory{nullptr};
    QLineEdit *m_startupCommand{nullptr};
    QCheckBox *m_openFileBrowser{nullptr};
    QCheckBox *m_compression{nullptr};
    QCheckBox *m_strictHostKey{nullptr};
    QSpinBox *m_keepAlive{nullptr};
    QComboBox *m_transferBackend{nullptr};

    ssh::HostProfile m_profile;
};

} // namespace arterm::ui
