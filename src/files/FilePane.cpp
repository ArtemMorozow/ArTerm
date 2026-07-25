#include "files/FilePane.hpp"

#include "files/FileListModel.hpp"
#include "ui/Icons.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QSortFilterProxyModel>
#include <QStackedLayout>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcFiles, "arterm.files")

namespace arterm::files {
namespace {

/// Sorts by the raw value behind each column rather than by the formatted
/// string, so "9 bytes" does not end up after "10 KB".
class FileSortProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const bool leftIsDir = left.data(IsDirectoryRole).toBool();
        const bool rightIsDir = right.data(IsDirectoryRole).toBool();

        // Folders always lead, regardless of the sort column or order. The
        // order is inverted for descending sorts so they do not sink instead.
        if (leftIsDir != rightIsDir)
            return sortOrder() == Qt::AscendingOrder ? leftIsDir : !leftIsDir;

        switch (static_cast<FileColumn>(left.column())) {
        case FileColumn::Size:
            return left.data(SizeRole).toULongLong() < right.data(SizeRole).toULongLong();
        case FileColumn::Modified:
            return left.data(ModifiedRole).toDateTime() < right.data(ModifiedRole).toDateTime();
        default:
            return left.data(NameRole).toString().compare(right.data(NameRole).toString(),
                                                          Qt::CaseInsensitive)
                   < 0;
        }
    }

    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (filterRegularExpression().pattern().isEmpty())
            return true;
        const QModelIndex index = sourceModel()->index(row, 0, parent);
        return index.data(NameRole).toString().contains(filterRegularExpression());
    }
};

} // namespace

// ---------------------------------------------------------------------------
// FileTableView
// ---------------------------------------------------------------------------

FileTableView::FileTableView(QWidget *parent)
    : QWidget(parent)
    , m_view(new QTableView(this))
    , m_proxy(new FileSortProxy(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_view->setObjectName(QStringLiteral("fileTable"));
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(false);
    m_view->setShowGrid(false);
    m_view->setWordWrap(false);
    m_view->setFrameStyle(QFrame::NoFrame);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->setDragDropMode(QAbstractItemView::DragDrop);
    m_view->setDefaultDropAction(Qt::CopyAction);

    m_view->verticalHeader()->setVisible(false);
    m_view->verticalHeader()->setDefaultSectionSize(26);
    m_view->horizontalHeader()->setHighlightSections(false);
    m_view->horizontalHeader()->setStretchLastSection(false);
    m_view->horizontalHeader()->setSectionsMovable(false);

    // The viewport is what actually receives drag events.
    m_view->viewport()->installEventFilter(this);

    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        Q_EMIT activated(index.data(PathRole).toString(), index.data(IsDirectoryRole).toBool());
    });
    connect(m_view, &QTableView::customContextMenuRequested, this, [this](const QPoint &position) {
        Q_EMIT contextMenuRequested(m_view->viewport()->mapToGlobal(position));
    });
}

void FileTableView::setModel(FileListModel *model)
{
    m_model = model;
    m_proxy->setSourceModel(model);
    m_view->setModel(m_proxy);

    m_view->sortByColumn(static_cast<int>(FileColumn::Name), Qt::AscendingOrder);

    QHeaderView *header = m_view->horizontalHeader();
    header->setSectionResizeMode(static_cast<int>(FileColumn::Name), QHeaderView::Stretch);
    header->setSectionResizeMode(static_cast<int>(FileColumn::Size), QHeaderView::ResizeToContents);
    header->setSectionResizeMode(static_cast<int>(FileColumn::Modified), QHeaderView::ResizeToContents);
    header->setSectionResizeMode(static_cast<int>(FileColumn::Permissions), QHeaderView::ResizeToContents);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this] { Q_EMIT selectionChanged(); });
}

QStringList FileTableView::selectedPaths() const
{
    QStringList paths;
    if (m_view->selectionModel() == nullptr)
        return paths;

    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    paths.reserve(rows.size());
    for (const QModelIndex &index : rows) {
        const QString path = index.data(PathRole).toString();
        if (!path.isEmpty())
            paths << path;
    }
    return paths;
}

bool FileTableView::hasSelection() const
{
    return m_view->selectionModel() != nullptr && m_view->selectionModel()->hasSelection();
}

void FileTableView::setNameFilter(const QString &pattern)
{
    m_proxy->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(pattern),
                           QRegularExpression::CaseInsensitiveOption));
}

QString FileTableView::dropTargetDirectory(const QPoint &position) const
{
    if (m_model == nullptr)
        return {};

    const QModelIndex index = m_view->indexAt(position);
    if (index.isValid() && index.data(IsDirectoryRole).toBool())
        return index.data(PathRole).toString();

    return m_model->currentPath();
}

bool FileTableView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_view->viewport())
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::DragEnter: {
        auto *dragEvent = static_cast<QDragEnterEvent *>(event);
        const bool acceptable = dragEvent->mimeData()->hasUrls()
                                || dragEvent->mimeData()->hasFormat(QLatin1String(kRemoteFilesMimeType));
        if (acceptable)
            dragEvent->acceptProposedAction();
        else
            dragEvent->ignore();
        return true;
    }

    case QEvent::DragMove:
        handleDragMove(static_cast<QDragMoveEvent *>(event));
        return true;

    case QEvent::DragLeave:
        m_dropRow = -1;
        m_view->viewport()->update();
        return true;

    case QEvent::Drop:
        handleDrop(static_cast<QDropEvent *>(event));
        return true;

    default:
        return QWidget::eventFilter(watched, event);
    }
}

void FileTableView::handleDragMove(QDragMoveEvent *event)
{
    const QModelIndex index = m_view->indexAt(event->position().toPoint());
    const int row = (index.isValid() && index.data(IsDirectoryRole).toBool()) ? index.row() : -1;

    if (row != m_dropRow) {
        m_dropRow = row;
        // Highlight the folder that would receive the drop.
        m_view->selectionModel()->clearCurrentIndex();
        if (row >= 0)
            m_view->setCurrentIndex(m_proxy->index(row, 0));
        m_view->viewport()->update();
    }

    event->acceptProposedAction();
}

void FileTableView::handleDrop(QDropEvent *event)
{
    const QString target = dropTargetDirectory(event->position().toPoint());
    qCDebug(lcFiles) << "drop on" << (m_model && m_model->isRemote() ? "remote" : "local")
                     << "target" << target << "formats" << event->mimeData()->formats();
    m_dropRow = -1;

    if (target.isEmpty()) {
        event->ignore();
        return;
    }

    const RemoteDragPayload remote = remotePayload(event->mimeData());
    if (remote.isValid()) {
        Q_EMIT remoteFilesDropped(remote, target);
        event->acceptProposedAction();
        return;
    }

    const QStringList paths = localPaths(event->mimeData());
    if (!paths.isEmpty()) {
        Q_EMIT localFilesDropped(paths, target);
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

// ---------------------------------------------------------------------------
// FilePane
// ---------------------------------------------------------------------------

FilePane::FilePane(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void FilePane::buildUi()
{
    setObjectName(QStringLiteral("filePane"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // -- Header ------------------------------------------------------------
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("filePaneHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 7, 10, 7);
    headerLayout->setSpacing(6);

    const auto makeButton = [header](const QString &icon, const QString &tooltip) {
        auto *button = new QToolButton(header);
        button->setIcon(ui::icon(icon));
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        button->setIconSize(QSize(16, 16));
        return button;
    };

    m_backButton = makeButton(QStringLiteral("chevron-left"), tr("Back"));
    m_forwardButton = makeButton(QStringLiteral("chevron-right"), tr("Forward"));
    m_upButton = makeButton(QStringLiteral("arrow-up"), tr("Parent folder"));

    m_titleLabel = new QLabel(header);
    m_titleLabel->setObjectName(QStringLiteral("filePaneTitle"));

    m_pathEdit = new QLineEdit(header);
    m_pathEdit->setObjectName(QStringLiteral("pathEdit"));
    m_pathEdit->setClearButtonEnabled(false);
    m_pathEdit->setPlaceholderText(tr("Path"));

    auto *refreshButton = makeButton(QStringLiteral("refresh"), tr("Refresh"));
    auto *newFolderButton = makeButton(QStringLiteral("folder-plus"), tr("New folder"));
    m_hiddenButton = makeButton(QStringLiteral("eye"), tr("Show hidden files"));
    m_hiddenButton->setCheckable(true);

    headerLayout->addWidget(m_backButton);
    headerLayout->addWidget(m_forwardButton);
    headerLayout->addWidget(m_upButton);
    headerLayout->addWidget(m_pathEdit, 1);
    headerLayout->addWidget(refreshButton);
    headerLayout->addWidget(newFolderButton);
    headerLayout->addWidget(m_hiddenButton);

    // -- Title strip -------------------------------------------------------
    auto *titleStrip = new QWidget(this);
    titleStrip->setObjectName(QStringLiteral("filePaneTitleStrip"));
    auto *titleLayout = new QHBoxLayout(titleStrip);
    titleLayout->setContentsMargins(12, 6, 10, 6);
    titleLayout->setSpacing(8);
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch(1);

    m_filterEdit = new QLineEdit(titleStrip);
    m_filterEdit->setObjectName(QStringLiteral("filterEdit"));
    m_filterEdit->setPlaceholderText(tr("Filter"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMaximumWidth(180);
    titleLayout->addWidget(m_filterEdit);

    // -- Listing -----------------------------------------------------------
    m_table = new FileTableView(this);

    m_placeholderLabel = new QLabel(this);
    m_placeholderLabel->setObjectName(QStringLiteral("filePanePlaceholder"));
    m_placeholderLabel->setAlignment(Qt::AlignCenter);
    m_placeholderLabel->setWordWrap(true);
    m_placeholderLabel->hide();

    auto *stack = new QWidget(this);
    auto *stackLayout = new QStackedLayout(stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setStackingMode(QStackedLayout::StackAll);
    stackLayout->addWidget(m_table);
    stackLayout->addWidget(m_placeholderLabel);

    // -- Status ------------------------------------------------------------
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("filePaneStatus"));
    m_statusLabel->setContentsMargins(12, 4, 12, 4);

    root->addWidget(titleStrip);
    root->addWidget(header);
    root->addWidget(stack, 1);
    root->addWidget(m_statusLabel);

    // -- Wiring ------------------------------------------------------------
    connect(m_backButton, &QToolButton::clicked, this, &FilePane::navigateBack);
    connect(m_forwardButton, &QToolButton::clicked, this, &FilePane::navigateForward);
    connect(m_upButton, &QToolButton::clicked, this, &FilePane::navigateUp);
    connect(refreshButton, &QToolButton::clicked, this, &FilePane::refresh);
    connect(newFolderButton, &QToolButton::clicked, this, &FilePane::createFolder);
    connect(m_hiddenButton, &QToolButton::toggled, this, &FilePane::toggleHiddenFiles);

    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this] {
        if (m_model != nullptr)
            m_model->setCurrentPath(m_pathEdit->text().trimmed());
    });

    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_table->setNameFilter(text);
        updateStatus();
    });

    connect(m_table, &FileTableView::activated, this, [this](const QString &path, bool isDirectory) {
        if (isDirectory && m_model != nullptr)
            m_model->setCurrentPath(path);
        else
            Q_EMIT transferRequested({path});
    });

    connect(m_table, &FileTableView::localFilesDropped, this, &FilePane::localFilesDropped);
    connect(m_table, &FileTableView::remoteFilesDropped, this, &FilePane::remoteFilesDropped);
    connect(m_table, &FileTableView::selectionChanged, this, [this] {
        updateStatus();
        Q_EMIT selectionChanged();
    });
    connect(m_table, &FileTableView::contextMenuRequested, this, &FilePane::showContextMenu);

    m_backButton->setEnabled(false);
    m_forwardButton->setEnabled(false);
}

void FilePane::setModel(FileListModel *model)
{
    m_model = model;
    m_table->setModel(model);

    connect(model, &FileListModel::pathChanged, this, &FilePane::applyPath);
    connect(model, &FileListModel::errorOccurred, this, [this](const QString &message) {
        m_statusLabel->setText(message);
        m_statusLabel->setProperty("state", QStringLiteral("error"));
        m_statusLabel->style()->polish(m_statusLabel);
    });
    connect(model, &FileListModel::loadingChanged, this, [this](bool loading) {
        if (loading)
            m_statusLabel->setText(tr("Loading…"));
        else
            updateStatus();
    });
    connect(model, &QAbstractItemModel::modelReset, this, &FilePane::updateStatus);

    m_hiddenButton->setChecked(model->showsHidden());
    applyPath(model->currentPath());
}

void FilePane::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
}

void FilePane::setPlaceholder(const QString &message)
{
    m_placeholderLabel->setText(message);
    m_placeholderLabel->show();
    m_placeholderLabel->raise();
    m_table->setEnabled(false);
}

void FilePane::clearPlaceholder()
{
    m_placeholderLabel->hide();
    m_table->setEnabled(true);
}

QString FilePane::currentDirectory() const
{
    return m_model != nullptr ? m_model->currentPath() : QString();
}

QStringList FilePane::selectedPaths() const
{
    return m_table->selectedPaths();
}

void FilePane::applyPath(const QString &path)
{
    m_pathEdit->setText(path);

    if (!m_navigatingHistory)
        pushHistory(path);

    m_upButton->setEnabled(m_model != nullptr && !m_model->parentPath().isEmpty());
    m_backButton->setEnabled(m_historyIndex > 0);
    m_forwardButton->setEnabled(m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1);

    updateStatus();
}

void FilePane::pushHistory(const QString &path)
{
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size()
        && m_history.at(m_historyIndex) == path) {
        return;
    }

    // A new destination truncates the forward history, exactly like a browser.
    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();

    m_history.append(path);
    m_historyIndex = static_cast<int>(m_history.size()) - 1;
}

void FilePane::navigateBack()
{
    if (m_historyIndex <= 0 || m_model == nullptr)
        return;

    --m_historyIndex;
    m_navigatingHistory = true;
    m_model->setCurrentPath(m_history.at(m_historyIndex));
    m_navigatingHistory = false;
}

void FilePane::navigateForward()
{
    if (m_model == nullptr || m_historyIndex < 0 || m_historyIndex >= m_history.size() - 1)
        return;

    ++m_historyIndex;
    m_navigatingHistory = true;
    m_model->setCurrentPath(m_history.at(m_historyIndex));
    m_navigatingHistory = false;
}

void FilePane::navigateUp()
{
    if (m_model == nullptr)
        return;
    const QString parent = m_model->parentPath();
    if (!parent.isEmpty())
        m_model->setCurrentPath(parent);
}

void FilePane::navigateHome()
{
    if (m_model == nullptr)
        return;
    m_model->setCurrentPath(m_model->isRemote() ? QStringLiteral("~") : QDir::homePath());
}

void FilePane::refresh()
{
    if (m_model != nullptr)
        m_model->refresh();
}

void FilePane::toggleHiddenFiles()
{
    if (m_model != nullptr)
        m_model->setShowHidden(m_hiddenButton->isChecked());
}

void FilePane::createFolder()
{
    if (m_model == nullptr)
        return;

    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, tr("untitled folder"), &accepted);
    if (!accepted || name.trimmed().isEmpty())
        return;

    m_model->createDirectory(name.trimmed());
}

void FilePane::renameSelection()
{
    if (m_model == nullptr)
        return;

    const QStringList paths = selectedPaths();
    if (paths.size() != 1)
        return;

    const QString currentName = paths.first().section(QLatin1Char('/'), -1);

    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"), QLineEdit::Normal,
                                               currentName, &accepted);
    if (!accepted || name.trimmed().isEmpty() || name == currentName)
        return;

    m_model->rename(paths.first(), name.trimmed());
}

void FilePane::deleteSelection()
{
    if (m_model == nullptr)
        return;

    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;

    const QString question = paths.size() == 1
                                 ? tr("Delete “%1”?").arg(paths.first().section(QLatin1Char('/'), -1))
                                 : tr("Delete %n selected items?", nullptr, paths.size());

    const auto answer = QMessageBox::warning(this, tr("Delete"), question,
                                             QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    m_model->remove(paths);
}

void FilePane::copyPathToClipboard()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
        QGuiApplication::clipboard()->setText(currentDirectory());
        return;
    }
    QGuiApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
}

void FilePane::showContextMenu(const QPoint &globalPosition)
{
    if (m_model == nullptr)
        return;

    const QStringList paths = selectedPaths();
    const bool hasSelection = !paths.isEmpty();

    QMenu menu(this);

    if (hasSelection) {
        const QString label = m_model->isRemote() ? tr("Download") : tr("Upload");
        menu.addAction(label, this, [this, paths] { Q_EMIT transferRequested(paths); });
        menu.addSeparator();
    }

    menu.addAction(tr("New Folder…"), this, &FilePane::createFolder);

    QAction *renameAction = menu.addAction(tr("Rename…"), this, &FilePane::renameSelection);
    renameAction->setEnabled(paths.size() == 1);

    QAction *deleteAction = menu.addAction(tr("Delete…"), this, &FilePane::deleteSelection);
    deleteAction->setEnabled(hasSelection);

    menu.addSeparator();
    menu.addAction(tr("Copy Path"), this, &FilePane::copyPathToClipboard);
    menu.addAction(tr("Refresh"), this, &FilePane::refresh);

    menu.exec(globalPosition);
}

void FilePane::updateStatus()
{
    if (m_model == nullptr)
        return;

    m_statusLabel->setProperty("state", QString());
    m_statusLabel->style()->polish(m_statusLabel);

    const int shown = m_table->proxy()->rowCount();
    const QStringList selected = selectedPaths();

    if (selected.isEmpty()) {
        m_statusLabel->setText(tr("%n item(s)", nullptr, shown));
        return;
    }

    quint64 totalBytes = 0;
    const QModelIndexList rows = m_table->view()->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows)
        totalBytes += index.data(SizeRole).toULongLong();

    // QStringLiteral, not QLatin1String: the separator is non-ASCII and
    // Latin-1 would mangle its UTF-8 bytes.
    m_statusLabel->setText(tr("%n selected", nullptr, selected.size()) + QStringLiteral(" · ")
                           + formatFileSize(totalBytes));
}

} // namespace arterm::files
