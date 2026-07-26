#include "files/local_file_model.hpp"

#include "files/file_mime_data.hpp"
#include "ui/icons.hpp"

#include <QDir>
#include <QFileSystemWatcher>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace arterm::files
{

	LocalFileModel::LocalFileModel( QObject* parent )
		: FileListModel( parent )
		, _watcher( new QFileSystemWatcher( this ) )
	{
		_path = QDir::homePath();

		// Coalesce bursts of filesystem notifications: an unpacking archive would
		// otherwise trigger hundreds of reloads a second.
		auto* debounce = new QTimer( this );
		debounce->setSingleShot( true );
		debounce->setInterval( 200 );
		connect( debounce, &QTimer::timeout, this, &LocalFileModel::reload );
		connect( _watcher, &QFileSystemWatcher::directoryChanged, this,
				 [debounce]( QString const& ) { debounce->start(); } );

		reload();
	}

	int LocalFileModel::rowCount( QModelIndex const& parent ) const{
		return parent.isValid() ? 0 : static_cast<int>( _entries.size() );
	}

	int LocalFileModel::columnCount( QModelIndex const& parent ) const{
		return parent.isValid() ? 0 : static_cast<int>( FileColumn::COUNT );
	}

	QVariant LocalFileModel::data( QModelIndex const& index, int role ) const{
		if( !index.isValid() || index.row() >= _entries.size() )
			return {};

		QFileInfo const& info = _entries.at( index.row() );

		switch( role ){
			case Qt::DisplayRole:
				switch( static_cast<FileColumn>( index.column() ) ){
					case FileColumn::NAME:
						return info.fileName();
					case FileColumn::SIZE:
						return info.isDir() ? QString() : format_file_size( static_cast<quint64>( info.size() ) );
					case FileColumn::MODIFIED:
						return format_timestamp( info.lastModified() );
					case FileColumn::PERMISSIONS:
						return QStringLiteral( "%1%2%3%4" )
							.arg( info.isDir() ? QLatin1String( "d" ) : QLatin1String( "-" ),
								  info.isReadable() ? QLatin1String( "r" ) : QLatin1String( "-" ),
								  info.isWritable() ? QLatin1String( "w" ) : QLatin1String( "-" ),
								  info.isExecutable() ? QLatin1String( "x" ) : QLatin1String( "-" ) );
					case FileColumn::COUNT:
						break;
				}
				return {};

			case Qt::DecorationRole:
				// Only the Name column carries an icon; a glyph in every cell would be
				// noise.
				if( static_cast<FileColumn>( index.column() ) != FileColumn::NAME )
					return {};
				return ui::file_icon( info.fileName(), info.isDir(), info.isSymLink() );

			case Qt::ToolTipRole:
				return info.absoluteFilePath();

			case Qt::TextAlignmentRole:
				if( static_cast<FileColumn>( index.column() ) == FileColumn::SIZE )
					return QVariant( Qt::AlignRight | Qt::AlignVCenter );
				return QVariant( Qt::AlignLeft | Qt::AlignVCenter );

			case PATH_ROLE:
				return info.absoluteFilePath();
			case NAME_ROLE:
				return info.fileName();
			case IS_DIRECTORY_ROLE:
				return info.isDir();
			case IS_SYMLINK_ROLE:
				return info.isSymLink();
			case IS_EXECUTABLE_ROLE:
				return info.isExecutable() && !info.isDir();
			case SIZE_ROLE:
				return static_cast<qulonglong>( info.size() );
			case MODIFIED_ROLE:
				return info.lastModified();

			default:
				return {};
		}
	}

	QVariant LocalFileModel::headerData( int section, Qt::Orientation orientation, int role ) const{
		if( orientation != Qt::Horizontal || role != Qt::DisplayRole )
			return {};

		switch( static_cast<FileColumn>( section ) ){
			case FileColumn::NAME:
				return tr( "Name" );
			case FileColumn::SIZE:
				return tr( "Size" );
			case FileColumn::MODIFIED:
				return tr( "Modified" );
			case FileColumn::PERMISSIONS:
				return tr( "Access" );
			case FileColumn::COUNT:
				break;
		}
		return {};
	}

	Qt::ItemFlags LocalFileModel::flags( QModelIndex const& index ) const{
		Qt::ItemFlags base = QAbstractTableModel::flags( index );
		if( !index.isValid() )
			return base | Qt::ItemIsDropEnabled;
		return base | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
	}

	QStringList LocalFileModel::mimeTypes() const{
		return { QLatin1String( URI_LIST_MIME_TYPE ), QLatin1String( REMOTE_FILES_MIME_TYPE ) };
	}

	Qt::DropActions LocalFileModel::supportedDragActions() const{
		return Qt::CopyAction | Qt::MoveAction;
	}

	QMimeData* LocalFileModel::mimeData( QModelIndexList const& indexes ) const{
		QList<QUrl> urls;

		for( QModelIndex const& index : indexes ){
			if( index.column() != 0 )
				continue;
			QString const path = index.data( PATH_ROLE ).toString();
			if( !path.isEmpty() )
				urls << QUrl::fromLocalFile( path );
		}

		if( urls.isEmpty() )
			return nullptr;

		// Plain file URLs: this is what Finder and every other application expects,
		// so a drag out of ArTerm behaves like a drag out of a Finder window.
		auto* mime = new QMimeData;
		mime->setUrls( urls );
		return mime;
	}

	QString LocalFileModel::parent_path() const{
		QDir directory( _path );
		if( directory.isRoot() )
			return {};
		directory.cdUp();
		return directory.absolutePath();
	}

	QString LocalFileModel::child_path( QString const& name ) const{
		return QDir( _path ).absoluteFilePath( name );
	}

	void LocalFileModel::set_current_path( QString const& path ){
		QFileInfo const info( path );
		if( !info.isDir() ){
			Q_EMIT error_occurred( tr( "%1 is not a directory" ).arg( path ) );
			return;
		}
		if( !info.isReadable() ){
			Q_EMIT error_occurred( tr( "Cannot read %1: permission denied" ).arg( path ) );
			return;
		}

		_path = info.absoluteFilePath();
		reload();
		Q_EMIT path_changed( _path );
	}

	void LocalFileModel::refresh(){
		reload();
	}

	void LocalFileModel::set_show_hidden( bool show ){
		if( _show_hidden == show )
			return;
		_show_hidden = show;
		reload();
	}

	void LocalFileModel::reload(){
		set_loading( true );

		QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
		if( _show_hidden )
			filters |= QDir::Hidden;

		QDir directory( _path );
		auto entries = directory.entryInfoList( filters, QDir::NoSort );

		// Directories first, then case-insensitive by name, matching the remote pane.
		std::sort( entries.begin(), entries.end(), []( QFileInfo const& a, QFileInfo const& b ){
			if( a.isDir() != b.isDir() )
				return a.isDir();
			return a.fileName().compare( b.fileName(), Qt::CaseInsensitive ) < 0;
		} );

		beginResetModel();
		_entries = QVector<QFileInfo>( entries.begin(), entries.end() );
		endResetModel();

		if( !_watcher->directories().isEmpty() )
			_watcher->removePaths( _watcher->directories() );
		_watcher->addPath( _path );

		set_loading( false );
	}

	void LocalFileModel::create_directory( QString const& name ){
		if( name.isEmpty() )
			return;

		if( !QDir( _path ).mkdir( name ) ){
			Q_EMIT error_occurred( tr( "Cannot create the folder %1" ).arg( name ) );
			return;
		}

		reload();
		Q_EMIT content_changed();
	}

	void LocalFileModel::rename( QString const& path, QString const& new_name ){
		QFileInfo const info( path );
		QString const   target = info.absoluteDir().absoluteFilePath( new_name );

		if( QFileInfo::exists( target ) ){
			Q_EMIT error_occurred( tr( "%1 already exists" ).arg( new_name ) );
			return;
		}

		if( !QFile::rename( path, target ) ){
			Q_EMIT error_occurred( tr( "Cannot rename %1" ).arg( info.fileName() ) );
			return;
		}

		reload();
		Q_EMIT content_changed();
	}

	void LocalFileModel::remove( QStringList const& paths ){
		QStringList failures;

		for( QString const& path : paths ){
			QFileInfo const info( path );
			bool const      removed =
                info.isDir() && !info.isSymLink() ? QDir( path ).removeRecursively() : QFile::remove( path );
			if( !removed )
				failures << info.fileName();
		}

		if( !failures.isEmpty() )
			Q_EMIT error_occurred( tr( "Cannot delete: %1" ).arg( failures.join( QLatin1String( ", " ) ) ) );

		reload();
		Q_EMIT content_changed();
	}

} // namespace arterm::files
