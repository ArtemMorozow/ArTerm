#include "files/file_browser.hpp"

#include "files/file_pane.hpp"
#include "files/local_file_model.hpp"
#include "files/remote_file_model.hpp"
#include "files/transfer_manager.hpp"
#include "files/transfer_panel.hpp"
#include "session/remote_file_service.hpp"

#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>

namespace arterm::files
{
	namespace
	{

		QString remote_base_name( QString const& path ){
			QString trimmed = path;
			while( trimmed.size() > 1 && trimmed.endsWith( QLatin1Char( '/' ) ) )
				trimmed.chop( 1 );
			int const slash = trimmed.lastIndexOf( QLatin1Char( '/' ) );
			return slash >= 0 ? trimmed.mid( slash + 1 ) : trimmed;
		}

		QString join_remote( QString const& directory, QString const& name ){
			if( directory.isEmpty() || directory == QLatin1String( "/" ) )
				return QLatin1Char( '/' ) + name;
			if( directory.endsWith( QLatin1Char( '/' ) ) )
				return directory + name;
			return directory + QLatin1Char( '/' ) + name;
		}

	} // namespace

	FileBrowser::FileBrowser( session::RemoteFileService* service, QString session_id, QWidget* parent )
		: QWidget( parent )
		, _service( service )
		, _session_id( std::move( session_id ) )
	{
		build_ui();
		connect_panes();
	}

	void FileBrowser::build_ui(){
		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 0, 0, 0, 0 );
		root->setSpacing( 0 );

		_local_model  = new LocalFileModel( this );
		_remote_model = new RemoteFileModel( _service, _session_id, this );
		_transfers    = new TransferManager( _service, this );

		_local_pane = new FilePane( this );
		_local_pane->set_model( _local_model );
		_local_pane->set_title( tr( "This Mac" ) );

		_remote_pane = new FilePane( this );
		_remote_pane->set_model( _remote_model );
		_remote_pane->set_title( tr( "Remote" ) );
		_remote_pane->set_placeholder( tr( "Connecting…" ) );

		_splitter = new QSplitter( Qt::Horizontal, this );
		_splitter->addWidget( _local_pane );
		_splitter->addWidget( _remote_pane );
		_splitter->setStretchFactor( 0, 1 );
		_splitter->setStretchFactor( 1, 1 );
		_splitter->setChildrenCollapsible( false );
		_splitter->setHandleWidth( 1 );

		_transfer_panel = new TransferPanel( _transfers, this );

		// The panel shares a splitter with the panes instead of pinning its own
		// height, so expanding it steals space the user can drag back.
		_vertical_splitter = new QSplitter( Qt::Vertical, this );
		_vertical_splitter->addWidget( _splitter );
		_vertical_splitter->addWidget( _transfer_panel );
		_vertical_splitter->setStretchFactor( 0, 1 );
		_vertical_splitter->setStretchFactor( 1, 0 );
		_vertical_splitter->setChildrenCollapsible( false );
		_vertical_splitter->setHandleWidth( 1 );

		connect( _transfer_panel, &TransferPanel::expanded_changed, this, [this]( bool expanded ){
			int const total        = _vertical_splitter->height();
			int const panel_height = expanded ? std::min( 150, total / 3 ) : TransferPanel::header_height();
			_vertical_splitter->setSizes( { total - panel_height, panel_height } );
		} );

		root->addWidget( _vertical_splitter, 1 );
	}

	void FileBrowser::connect_panes(){
		// -- Explicit transfer requests (double click, context menu) -------------

		connect( _local_pane, &FilePane::transfer_requested, this,
				 [this]( QStringList const& paths ) { upload_into( paths, _remote_pane->current_directory() ); } );
		connect( _remote_pane, &FilePane::transfer_requested, this,
				 [this]( QStringList const& paths ) { download_into( paths, _local_pane->current_directory() ); } );

		// -- Drops onto the remote pane -----------------------------------------

		connect( _remote_pane, &FilePane::local_files_dropped, this,
				 [this]( QStringList const& paths, QString const& target ) { upload_into( paths, target ); } );

		connect( _remote_pane, &FilePane::remote_files_dropped, this,
				 [this]( RemoteDragPayload const& payload, QString const& target ){
					 if( payload.session_id != _session_id ){
						 // Cross-session moves would need a second connection to
						 // relay through; refuse rather than half-doing it.
						 Q_EMIT status_message( tr( "Files can only be moved within the same session" ) );
						 return;
					 }
					 move_remote( payload.paths, target );
				 } );

		// -- Drops onto the local pane ------------------------------------------

		connect( _local_pane, &FilePane::remote_files_dropped, this,
				 [this]( RemoteDragPayload const& payload, QString const& target ){
					 if( payload.session_id != _session_id ){
						 Q_EMIT status_message( tr( "This drag came from another session" ) );
						 return;
					 }
					 download_into( payload.paths, target );
				 } );

		connect( _local_pane, &FilePane::local_files_dropped, this, [this]( QStringList const&, QString const& ){
			// Local-to-local copying belongs to Finder, not to an SSH client.
			Q_EMIT status_message( tr( "Drop files onto the remote pane to upload them" ) );
		} );

		// -- Refresh the affected side once a transfer lands --------------------

		connect( _transfers, &TransferManager::transfer_completed, this,
				 [this]( ssh::TransferDirection direction, QString const& ){
					 if( direction == ssh::TransferDirection::UPLOAD )
						 _remote_pane->refresh();
					 else
						 _local_pane->refresh();
				 } );

		connect( _transfers, &TransferManager::transfer_failed, this,
				 [this]( QString const& name, QString const& message ){
					 Q_EMIT status_message( tr( "%1: %2" ).arg( name, message ) );
				 } );

		connect( _remote_model, &FileListModel::error_occurred, this, &FileBrowser::status_message );
		connect( _local_model, &FileListModel::error_occurred, this, &FileBrowser::status_message );
	}

	void FileBrowser::set_connected( bool connected, QString const& home_directory ){
		if( connected )
			_remote_pane->clear_placeholder();
		else
			_remote_pane->set_placeholder( tr( "Disconnected" ) );

		_remote_model->set_connected( connected, home_directory );
	}

	void FileBrowser::set_remote_title( QString const& title ){
		_remote_pane->set_title( title );
	}

	void FileBrowser::upload_to_current_remote_directory( QStringList const& paths ){
		upload_into( paths, _remote_pane->current_directory() );
	}

	void FileBrowser::upload_into( QStringList const& local_paths, QString const& remote_directory ){
		if( remote_directory.isEmpty() ){
			Q_EMIT status_message( tr( "The remote pane is not connected yet" ) );
			return;
		}

		for( QString const& path : local_paths )
			_transfers->enqueue_upload( path, remote_directory );

		_transfer_panel->expand();
	}

	void FileBrowser::download_into( QStringList const& remote_paths, QString const& local_directory ){
		if( local_directory.isEmpty() )
			return;

		for( QString const& path : remote_paths )
			_transfers->enqueue_download( path, local_directory );

		_transfer_panel->expand();
	}

	void FileBrowser::move_remote( QStringList const& remote_paths, QString const& remote_directory ){
		for( QString const& path : remote_paths ){
			QString const name   = remote_base_name( path );
			QString const target = join_remote( remote_directory, name );

			if( target == path )
				continue; // Dropped back where it came from.

			_service->rename_entry( path, target );
		}
	}

} // namespace arterm::files
