#include "session/shell_service.hpp"

#include "ssh/session_interaction.hpp"

#include <QMetaObject>
#include <QThread>

namespace arterm::session
{

	ShellService::ShellService( ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
								QObject* parent )
		: QObject( parent )
		, _thread( new QThread )
		, _session( new ssh::ShellSession( std::move( profile ), std::move( interaction ) ) )
	{
		_thread->setObjectName( QStringLiteral( "arterm-shell" ) );
		_session->moveToThread( _thread );

		connect( _thread, &QThread::finished, _session, &QObject::deleteLater );

		connect( _session, &ssh::ShellSession::state_changed, this, [this]( ssh::ShellSession::State state ){
			_state = state;
			Q_EMIT state_changed( state );
		} );
		connect( _session, &ssh::ShellSession::connected, this, &ShellService::connected );
		connect( _session, &ssh::ShellSession::data_received, this, &ShellService::data_received );
		connect( _session, &ssh::ShellSession::failed, this, &ShellService::failed );
		connect( _session, &ssh::ShellSession::closed, this, &ShellService::closed );

		// stderr from the channel is interleaved into the terminal exactly as a
		// local PTY would present it.
		connect( _session, &ssh::ShellSession::error_output_received, this, &ShellService::data_received );

		_thread->start();
	}

	ShellService::~ShellService(){
		stop();

		if( _thread != nullptr ){
			_thread->wait();
			delete _thread;
			_thread = nullptr;
		}
	}

	void ShellService::start(){
		QMetaObject::invokeMethod( _session, "start", Qt::QueuedConnection );
	}

	void ShellService::stop(){
		if( _thread == nullptr || !_thread->isRunning() )
			return;

		QMetaObject::invokeMethod( _session, "shutdown", Qt::BlockingQueuedConnection );

		_thread->quit();
		_thread->wait( 5000 );
	}

	void ShellService::write( QByteArray const& data ){
		if( data.isEmpty() )
			return;
		QMetaObject::invokeMethod( _session, "write", Qt::QueuedConnection, Q_ARG( QByteArray, data ) );
	}

	void ShellService::resize( int columns, int rows, int pixel_width, int pixel_height ){
		QMetaObject::invokeMethod( _session, "resize", Qt::QueuedConnection, Q_ARG( int, columns ), Q_ARG( int, rows ),
								   Q_ARG( int, pixel_width ), Q_ARG( int, pixel_height ) );
	}

} // namespace arterm::session
