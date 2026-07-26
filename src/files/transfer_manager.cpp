#include "files/transfer_manager.hpp"

#include "files/file_list_model.hpp"
#include "session/remote_file_service.hpp"

#include <QDir>
#include <QFileInfo>

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

		QString state_text( ssh::TransferState state ){
			switch( state ){
				case ssh::TransferState::QUEUED:
					return QObject::tr( "Queued" );
				case ssh::TransferState::RUNNING:
					return QObject::tr( "Transferring" );
				case ssh::TransferState::COMPLETED:
					return QObject::tr( "Done" );
				case ssh::TransferState::FAILED:
					return QObject::tr( "Failed" );
				case ssh::TransferState::CANCELLED:
					return QObject::tr( "Cancelled" );
			}
			return {};
		}

	} // namespace

	TransferManager::TransferManager( session::RemoteFileService* service, QObject* parent )
		: QAbstractTableModel( parent )
		, _service( service )
	{
		connect( _service, &session::RemoteFileService::transfer_started, this,
				 [this]( quint64 request_id, quint64 total ){
					 int const row = index_of_request( request_id );
					 if( row < 0 )
						 return;
					 _jobs[row].progress.total = total;
					 emit_row_changed( row );
				 } );

		connect( _service, &session::RemoteFileService::transfer_progress, this,
				 [this]( quint64 request_id, ssh::TransferProgress const& progress ){
					 int const row = index_of_request( request_id );
					 if( row < 0 )
						 return;
					 _jobs[row].progress = progress;
					 emit_row_changed( row );
					 publish_queue_progress();
				 } );

		connect( _service, &session::RemoteFileService::transfer_finished, this, [this]( quint64 request_id ){
			if( index_of_request( request_id ) < 0 )
				return;
			finish_current( ssh::TransferState::COMPLETED, {} );
		} );

		connect( _service, &session::RemoteFileService::operation_failed, this,
				 [this]( quint64 request_id, Error const& error ){
					 // operation_failed also fires for listings and mkdir; only react
					 // when the id belongs to a transfer we started.
					 if( index_of_request( request_id ) < 0 )
						 return;
					 finish_current( error.is_cancellation() ? ssh::TransferState::CANCELLED
															 : ssh::TransferState::FAILED,
									 error.message );
				 } );
	}

	int TransferManager::rowCount( QModelIndex const& parent ) const{
		return parent.isValid() ? 0 : static_cast<int>( _jobs.size() );
	}

	int TransferManager::columnCount( QModelIndex const& parent ) const{
		return parent.isValid() ? 0 : static_cast<int>( Column::COUNT );
	}

	QVariant TransferManager::data( QModelIndex const& index, int role ) const{
		if( !index.isValid() || index.row() >= _jobs.size() )
			return {};

		TransferJob const& job = _jobs.at( index.row() );

		switch( role ){
			case Qt::DisplayRole:
				switch( static_cast<Column>( index.column() ) ){
					case Column::NAME:
						return job.display_name;
					case Column::DIRECTION:
						return job.direction == ssh::TransferDirection::UPLOAD ? tr( "Upload" ) : tr( "Download" );
					case Column::PROGRESS:
						if( job.progress.total == 0 )
							return QString();
						return tr( "%1 / %2" )
							.arg( format_file_size( job.progress.transferred ),
								  format_file_size( job.progress.total ) );
					case Column::SPEED:
						if( job.state != ssh::TransferState::RUNNING || job.progress.bytes_per_second <= 0.0 )
							return QString();
						return tr( "%1/s" ).arg(
							format_file_size( static_cast<quint64>( job.progress.bytes_per_second ) ) );
					case Column::STATUS:
						return job.state == ssh::TransferState::FAILED && !job.error_message.isEmpty()
								   ? job.error_message
								   : state_text( job.state );
					case Column::COUNT:
						break;
				}
				return {};

			case Qt::ToolTipRole:
				return tr( "%1\nto %2" ).arg( job.source_path, job.destination_path );

			case FRACTION_ROLE:
				return job.progress.fraction();
			case STATE_ROLE:
				return static_cast<int>( job.state );
			case JOB_ID_ROLE:
				return static_cast<qulonglong>( job.id );

			default:
				return {};
		}
	}

	QVariant TransferManager::headerData( int section, Qt::Orientation orientation, int role ) const{
		if( orientation != Qt::Horizontal || role != Qt::DisplayRole )
			return {};

		switch( static_cast<Column>( section ) ){
			case Column::NAME:
				return tr( "File" );
			case Column::DIRECTION:
				return tr( "Direction" );
			case Column::PROGRESS:
				return tr( "Progress" );
			case Column::SPEED:
				return tr( "Speed" );
			case Column::STATUS:
				return tr( "Status" );
			case Column::COUNT:
				break;
		}
		return {};
	}

	void TransferManager::enqueue_download( QString const& remote_path, QString const& local_directory ){
		TransferJob job;
		job.id               = _next_job_id++;
		job.direction        = ssh::TransferDirection::DOWNLOAD;
		job.source_path      = remote_path;
		job.display_name     = remote_base_name( remote_path );
		job.destination_path = QDir( local_directory ).absoluteFilePath( job.display_name );

		beginInsertRows( {}, static_cast<int>( _jobs.size() ), static_cast<int>( _jobs.size() ) );
		_jobs.append( std::move( job ) );
		endInsertRows();

		start_next();
	}

	void TransferManager::enqueue_upload( QString const& local_path, QString const& remote_directory ){
		TransferJob job;
		job.id               = _next_job_id++;
		job.direction        = ssh::TransferDirection::UPLOAD;
		job.source_path      = local_path;
		job.display_name     = QFileInfo( local_path ).fileName();
		job.destination_path = join_remote( remote_directory, job.display_name );

		beginInsertRows( {}, static_cast<int>( _jobs.size() ), static_cast<int>( _jobs.size() ) );
		_jobs.append( std::move( job ) );
		endInsertRows();

		start_next();
	}

	void TransferManager::start_next(){
		if( _running_row >= 0 )
			return;

		auto const it = std::find_if( _jobs.begin(), _jobs.end(), []( TransferJob const& job ){
			return job.state == ssh::TransferState::QUEUED;
		} );
		if( it == _jobs.end() ){
			publish_queue_progress();
			return;
		}

		int const row = static_cast<int>( std::distance( _jobs.begin(), it ) );

		TransferJob& job = _jobs[row];
		job.state        = ssh::TransferState::RUNNING;
		job.clock.start();

		job.request_id = ( job.direction == ssh::TransferDirection::DOWNLOAD )
							 ? _service->download( job.source_path, job.destination_path )
							 : _service->upload( job.source_path, job.destination_path );

		_running_row = row;
		emit_row_changed( row );
		publish_queue_progress();
	}

	void TransferManager::finish_current( ssh::TransferState state, QString const& message ){
		if( _running_row < 0 || _running_row >= _jobs.size() )
			return;

		int const row = _running_row;
		_running_row  = -1;

		TransferJob& job  = _jobs[row];
		job.state         = state;
		job.error_message = message;
		job.request_id    = 0;

		if( state == ssh::TransferState::COMPLETED )
			job.progress.transferred = job.progress.total;

		emit_row_changed( row );

		if( state == ssh::TransferState::COMPLETED )
			Q_EMIT transfer_completed( job.direction, job.destination_path );
		else if( state == ssh::TransferState::FAILED )
			Q_EMIT transfer_failed( job.display_name, message );

		start_next();
	}

	int TransferManager::index_of_request( quint64 request_id ) const{
		if( request_id == 0 )
			return -1;

		for( int row = 0; row < _jobs.size(); ++row ){
			if( _jobs.at( row ).request_id == request_id && _jobs.at( row ).state == ssh::TransferState::RUNNING )
				return row;
		}
		return -1;
	}

	void TransferManager::emit_row_changed( int row ){
		if( row < 0 || row >= _jobs.size() )
			return;
		Q_EMIT dataChanged( index( row, 0 ), index( row, static_cast<int>( Column::COUNT ) - 1 ) );
	}

	void TransferManager::cancel( quint64 job_id ){
		for( int row = 0; row < _jobs.size(); ++row ){
			TransferJob& job = _jobs[row];
			if( job.id != job_id )
				continue;

			if( job.state == ssh::TransferState::RUNNING ){
				_service->cancel( job.request_id );
			}
			else if( job.state == ssh::TransferState::QUEUED ){
				job.state = ssh::TransferState::CANCELLED;
				emit_row_changed( row );
				publish_queue_progress();
			}
			return;
		}
	}

	void TransferManager::cancel_all(){
		for( int row = 0; row < _jobs.size(); ++row ){
			TransferJob& job = _jobs[row];
			if( job.state == ssh::TransferState::RUNNING ){
				_service->cancel( job.request_id );
			}
			else if( job.state == ssh::TransferState::QUEUED ){
				job.state = ssh::TransferState::CANCELLED;
				emit_row_changed( row );
			}
		}
		publish_queue_progress();
	}

	void TransferManager::clear_completed(){
		for( int row = static_cast<int>( _jobs.size() ) - 1; row >= 0; --row ){
			ssh::TransferState const state = _jobs.at( row ).state;
			if( state == ssh::TransferState::RUNNING || state == ssh::TransferState::QUEUED )
				continue;

			beginRemoveRows( {}, row, row );
			_jobs.removeAt( row );
			endRemoveRows();

			if( _running_row > row )
				--_running_row;
		}
	}

	bool TransferManager::has_active_transfers() const{
		return active_count() > 0;
	}

	int TransferManager::active_count() const{
		return static_cast<int>( std::count_if( _jobs.begin(), _jobs.end(), []( TransferJob const& job ){
			return job.state == ssh::TransferState::RUNNING || job.state == ssh::TransferState::QUEUED;
		} ) );
	}

	void TransferManager::publish_queue_progress(){
		quint64 transferred = 0;
		quint64 total       = 0;
		int     remaining   = 0;

		for( TransferJob const& job : _jobs ){
			if( job.state != ssh::TransferState::RUNNING && job.state != ssh::TransferState::QUEUED )
				continue;
			++remaining;
			transferred += job.progress.transferred;
			total += job.progress.total;
		}

		if( remaining == 0 ){
			Q_EMIT queue_progress_changed( -1.0, 0 );
			return;
		}

		double const fraction = total == 0 ? 0.0 : static_cast<double>( transferred ) / static_cast<double>( total );
		Q_EMIT queue_progress_changed( fraction, remaining );
	}

} // namespace arterm::files
