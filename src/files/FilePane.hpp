#pragma once

#include "files/FileMimeData.hpp"

#include <QWidget>

class QLineEdit;
class QSortFilterProxyModel;
class QTableView;
class QToolButton;
class QLabel;

namespace arterm::files {

class FileListModel;

/// A table view that handles the drag and drop wiring the panes need.
///
/// Qt's model-level drop handling is bypassed: a drop here does not insert a
/// row, it starts a transfer, and the target directory depends on whether the
/// pointer was over a folder or over empty space.
class FileTableView : public QWidget {
    Q_OBJECT

public:
    explicit FileTableView(QWidget *parent = nullptr);

    void setModel(FileListModel *model);
    [[nodiscard]] QTableView *view() const noexcept { return m_view; }
    [[nodiscard]] QSortFilterProxyModel *proxy() const noexcept { return m_proxy; }

    /// Absolute paths of the selected rows.
    [[nodiscard]] QStringList selectedPaths() const;
    [[nodiscard]] bool hasSelection() const;

    void setNameFilter(const QString &pattern);

Q_SIGNALS:
    /// Local files were dropped; `targetDirectory` is where they belong.
    void localFilesDropped(const QStringList &paths, const QString &targetDirectory);
    /// Remote files were dropped, carrying the originating session id.
    void remoteFilesDropped(const RemoteDragPayload &payload, const QString &targetDirectory);
    void activated(const QString &path, bool isDirectory);
    void selectionChanged();
    void contextMenuRequested(const QPoint &globalPosition);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// Directory a drop at `position` should land in: the folder under the
    /// pointer, or the directory being listed when the pointer is over a file
    /// or over empty space.
    [[nodiscard]] QString dropTargetDirectory(const QPoint &position) const;
    void handleDragMove(class QDragMoveEvent *event);
    void handleDrop(class QDropEvent *event);

    QTableView *m_view{nullptr};
    QSortFilterProxyModel *m_proxy{nullptr};
    FileListModel *m_model{nullptr};

    /// Row currently highlighted as a drop target, or -1.
    int m_dropRow{-1};
};

/// One side of the file browser: toolbar, path bar, listing and status line.
class FilePane : public QWidget {
    Q_OBJECT

public:
    explicit FilePane(QWidget *parent = nullptr);

    void setModel(FileListModel *model);
    [[nodiscard]] FileListModel *model() const noexcept { return m_model; }
    [[nodiscard]] FileTableView *table() const noexcept { return m_table; }

    /// Shown in the pane header, e.g. "This Mac" or "user@host".
    void setTitle(const QString &title);

    /// Replaces the listing with a message, used while connecting or after a
    /// failure.
    void setPlaceholder(const QString &message);
    void clearPlaceholder();

    [[nodiscard]] QString currentDirectory() const;
    [[nodiscard]] QStringList selectedPaths() const;

Q_SIGNALS:
    /// The user asked to send the selection to the other pane.
    void transferRequested(const QStringList &paths);
    void localFilesDropped(const QStringList &paths, const QString &targetDirectory);
    void remoteFilesDropped(const RemoteDragPayload &payload, const QString &targetDirectory);
    void selectionChanged();

public Q_SLOTS:
    void navigateUp();
    void navigateBack();
    void navigateForward();
    void navigateHome();
    void refresh();
    void createFolder();
    void renameSelection();
    void deleteSelection();
    void copyPathToClipboard();
    void toggleHiddenFiles();

private:
    void buildUi();
    void applyPath(const QString &path);
    void pushHistory(const QString &path);
    void updateStatus();
    void showContextMenu(const QPoint &globalPosition);

    FileListModel *m_model{nullptr};

    QLabel *m_titleLabel{nullptr};
    QLineEdit *m_pathEdit{nullptr};
    QLineEdit *m_filterEdit{nullptr};
    QToolButton *m_backButton{nullptr};
    QToolButton *m_forwardButton{nullptr};
    QToolButton *m_upButton{nullptr};
    QToolButton *m_hiddenButton{nullptr};
    FileTableView *m_table{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_placeholderLabel{nullptr};

    QStringList m_history;
    int m_historyIndex{-1};
    /// Set while navigating through history so the visit is not re-recorded.
    bool m_navigatingHistory{false};
};

} // namespace arterm::files
