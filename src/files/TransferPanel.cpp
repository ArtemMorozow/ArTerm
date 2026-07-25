#include "files/TransferPanel.hpp"

#include "files/TransferManager.hpp"
#include "ui/Icons.hpp"
#include "ui/Theme.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace arterm::files {
namespace {

constexpr int kCollapsedHeight = 30;
constexpr int kExpandedHeight = 168;

} // namespace

void TransferProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    const auto state = static_cast<ssh::TransferState>(index.data(TransferManager::StateRole).toInt());
    const double fraction = index.data(TransferManager::FractionRole).toDouble();

    QStyleOptionViewItem adjusted = option;
    initStyleOption(&adjusted, index);

    painter->save();
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());

    const ui::Theme &theme = ui::Theme::current();

    // The byte counts need room to the right of the bar; without this reserve
    // the text is clipped to "195 KB / 195".
    constexpr int kTextWidth = 140;
    const qreal trackWidth = std::max(24.0, static_cast<qreal>(option.rect.width() - kTextWidth - 16));
    const QRectF track(option.rect.left() + 8, option.rect.center().y() - 3, trackWidth, 6);

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(theme.elevated);
    painter->drawRoundedRect(track, 3, 3);

    QColor fillColor = theme.accent;
    switch (state) {
    case ssh::TransferState::Completed:
        fillColor = theme.success;
        break;
    case ssh::TransferState::Failed:
        fillColor = theme.danger;
        break;
    case ssh::TransferState::Cancelled:
        fillColor = theme.textMuted;
        break;
    default:
        break;
    }

    const double clamped = std::clamp(fraction, 0.0, 1.0);
    if (clamped > 0.0) {
        QRectF fill = track;
        fill.setWidth(track.width() * clamped);
        painter->setBrush(fillColor);
        painter->drawRoundedRect(fill, 3, 3);
    }

    // The byte counts the model formats sit to the right of the bar.
    painter->setPen(theme.textMuted);
    const QRect textRect(static_cast<int>(track.right()) + 8, option.rect.top(), kTextWidth,
                         option.rect.height());
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());

    painter->restore();
}

QSize TransferProgressDelegate::sizeHint(const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(std::max(size.width(), 240));
    return size;
}

TransferPanel::TransferPanel(TransferManager *manager, QWidget *parent)
    : QWidget(parent)
    , m_manager(manager)
{
    setObjectName(QStringLiteral("transferPanel"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // -- Header ------------------------------------------------------------
    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 4, 8, 4);
    headerLayout->setSpacing(6);

    m_toggleButton = new QToolButton(header);
    m_toggleButton->setIcon(ui::icon(QStringLiteral("chevron-down")));
    m_toggleButton->setAutoRaise(true);
    m_toggleButton->setIconSize(QSize(14, 14));

    m_headerLabel = new QLabel(header);
    m_headerLabel->setObjectName(QStringLiteral("transferPanelHeader"));

    m_cancelButton = new QToolButton(header);
    m_cancelButton->setIcon(ui::icon(QStringLiteral("close")));
    m_cancelButton->setToolTip(tr("Cancel all transfers"));
    m_cancelButton->setAutoRaise(true);
    m_cancelButton->setIconSize(QSize(14, 14));

    m_clearButton = new QToolButton(header);
    m_clearButton->setIcon(ui::icon(QStringLiteral("trash")));
    m_clearButton->setToolTip(tr("Clear finished transfers"));
    m_clearButton->setAutoRaise(true);
    m_clearButton->setIconSize(QSize(14, 14));

    headerLayout->addWidget(m_toggleButton);
    headerLayout->addWidget(m_headerLabel, 1);
    headerLayout->addWidget(m_cancelButton);
    headerLayout->addWidget(m_clearButton);

    // -- Table -------------------------------------------------------------
    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("transferTable"));
    m_table->setModel(m_manager);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setFrameStyle(QFrame::NoFrame);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(26);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setItemDelegateForColumn(static_cast<int>(TransferManager::Column::Progress),
                                      new TransferProgressDelegate(this));

    m_table->horizontalHeader()->setSectionResizeMode(
        static_cast<int>(TransferManager::Column::Name), QHeaderView::Stretch);

    root->addWidget(header);
    root->addWidget(m_table, 1);

    connect(m_toggleButton, &QToolButton::clicked, this, [this] {
        if (m_expanded)
            collapse();
        else
            expand();
    });
    connect(m_clearButton, &QToolButton::clicked, m_manager, &TransferManager::clearCompleted);
    connect(m_cancelButton, &QToolButton::clicked, m_manager, &TransferManager::cancelAll);

    connect(m_manager, &QAbstractItemModel::rowsInserted, this, [this] {
        updateHeader();
        expand();
    });
    connect(m_manager, &QAbstractItemModel::rowsRemoved, this, &TransferPanel::updateHeader);
    connect(m_manager, &QAbstractItemModel::dataChanged, this, &TransferPanel::updateHeader);

    collapse();
    updateHeader();
}

int TransferPanel::headerHeight() noexcept
{
    return kCollapsedHeight;
}

void TransferPanel::expand()
{
    if (m_expanded)
        return;

    m_expanded = true;
    m_table->show();
    m_toggleButton->setIcon(ui::icon(QStringLiteral("chevron-down")));
    setMinimumHeight(kCollapsedHeight);
    setMaximumHeight(QWIDGETSIZE_MAX);

    Q_EMIT expandedChanged(true);
}

void TransferPanel::collapse()
{
    m_expanded = false;
    m_table->hide();
    m_toggleButton->setIcon(ui::icon(QStringLiteral("chevron-right")));
    setMinimumHeight(kCollapsedHeight);
    setMaximumHeight(kCollapsedHeight);

    Q_EMIT expandedChanged(false);
}

void TransferPanel::updateHeader()
{
    const int active = m_manager->activeCount();
    const int total = m_manager->rowCount();

    if (total == 0) {
        m_headerLabel->setText(tr("TRANSFERS"));
        m_cancelButton->setEnabled(false);
        m_clearButton->setEnabled(false);
        return;
    }

    m_headerLabel->setText(active > 0 ? tr("TRANSFERS — %n active", nullptr, active)
                                      : tr("TRANSFERS — %n finished", nullptr, total));
    m_cancelButton->setEnabled(active > 0);
    m_clearButton->setEnabled(active < total);
}

} // namespace arterm::files
