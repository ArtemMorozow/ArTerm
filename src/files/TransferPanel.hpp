#pragma once

#include <QStyledItemDelegate>
#include <QWidget>

class QLabel;
class QTableView;
class QToolButton;

namespace arterm::files {

class TransferManager;

/// Draws the progress column as a slim bar plus its percentage.
class TransferProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const override;
};

/// The collapsible queue shown under the file browser.
class TransferPanel : public QWidget {
    Q_OBJECT

public:
    explicit TransferPanel(TransferManager *manager, QWidget *parent = nullptr);

    /// Expands the panel; called when a transfer starts.
    void expand();
    void collapse();
    [[nodiscard]] bool isExpanded() const noexcept { return m_expanded; }

    /// Height of the header strip, which is all that remains when collapsed.
    [[nodiscard]] static int headerHeight() noexcept;

Q_SIGNALS:
    /// The owner re-sizes its splitter in response; the panel deliberately does
    /// not pin its own height, because doing so squeezed the file lists to
    /// nothing when a transfer started.
    void expandedChanged(bool expanded);

private:
    void updateHeader();

    TransferManager *m_manager;
    QTableView *m_table{nullptr};
    QLabel *m_headerLabel{nullptr};
    QToolButton *m_toggleButton{nullptr};
    QToolButton *m_clearButton{nullptr};
    QToolButton *m_cancelButton{nullptr};
    bool m_expanded{false};
};

} // namespace arterm::files
