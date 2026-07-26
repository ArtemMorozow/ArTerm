#include "session/remote_file_service.hpp"

#include "ssh/session_interaction.hpp"

#include <QMetaObject>
#include <QThread>

namespace arterm::session
{

	RemoteFileService::RemoteFileService( ssh::HostProfile                         profile,
										  std::shared_ptr<ssh::SessionInteraction> interaction, QObject* parent )
		: QObject( parent )
		, _thread( new QThread )
		, _session( new ssh::SftpSession( std::move( profile ), std::move( interaction ) ) )
	{
		_thread->setObjectName( QStringLiteral( "arterm-sftp" ) );
		_session->moveToThread( _thread );

		// The worker is deleted on its own thread once the event loop stops, which
		// is the only place its libssh2 handles may be touched.
		connect( _thread, &QThread::finished, _session, &QObject::deleteLater );

		connect( _session, &ssh::SftpSession::ready, this, [this]( QString const& home ){
			_home_directory = home;
			_connected      = true;
			Q_EMIT ready( home );
		} );
		connect( _session, &ssh::SftpSession::failed, this, [this]( Error const& error ){
			_connected = false;
			Q_EMIT failed( error );
		} );

		connect( _session, &ssh::SftpSession::listing_ready, this, &RemoteFileService::listing_ready );
		connect( _session, &ssh::SftpSession::path_resolved, this, &RemoteFileService::path_resolved );
		connect( _session, &ssh::SftpSession::operation_finished, this, &RemoteFileService::operation_finished );
		connect( _session, &ssh::SftpSession::operation_failed, this, &RemoteFileService::operation_failed );
		connect( _session, &ssh::SftpSession::transfer_started, this, &RemoteFileService::transfer_started );
		connect( _session, &ssh::SftpSession::transfer_progress, this, &RemoteFileService::transfer_progress );
		connect( _session, &ssh::SftpSession::transfer_finished, this, &RemoteFileService::transfer_finished );

		_thread->start();
	}

	RemoteFileService::~RemoteFileService(){
		stop();

		if( _thread != nullptr ){
			_thread->wait();
			delete _thread;
			_thread = nullptr;
		}
	}

	quint64 RemoteFileService::next_request_id(){
		return _next_request_id.fetch_add( 1, std::memory_order_relaxed );
	}

	void RemoteFileService::start(){
		QMetaObject::invokeMethod( _session, "start", Qt::QueuedConnection );
	}

	void RemoteFileService::stop(){
		if( _thread == nullptr || !_thread->isRunning() )
			return;

		_connected = false;

		// Blocking so the session is torn down before the event loop quits;
		// otherwise deleteLater would run against a live libssh2 handle.
		QMetaObject::invokeMethod( _session, "shutdown", Qt::BlockingQueuedConnection );

		_thread->quit();
		_thread->wait( 5000 );

		Q_EMIT disconnected();
	}

	void RemoteFileService::set_transfer_backend( ssh::TransferBackend backend ){
		QMetaObject::invokeMethod(
			_session, [session = _session, backend] { session->set_transfer_backend( backend ); },
			Qt::QueuedConnection );
	}

	void RemoteFileService::cancel( quint64 request_id ){
		// Called directly rather than queued: a queued call would sit behind the
		// very transfer it is meant to interrupt.
		_session->request_cancel( request_id );
	}

	quint64 RemoteFileService::list_directory( QString const& path ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "listDirectory", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, path ) );
		return id;
	}

	quint64 RemoteFileService::resolve_path( QString const& path ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "resolvePath", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, path ) );
		return id;
	}

	quint64 RemoteFileService::make_directory( QString const& path ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "makeDirectory", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, path ) );
		return id;
	}

	quint64 RemoteFileService::remove_entry( QString const& path, bool recursive ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "removeEntry", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, path ), Q_ARG( bool, recursive ) );
		return id;
	}

	quint64 RemoteFileService::rename_entry( QString const& from, QString const& to ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "renameEntry", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, from ), Q_ARG( QString, to ) );
		return id;
	}

	quint64 RemoteFileService::change_permissions( QString const& path, quint32 mode ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "changePermissions", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, path ), Q_ARG( quint32, mode ) );
		return id;
	}

	quint64 RemoteFileService::download( QString const& remote_path, QString const& local_path ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "download", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, remote_path ), Q_ARG( QString, local_path ) );
		return id;
	}

	quint64 RemoteFileService::upload( QString const& local_path, QString const& remote_path ){
		quint64 const id = next_request_id();
		QMetaObject::invokeMethod( _session, "upload", Qt::QueuedConnection, Q_ARG( quint64, id ),
								   Q_ARG( QString, local_path ), Q_ARG( QString, remote_path ) );
		return id;
	}

} // namespace arterm::session
