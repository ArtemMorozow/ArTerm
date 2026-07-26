#pragma once

#include "files/file_mime_data.hpp"

#include <QWidget>

class QSplitter;

namespace arterm::session
{
	class RemoteFileService;
}

namespace arterm::files
{

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
	class FileBrowser : public QWidget
	{
		Q_OBJECT

	public:
		FileBrowser( session::RemoteFileService* service, QString session_id, QWidget* parent = nullptr );

		/// Called once the SFTP connection is up.
		void set_connected( bool connected, QString const& home_directory );

		/// Shown in the remote pane's title, e.g. "root@build-01".
		void set_remote_title( QString const& title );

		[[nodiscard]] FilePane*        local_pane() const noexcept { return _local_pane; }
		[[nodiscard]] FilePane*        remote_pane() const noexcept { return _remote_pane; }
		[[nodiscard]] TransferManager* transfers() const noexcept { return _transfers; }

		/// Queue an upload of `paths` into the directory the remote pane is showing.
		/// Used by the terminal's own drop handler.
		void upload_to_current_remote_directory( QStringList const& paths );

	Q_SIGNALS:
		void status_message( QString const& message );

	private:
		void build_ui();
		void connect_panes();

		void upload_into( QStringList const& local_paths, QString const& remote_directory );
		void download_into( QStringList const& remote_paths, QString const& local_directory );
		void move_remote( QStringList const& remote_paths, QString const& remote_directory );

		session::RemoteFileService* _service;
		QString                     _session_id;

		FilePane*        _local_pane{ nullptr };
		FilePane*        _remote_pane{ nullptr };
		LocalFileModel*  _local_model{ nullptr };
		RemoteFileModel* _remote_model{ nullptr };
		TransferManager* _transfers{ nullptr };
		TransferPanel*   _transfer_panel{ nullptr };
		QSplitter*       _splitter{ nullptr };          ///< Local | remote panes.
		QSplitter*       _vertical_splitter{ nullptr }; ///< Panes | transfer panel.
	};

} // namespace arterm::files
