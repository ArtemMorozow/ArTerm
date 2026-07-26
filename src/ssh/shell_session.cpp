#include "ssh/shell_session.hpp"

#include "ssh/session_interaction.hpp"
#include "ssh/ssh_connection.hpp"

#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QTimer>

#include <libssh2.h>

#include <utility>

Q_DECLARE_LOGGING_CATEGORY( lc_ssh )

namespace arterm::ssh
{
	namespace
	{

		/// libssh2 hands us decrypted data in chunks; 32 KiB keeps syscalls low without
		/// making a single drain pass starve the event loop.
		constexpr int READ_CHUNK = 32 * 1024;

		/// The socket notifier can miss data that libssh2 already buffered internally,
		/// so a short timer sweeps the channel as a safety net.
		constexpr int POLL_INTERVAL_MS = 25;

	} // namespace

	ShellSession::ShellSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction, QObject* parent )
		: QObject( parent )
		, _profile( std::move( profile ) )
		, _interaction( std::move( interaction ) )
	{}

	ShellSession::~ShellSession(){
		teardown();
	}

	void ShellSession::set_state( State state ){
		if( _state == state )
			return;
		_state = state;
		Q_EMIT state_changed( state );
	}

	void ShellSession::start(){
		if( _state != State::IDLE )
			return;

		set_state( State::CONNECTING );

		_connection = std::make_unique<SshConnection>( _profile );

		if( _interaction ){
			auto interaction = _interaction;
			_connection->set_host_key_prompt(
				[interaction]( HostKeyInfo const& info ) { return interaction->confirm_host_key( info ); } );
			_connection->set_credential_prompt( [interaction]( QString const& prompt, bool echo ){
				return interaction->ask_credential( prompt, echo );
			} );
		}

		set_state( State::AUTHENTICATING );
		if( auto status = _connection->open(); !status ){
			_connection.reset();
			set_state( State::FAILED );
			Q_EMIT failed( status.error() );
			return;
		}

		if( auto status = open_shell( _pending_size.width(), _pending_size.height() ); !status ){
			teardown();
			set_state( State::FAILED );
			Q_EMIT failed( status.error() );
			return;
		}

		// From here on the session is event driven, so it must not block.
		_connection->set_blocking( false );

		_read_notifier = new QSocketNotifier( _connection->socket_descriptor(), QSocketNotifier::Read, this );
		connect( _read_notifier, &QSocketNotifier::activated, this, [this] { drain_channel(); } );

		_poll_timer = new QTimer( this );
		_poll_timer->setInterval( POLL_INTERVAL_MS );
		connect( _poll_timer, &QTimer::timeout, this, [this] { drain_channel(); } );
		_poll_timer->start();

		if( _profile.keep_alive_seconds > 0 ){
			_keep_alive_timer = new QTimer( this );
			_keep_alive_timer->setInterval( _profile.keep_alive_seconds * 1000 );
			connect( _keep_alive_timer, &QTimer::timeout, this, [this] { send_keep_alive(); } );
			_keep_alive_timer->start();
		}

		set_state( State::READY );
		Q_EMIT connected( _connection->banner(), auth_method_name( _connection->used_auth_method() ) );

		if( !_profile.startup_command.isEmpty() ){
			QByteArray command = _profile.startup_command.toUtf8();
			if( !command.endsWith( '\n' ) )
				command.append( '\n' );
			_pending_write.append( command );
		}

		if( !_pending_write.isEmpty() ){
			QByteArray const buffered = std::exchange( _pending_write, QByteArray{} );
			write( buffered );
		}

		drain_channel();
	}

	Status ShellSession::open_shell( int columns, int rows ){
		LIBSSH2_SESSION* session = _connection->session();

		_channel = libssh2_channel_open_session( session );
		if( _channel == nullptr )
			return std::unexpected(
				_connection->last_error( ErrorKind::CHANNEL, QObject::tr( "Cannot open a session channel" ) ) );

		// xterm-256color matches what the bundled colour palette implements.
		static constexpr char TERM[] = "xterm-256color";
		if( libssh2_channel_request_pty_ex( _channel, TERM, sizeof( TERM ) - 1, nullptr, 0, columns, rows, 0, 0 ) !=
			0 ){
			return std::unexpected(
				_connection->last_error( ErrorKind::CHANNEL, QObject::tr( "Cannot allocate a pseudo terminal" ) ) );
		}

		if( libssh2_channel_shell( _channel ) != 0 ){
			return std::unexpected(
				_connection->last_error( ErrorKind::CHANNEL, QObject::tr( "Cannot start the remote shell" ) ) );
		}

		return {};
	}

	void ShellSession::drain_channel(){
		if( _channel == nullptr || _state != State::READY )
			return;

		QByteArray buffer( READ_CHUNK, Qt::Uninitialized );
		bool       saw_eof = false;

		// Read both streams until libssh2 has nothing left; a single socket wakeup
		// often carries several channel windows worth of data.
		for( int stream = 0; stream < 2; ++stream ){
			int const stream_id = ( stream == 0 ) ? 0 : SSH_EXTENDED_DATA_STDERR;

			while( true ){
				auto const count = libssh2_channel_read_ex( _channel, stream_id, buffer.data(), READ_CHUNK );

				if( count > 0 ){
					QByteArray const chunk( buffer.constData(), static_cast<qsizetype>( count ) );
					if( stream_id == 0 )
						Q_EMIT data_received( chunk );
					else
						Q_EMIT error_output_received( chunk );
					continue;
				}

				if( count == LIBSSH2_ERROR_EAGAIN )
					break;

				if( count == 0 ){
					// Zero only means "nothing right now"; EOF is reported separately.
					break;
				}

				qCWarning( lc_ssh ) << "channel read failed with" << count;
				saw_eof = true;
				break;
			}

			if( saw_eof )
				break;
		}

		if( saw_eof || libssh2_channel_eof( _channel ) == 1 ){
			int const exit_status = libssh2_channel_get_exit_status( _channel );
			teardown();
			set_state( State::CLOSED );
			Q_EMIT closed( exit_status );
		}
	}

	void ShellSession::write( QByteArray const& data ){
		if( data.isEmpty() )
			return;

		if( _state != State::READY || _channel == nullptr ){
			// Keep early keystrokes instead of dropping them on the floor.
			if( _state == State::CONNECTING || _state == State::AUTHENTICATING )
				_pending_write.append( data );
			return;
		}

		if( auto status = write_all( data ); !status ){
			teardown();
			set_state( State::FAILED );
			Q_EMIT failed( status.error() );
		}
	}

	Status ShellSession::write_all( QByteArray const& data ){
		// Bound the wait so a wedged peer cannot pin the worker thread forever.
		constexpr int MAX_STALLED_WAITS = 60;

		qsizetype offset        = 0;
		int       stalled_waits = 0;

		while( offset < data.size() ){
			auto const written = libssh2_channel_write_ex( _channel, 0, data.constData() + offset,
														   static_cast<size_t>( data.size() - offset ) );

			if( written == LIBSSH2_ERROR_EAGAIN ){
				// The remote window is full; wait for the socket instead of spinning.
				if( !_connection->wait_socket( 500 ) && ++stalled_waits >= MAX_STALLED_WAITS ){
					return fail( ErrorKind::CHANNEL, QObject::tr( "The remote shell stopped accepting input" ) );
				}
				continue;
			}

			stalled_waits = 0;

			if( written < 0 ){
				return std::unexpected(
					_connection->last_error( ErrorKind::CHANNEL, QObject::tr( "Cannot write to the shell" ) ) );
			}

			offset += static_cast<qsizetype>( written );
		}

		return {};
	}

	void ShellSession::resize( int columns, int rows, int pixel_width, int pixel_height ){
		_pending_size = QSize( columns, rows );

		if( _channel == nullptr || _state != State::READY )
			return;

		int rc = 0;
		do{
			rc = libssh2_channel_request_pty_size_ex( _channel, columns, rows, pixel_width, pixel_height );
			if( rc == LIBSSH2_ERROR_EAGAIN && !_connection->wait_socket( 200 ) )
				break;
		} while( rc == LIBSSH2_ERROR_EAGAIN );

		if( rc != 0 && rc != LIBSSH2_ERROR_EAGAIN )
			qCDebug( lc_ssh ) << "pty resize rejected with" << rc;
	}

	void ShellSession::send_keep_alive(){
		if( _connection == nullptr || !_connection->is_open() )
			return;

		int       seconds_to_next = 0;
		int const rc              = libssh2_keepalive_send( _connection->session(), &seconds_to_next );
		if( rc != 0 && rc != LIBSSH2_ERROR_EAGAIN )
			qCDebug( lc_ssh ) << "keepalive failed with" << rc;
	}

	void ShellSession::shutdown(){
		if( _state == State::CLOSED || _state == State::FAILED ){
			teardown();
			return;
		}

		set_state( State::CLOSING );
		teardown();
		set_state( State::CLOSED );
		Q_EMIT closed( 0 );
	}

	void ShellSession::teardown(){
		// Re-entrancy guard: drain_channel() can call teardown() while a signal
		// emitted from inside it is still unwinding.
		if( _torndown )
			return;
		_torndown = true;

		if( _poll_timer != nullptr ){
			_poll_timer->stop();
			_poll_timer->deleteLater();
			_poll_timer = nullptr;
		}
		if( _keep_alive_timer != nullptr ){
			_keep_alive_timer->stop();
			_keep_alive_timer->deleteLater();
			_keep_alive_timer = nullptr;
		}
		if( _read_notifier != nullptr ){
			_read_notifier->setEnabled( false );
			_read_notifier->deleteLater();
			_read_notifier = nullptr;
		}

		if( _channel != nullptr ){
			if( _connection != nullptr )
				_connection->set_blocking( true );
			libssh2_channel_close( _channel );
			libssh2_channel_free( _channel );
			_channel = nullptr;
		}

		_connection.reset();
		_torndown = false;
	}

} // namespace arterm::ssh
