#include "ui/HostSidebar.hpp"

#include "model/HostStore.hpp"
#include "ui/Icons.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace arterm::ui {
namespace {

constexpr int kHostIdRole = Qt::UserRole + 1;
constexpr int kIsGroupRole = Qt::UserRole + 2;

} // namespace

HostSidebar::HostSidebar(model::HostStore *store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setObjectName(QStringLiteral("sidebar"));
    setMinimumWidth(210);
    setMaximumWidth(360);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // -- Header ------------------------------------------------------------
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("sidebarHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(14, 12, 10, 8);
    headerLayout->setSpacing(6);

    auto *title = new QLabel(tr("Hosts"), header);
    title->setObjectName(QStringLiteral("sidebarTitle"));

    auto *addButton = new QToolButton(header);
    addButton->setIcon(icon(QStringLiteral("plus")));
    addButton->setToolTip(tr("New host (⌘N)"));
    addButton->setAutoRaise(true);
    addButton->setIconSize(QSize(16, 16));

    headerLayout->addWidget(title, 1);
    headerLayout->addWidget(addButton);

    // -- Search ------------------------------------------------------------
    auto *searchWrapper = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(searchWrapper);
    searchLayout->setContentsMargins(10, 0, 10, 8);

    m_search = new QLineEdit(searchWrapper);
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setPlaceholderText(tr("Search hosts"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
    searchLayout->addWidget(m_search);

    // -- List --------------------------------------------------------------
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("hostList"));
    m_list->setFrameStyle(QFrame::NoFrame);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setUniformItemSizes(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    root->addWidget(header);
    root->addWidget(searchWrapper);
    root->addWidget(m_list, 1);

    connect(addButton, &QToolButton::clicked, this, &HostSidebar::newHostRequested);

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_filter = text.trimmed();
        rebuild();
    });

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item == nullptr || item->data(kIsGroupRole).toBool())
            return;
        Q_EMIT connectRequested(item->data(kHostIdRole).toString());
    });

    connect(m_list, &QListWidget::customContextMenuRequested, this, &HostSidebar::showContextMenu);

    connect(m_store, &model::HostStore::changed, this, &HostSidebar::rebuild);

    rebuild();
}

QString HostSidebar::selectedHostId() const
{
    QListWidgetItem *item = m_list->currentItem();
    if (item == nullptr || item->data(kIsGroupRole).toBool())
        return {};
    return item->data(kHostIdRole).toString();
}

void HostSidebar::focusSearch()
{
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void HostSidebar::rebuild()
{
    const QString previous = selectedHostId();

    m_list->clear();

    // Bucket the profiles by group so each group can get its own heading.
    QMap<QString, QVector<ssh::HostProfile>> grouped;
    for (const ssh::HostProfile &profile : m_store->profiles()) {
        if (!m_filter.isEmpty()) {
            const bool matches = profile.displayName().contains(m_filter, Qt::CaseInsensitive)
                                 || profile.hostname.contains(m_filter, Qt::CaseInsensitive)
                                 || profile.username.contains(m_filter, Qt::CaseInsensitive)
                                 || profile.group.contains(m_filter, Qt::CaseInsensitive);
            if (!matches)
                continue;
        }

        grouped[profile.group.isEmpty() ? tr("Hosts") : profile.group].append(profile);
    }

    if (grouped.isEmpty()) {
        auto *empty = new QListWidgetItem(m_filter.isEmpty()
                                              ? tr("No hosts yet.\nPress ⌘N to add one.")
                                              : tr("Nothing matches “%1”").arg(m_filter));
        empty->setFlags(Qt::NoItemFlags);
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setForeground(QColor(0x6B, 0x74, 0x82));
        empty->setSizeHint(QSize(0, 72));
        m_list->addItem(empty);
        return;
    }

    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        auto *groupItem = new QListWidgetItem(it.key().toUpper());
        groupItem->setData(kIsGroupRole, true);
        groupItem->setFlags(Qt::NoItemFlags);
        groupItem->setForeground(QColor(0x6B, 0x74, 0x82));
        groupItem->setSizeHint(QSize(0, 28));

        QFont groupFont = groupItem->font();
        groupFont.setPointSizeF(groupFont.pointSizeF() - 1.5);
        groupFont.setBold(true);
        groupItem->setFont(groupFont);

        m_list->addItem(groupItem);

        QVector<ssh::HostProfile> hosts = it.value();
        std::sort(hosts.begin(), hosts.end(),
                  [](const ssh::HostProfile &a, const ssh::HostProfile &b) {
                      return a.displayName().compare(b.displayName(), Qt::CaseInsensitive) < 0;
                  });

        for (const ssh::HostProfile &profile : hosts) {
            auto *item = new QListWidgetItem(profile.displayName());
            item->setData(kHostIdRole, profile.id);
            item->setData(kIsGroupRole, false);
            item->setIcon(icon(QStringLiteral("server")));
            item->setToolTip(tr("%1@%2:%3")
                                 .arg(profile.username, profile.hostname)
                                 .arg(profile.port));
            item->setSizeHint(QSize(0, 34));
            m_list->addItem(item);

            if (profile.id == previous)
                m_list->setCurrentItem(item);
        }
    }
}

void HostSidebar::showContextMenu(const QPoint &position)
{
    QListWidgetItem *item = m_list->itemAt(position);
    const bool isHost = item != nullptr && !item->data(kIsGroupRole).toBool();

    QMenu menu(this);

    if (isHost) {
        m_list->setCurrentItem(item);
        const QString hostId = item->data(kHostIdRole).toString();

        menu.addAction(icon(QStringLiteral("terminal")), tr("Connect"), this,
                       [this, hostId] { Q_EMIT connectRequested(hostId); });
        menu.addAction(icon(QStringLiteral("edit")), tr("Edit…"), this,
                       [this, hostId] { Q_EMIT editRequested(hostId); });
        menu.addSeparator();
        menu.addAction(icon(QStringLiteral("trash")), tr("Delete…"), this,
                       &HostSidebar::deleteSelected);
        menu.addSeparator();
    }

    menu.addAction(icon(QStringLiteral("plus")), tr("New Host…"), this,
                   &HostSidebar::newHostRequested);

    menu.exec(m_list->viewport()->mapToGlobal(position));
}

void HostSidebar::deleteSelected()
{
    const QString hostId = selectedHostId();
    if (hostId.isEmpty())
        return;

    const auto profile = m_store->profileById(hostId);
    if (!profile)
        return;

    const auto answer = QMessageBox::warning(
        this, tr("Delete Host"),
        tr("Delete “%1”?\nSaved credentials for this host are removed from the keychain as well.")
            .arg(profile->displayName()),
        QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);

    if (answer == QMessageBox::Yes)
        m_store->remove(hostId);
}

} // namespace arterm::ui
