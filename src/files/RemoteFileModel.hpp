#pragma once

#include "files/FileListModel.hpp"
#include "ssh/SshTypes.hpp"

#include <QHash>

namespace arterm::session {
class RemoteFileService;
}

namespace arterm::files {

/// Lists one remote directory over SFTP.
///
/// All work goes through `RemoteFileService`, which owns the worker thread; the
/// model itself only ever touches data on the GUI thread.
class RemoteFileModel : public FileListModel {
    Q_OBJECT

public:
    RemoteFileModel(session::RemoteFileService *service, QString sessionId, QObject *parent = nullptr);

    // QAbstractTableModel.
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &index) const override;

    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData *mimeData(const QModelIndexList &indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

    // FileListModel.
    [[nodiscard]] QString currentPath() const override { return m_path; }
    void setCurrentPath(const QString &path) override;
    void refresh() override;
    [[nodiscard]] bool isRemote() const override { return true; }
    [[nodiscard]] QString parentPath() const override;
    [[nodiscard]] QString childPath(const QString &name) const override;
    void setShowHidden(bool show) override;

    void createDirectory(const QString &name) override;
    void rename(const QString &path, const QString &newName) override;
    void remove(const QStringList &paths) override;

    /// Set once the SFTP session is up; until then the model is empty and the
    /// pane shows a connecting placeholder.
    void setConnected(bool connected, const QString &homeDirectory);
    [[nodiscard]] bool isConnected() const noexcept { return m_connected; }

private:
    void applyListing(const QString &path, const ssh::RemoteListing &entries);
    void rebuildVisible();

    session::RemoteFileService *m_service;
    QString m_sessionId;
    QString m_path;

    ssh::RemoteListing m_allEntries; ///< Everything the server returned.
    ssh::RemoteListing m_entries;    ///< Filtered by `m_showHidden`.

    /// Directory a pending listing was requested for, so a stale reply arriving
    /// after the user navigated away can be discarded.
    QString m_pendingPath;
    quint64 m_pendingListingId{0};
    bool m_connected{false};
};

} // namespace arterm::files
