#pragma once

#include "files/file_list_model.hpp"
#include "ssh/ssh_types.hpp"

#include <QHash>

namespace arterm::session
{
	class RemoteFileService;
}

namespace arterm::files
{

	/// Lists one remote directory over SFTP.
	///
	/// All work goes through `RemoteFileService`, which owns the worker thread; the
	/// model itself only ever touches data on the GUI thread.
	class RemoteFileModel : public FileListModel
	{
		Q_OBJECT

	public:
		RemoteFileModel( session::RemoteFileService* service, QString session_id, QObject* parent = nullptr );

		// QAbstractTableModel.
		[[nodiscard]] int           rowCount( QModelIndex const& parent = {} ) const override;
		[[nodiscard]] int           columnCount( QModelIndex const& parent = {} ) const override;
		[[nodiscard]] QVariant      data( QModelIndex const& index, int role ) const override;
		[[nodiscard]] QVariant      headerData( int section, Qt::Orientation orientation, int role ) const override;
		[[nodiscard]] Qt::ItemFlags flags( QModelIndex const& index ) const override;

		[[nodiscard]] QStringList     mimeTypes() const override;
		[[nodiscard]] QMimeData*      mimeData( QModelIndexList const& indexes ) const override;
		[[nodiscard]] Qt::DropActions supportedDragActions() const override;

		// FileListModel.
		[[nodiscard]] QString current_path() const override { return _path; }
		void                  set_current_path( QString const& path ) override;
		void                  refresh() override;
		[[nodiscard]] bool    is_remote() const override { return true; }
		[[nodiscard]] QString parent_path() const override;
		[[nodiscard]] QString child_path( QString const& name ) const override;
		void                  set_show_hidden( bool show ) override;

		void create_directory( QString const& name ) override;
		void rename( QString const& path, QString const& new_name ) override;
		void remove( QStringList const& paths ) override;

		/// Set once the SFTP session is up; until then the model is empty and the
		/// pane shows a connecting placeholder.
		void               set_connected( bool connected, QString const& home_directory );
		[[nodiscard]] bool is_connected() const noexcept { return _connected; }

	private:
		void apply_listing( QString const& path, ssh::RemoteListing const& entries );
		void rebuild_visible();

		session::RemoteFileService* _service;
		QString                     _session_id;
		QString                     _path;

		ssh::RemoteListing _all_entries; ///< Everything the server returned.
		ssh::RemoteListing _entries;     ///< Filtered by `_show_hidden`.

		/// Directory a pending listing was requested for, so a stale reply arriving
		/// after the user navigated away can be discarded.
		QString _pending_path;
		quint64 _pending_listing_id{ 0 };
		bool    _connected{ false };
	};

} // namespace arterm::files
