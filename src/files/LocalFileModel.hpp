#pragma once

#include "files/FileListModel.hpp"

#include <QFileInfo>
#include <QVector>

class QFileSystemWatcher;

namespace arterm::files {

/// Lists one local directory.
///
/// `QFileSystemModel` is deliberately not used: it is a tree model with its own
/// asynchronous population, and matching its columns and drag payload to the
/// remote pane turned out to be more work than listing a single directory.
class LocalFileModel : public FileListModel {
    Q_OBJECT

public:
    explicit LocalFileModel(QObject *parent = nullptr);

    // QAbstractTableModel.
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

    // Drag and drop.
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData *mimeData(const QModelIndexList &indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

    // FileListModel.
    [[nodiscard]] QString currentPath() const override { return m_path; }
    void setCurrentPath(const QString &path) override;
    void refresh() override;
    [[nodiscard]] bool isRemote() const override { return false; }
    [[nodiscard]] QString parentPath() const override;
    [[nodiscard]] QString childPath(const QString &name) const override;
    void setShowHidden(bool show) override;

    void createDirectory(const QString &name) override;
    void rename(const QString &path, const QString &newName) override;
    void remove(const QStringList &paths) override;

private:
    void reload();

    QString m_path;
    QVector<QFileInfo> m_entries;
    QFileSystemWatcher *m_watcher{nullptr};
};

} // namespace arterm::files
