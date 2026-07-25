#pragma once

#include "ssh/SshTypes.hpp"

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace arterm::model {
class HostStore;
}

namespace arterm::ui {

/// The host list on the left: search field, grouped entries, and the actions
/// that create, edit and delete profiles.
class HostSidebar : public QWidget {
    Q_OBJECT

public:
    HostSidebar(model::HostStore *store, QWidget *parent = nullptr);

    [[nodiscard]] QString selectedHostId() const;

Q_SIGNALS:
    /// The user activated a host and wants a session opened.
    void connectRequested(const QString &hostId);
    void editRequested(const QString &hostId);
    void newHostRequested();

public Q_SLOTS:
    void rebuild();
    void focusSearch();

private:
    void showContextMenu(const QPoint &position);
    void deleteSelected();

    model::HostStore *m_store;
    QLineEdit *m_search{nullptr};
    QListWidget *m_list{nullptr};
    QString m_filter;
};

} // namespace arterm::ui
