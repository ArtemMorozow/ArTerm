#pragma once

#include "files/file_list_model.hpp"

#include <QFileInfo>
#include <QVector>

class QFileSystemWatcher;

namespace arterm::files
{

	/// Lists one local directory.
	///
	/// `QFileSystemModel` is deliberately not used: it is a tree model with its own
	/// asynchronous population, and matching its columns and drag payload to the
	/// remote pane turned out to be more work than listing a single directory.
	class LocalFileModel : public FileListModel
	{
		Q_OBJECT

	public:
		explicit LocalFileModel( QObject* parent = nullptr );

		// QAbstractTableModel.
		[[nodiscard]] int           rowCount( QModelIndex const& parent = {} ) const override;
		[[nodiscard]] int           columnCount( QModelIndex const& parent = {} ) const override;
		[[nodiscard]] QVariant      data( QModelIndex const& index, int role ) const override;
		[[nodiscard]] QVariant      headerData( int section, Qt::Orientation orientation, int role ) const override;
		[[nodiscard]] Qt::ItemFlags flags( QModelIndex const& index ) const override;

		// Drag and drop.
		[[nodiscard]] QStringList     mimeTypes() const override;
		[[nodiscard]] QMimeData*      mimeData( QModelIndexList const& indexes ) const override;
		[[nodiscard]] Qt::DropActions supportedDragActions() const override;

		// FileListModel.
		[[nodiscard]] QString current_path() const override { return _path; }
		void                  set_current_path( QString const& path ) override;
		void                  refresh() override;
		[[nodiscard]] bool    is_remote() const override { return false; }
		[[nodiscard]] QString parent_path() const override;
		[[nodiscard]] QString child_path( QString const& name ) const override;
		void                  set_show_hidden( bool show ) override;

		void create_directory( QString const& name ) override;
		void rename( QString const& path, QString const& new_name ) override;
		void remove( QStringList const& paths ) override;

	private:
		void reload();

		QString             _path;
		QVector<QFileInfo>  _entries;
		QFileSystemWatcher* _watcher{ nullptr };
	};

} // namespace arterm::files
