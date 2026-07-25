#pragma once

#include "files/FileMimeData.hpp"

#include <QWidget>

class QSplitter;

namespace arterm::session {
class RemoteFileService;
}

namespace arterm::files {

class FilePane;
class LocalFileModel;
class RemoteFileModel;
class TransferManager;
class TransferPanel;

/// The two-pane file manager: local on the left, remote on the right, with the
/// transfer queue underneath.
///
/// This class owns the drag-and-drop policy. A drop is interpreted by comparing
/// where the data came from with where it landed:
///
///   local  -> remote  : upload
///   remote -> local   : download
///   remote -> remote  : server-side rename when both sides are this session
///   local  -> local   : a plain local copy is out of scope and is rejected
class FileBrowser : public QWidget {
    Q_OBJECT

public:
    FileBrowser(session::RemoteFileService *service, QString sessionId, QWidget *parent = nullptr);

    /// Called once the SFTP connection is up.
    void setConnected(bool connected, const QString &homeDirectory);

    /// Shown in the remote pane's title, e.g. "root@build-01".
    void setRemoteTitle(const QString &title);

    [[nodiscard]] FilePane *localPane() const noexcept { return m_localPane; }
    [[nodiscard]] FilePane *remotePane() const noexcept { return m_remotePane; }
    [[nodiscard]] TransferManager *transfers() const noexcept { return m_transfers; }

    /// Queue an upload of `paths` into the directory the remote pane is showing.
    /// Used by the terminal's own drop handler.
    void uploadToCurrentRemoteDirectory(const QStringList &paths);

Q_SIGNALS:
    void statusMessage(const QString &message);

private:
    void buildUi();
    void connectPanes();

    void uploadInto(const QStringList &localPaths, const QString &remoteDirectory);
    void downloadInto(const QStringList &remotePaths, const QString &localDirectory);
    void moveRemote(const QStringList &remotePaths, const QString &remoteDirectory);

    session::RemoteFileService *m_service;
    QString m_sessionId;

    FilePane *m_localPane{nullptr};
    FilePane *m_remotePane{nullptr};
    LocalFileModel *m_localModel{nullptr};
    RemoteFileModel *m_remoteModel{nullptr};
    TransferManager *m_transfers{nullptr};
    TransferPanel *m_transferPanel{nullptr};
    QSplitter *m_splitter{nullptr};         ///< Local | remote panes.
    QSplitter *m_verticalSplitter{nullptr}; ///< Panes | transfer panel.
};

} // namespace arterm::files
