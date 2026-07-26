#include "files/remote_file_model.hpp"

#include "files/file_mime_data.hpp"
#include "session/remote_file_service.hpp"
#include "ui/icons.hpp"

#include <QMimeData>

namespace arterm::files
{
	namespace
	{

		QString join_remote( QString const& directory, QString const& name ){
			if( directory.isEmpty() || directory == QLatin1String( "/" ) )
				return QLatin1Char( '/' ) + name;
			if( directory.endsWith( QLatin1Char( '/' ) ) )
				return directory + name;
			return directory + QLatin1Char( '/' ) + name;
		}

	} // namespace

	RemoteFileModel::RemoteFileModel( session::RemoteFileService* service, QString session_id, QObject* parent )
		: FileListModel( parent )
		, _service( service )
		, _session_id( std::move( session_id ) )
	{
		connect( _service, &session::RemoteFileService::listing_ready, this,
				 [this]( quint64 request_id, QString const& path, ssh::RemoteListing const& entries ){
					 if( request_id != _pending_listing_id )
						 return; // A listing for a directory the user already left.
					 _pending_listing_id = 0;
					 apply_listing( path, entries );
				 } );

		connect( _service, &session::RemoteFileService::operation_failed, this,
				 [this]( quint64 request_id, Error const& error ){
					 if( request_id == _pending_listing_id ){
						 _pending_listing_id = 0;
						 set_loading( false );
					 }
					 if( !error.is_cancellation() )
						 Q_EMIT error_occurred( error.message );
				 } );

		connect( _service, &session::RemoteFileService::operation_finished, this, [this]( quint64 ){
			// Mutations do not report what changed, so simply re-read the directory.
			refresh();
			Q_EMIT content_changed();
		} );
	}

	int RemoteFileModel::rowCount( QModelIndex const& parent ) const{
		return parent.isValid() ? 0 : static_cast<int>( _entries.size() );
	}

	int RemoteFileModel::columnCount( QModelIndex const& parent ) const{
		return parent.isValid() ? 0 : static_cast<int>( FileColumn::COUNT );
	}

	QVariant RemoteFileModel::data( QModelIndex const& index, int role ) const{
		if( !index.isValid() || index.row() >= _entries.size() )
			return {};

		ssh::RemoteFileEntry const& entry = _entries.at( index.row() );

		switch( role ){
			case Qt::DisplayRole:
				switch( static_cast<FileColumn>( index.column() ) ){
					case FileColumn::NAME:
						return entry.name;
					case FileColumn::SIZE:
						return entry.is_directory ? QString() : format_file_size( entry.size );
					case FileColumn::MODIFIED:
						return format_timestamp( entry.modified );
					case FileColumn::PERMISSIONS:
						return entry.permission_string();
					case FileColumn::COUNT:
						break;
				}
				return {};

			case Qt::DecorationRole:
				// Only the Name column carries an icon; a glyph in every cell would be
				// noise.
				if( static_cast<FileColumn>( index.column() ) != FileColumn::NAME )
					return {};
				return ui::file_icon( entry.name, entry.is_directory, entry.is_symlink );

			case Qt::ToolTipRole:
				return entry.path;

			case Qt::TextAlignmentRole:
				if( static_cast<FileColumn>( index.column() ) == FileColumn::SIZE )
					return QVariant( Qt::AlignRight | Qt::AlignVCenter );
				return QVariant( Qt::AlignLeft | Qt::AlignVCenter );

			case PATH_ROLE:
				return entry.path;
			case NAME_ROLE:
				return entry.name;
			case IS_DIRECTORY_ROLE:
				return entry.is_directory;
			case IS_SYMLINK_ROLE:
				return entry.is_symlink;
			case IS_EXECUTABLE_ROLE:
				return entry.is_executable && !entry.is_directory;
			case SIZE_ROLE:
				return static_cast<qulonglong>( entry.size );
			case MODIFIED_ROLE:
				return entry.modified;

			default:
				return {};
		}
	}

	QVariant RemoteFileModel::headerData( int section, Qt::Orientation orientation, int role ) const{
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
				return tr( "Mode" );
			case FileColumn::COUNT:
				break;
		}
		return {};
	}

	Qt::ItemFlags RemoteFileModel::flags( QModelIndex const& index ) const{
		Qt::ItemFlags base = QAbstractTableModel::flags( index );
		if( !_connected )
			return base & ~Qt::ItemIsEnabled;
		if( !index.isValid() )
			return base | Qt::ItemIsDropEnabled;
		return base | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
	}

	QStringList RemoteFileModel::mimeTypes() const{
		return { QLatin1String( REMOTE_FILES_MIME_TYPE ), QLatin1String( URI_LIST_MIME_TYPE ) };
	}

	Qt::DropActions RemoteFileModel::supportedDragActions() const{
		return Qt::CopyAction | Qt::MoveAction;
	}

	QMimeData* RemoteFileModel::mimeData( QModelIndexList const& indexes ) const{
		RemoteDragPayload payload;
		payload.session_id = _session_id;

		for( QModelIndex const& index : indexes ){
			if( index.column() != 0 )
				continue;
			QString const path = index.data( PATH_ROLE ).toString();
			if( !path.isEmpty() )
				payload.paths << path;
		}

		if( !payload.isValid() )
			return nullptr;

		auto* mime = new QMimeData;
		set_remote_payload( mime, payload );
		return mime;
	}

	QString RemoteFileModel::parent_path() const{
		if( _path.isEmpty() || _path == QLatin1String( "/" ) )
			return {};

		QString trimmed = _path;
		while( trimmed.endsWith( QLatin1Char( '/' ) ) )
			trimmed.chop( 1 );

		int const slash = trimmed.lastIndexOf( QLatin1Char( '/' ) );
		if( slash <= 0 )
			return QStringLiteral( "/" );
		return trimmed.left( slash );
	}

	QString RemoteFileModel::child_path( QString const& name ) const{
		return join_remote( _path, name );
	}

	void RemoteFileModel::set_connected( bool connected, QString const& home_directory ){
		_connected = connected;

		if( !connected ){
			beginResetModel();
			_all_entries.clear();
			_entries.clear();
			endResetModel();
			return;
		}

		set_current_path( home_directory.isEmpty() ? QStringLiteral( "/" ) : home_directory );
	}

	void RemoteFileModel::set_current_path( QString const& path ){
		if( !_connected ){
			Q_EMIT error_occurred( tr( "Not connected" ) );
			return;
		}

		_pending_path = path.isEmpty() ? QStringLiteral( "/" ) : path;
		set_loading( true );
		_pending_listing_id = _service->list_directory( _pending_path );
	}

	void RemoteFileModel::refresh(){
		if( !_connected || _path.isEmpty() )
			return;
		_pending_path = _path;
		set_loading( true );
		_pending_listing_id = _service->list_directory( _path );
	}

	void RemoteFileModel::apply_listing( QString const& path, ssh::RemoteListing const& entries ){
		bool const path_changed = ( path != _path );

		_path        = path;
		_all_entries = entries;
		rebuild_visible();

		set_loading( false );

		if( path_changed )
			Q_EMIT FileListModel::path_changed( _path );
	}

	void RemoteFileModel::rebuild_visible(){
		beginResetModel();

		_entries.clear();
		_entries.reserve( _all_entries.size() );
		for( ssh::RemoteFileEntry const& entry : _all_entries ){
			if( !_show_hidden && entry.name.startsWith( QLatin1Char( '.' ) ) )
				continue;
			_entries.append( entry );
		}

		endResetModel();
	}

	void RemoteFileModel::set_show_hidden( bool show ){
		if( _show_hidden == show )
			return;
		_show_hidden = show;
		rebuild_visible();
	}

	void RemoteFileModel::create_directory( QString const& name ){
		if( name.isEmpty() || !_connected )
			return;
		_service->make_directory( child_path( name ) );
	}

	void RemoteFileModel::rename( QString const& path, QString const& new_name ){
		if( new_name.isEmpty() || !_connected )
			return;

		int const     slash     = path.lastIndexOf( QLatin1Char( '/' ) );
		QString const directory = slash > 0 ? path.left( slash ) : QStringLiteral( "/" );
		_service->rename_entry( path, join_remote( directory, new_name ) );
	}

	void RemoteFileModel::remove( QStringList const& paths ){
		if( !_connected )
			return;
		for( QString const& path : paths )
			_service->remove_entry( path, /*recursive=*/true );
	}

} // namespace arterm::files
