#include "ssh/shell_session.hpp"

#include "core/log.hpp"
#include "ssh/session_interaction.hpp"
#include "ssh/ssh_connection.hpp"

#include <libssh2.h>

#include <chrono>
#include <utility>

namespace arterm::ssh
{
	namespace
	{

		/// libssh2 hands us decrypted data in chunks; 32 KiB keeps syscalls low without
		/// making a single drain pass starve the queue.
		constexpr int READ_CHUNK = 32 * 1024;

		/// The read source can miss data that libssh2 already buffered internally,
		/// so a short timer sweeps the channel as a safety net.
		constexpr auto POLL_INTERVAL = std::chrono::milliseconds( 25 );

	} // namespace

	std::shared_ptr<ShellSession> ShellSession::create( HostProfile                         profile,
														std::shared_ptr<SessionInteraction> interaction ){
		return std::shared_ptr<ShellSession>( new ShellSession( std::move( profile ), std::move( interaction ) ) );
	}

	ShellSession::ShellSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction )
		: _profile( std::move( profile ) )
		, _interaction( std::move( interaction ) )
		, _queue( "arterm.shell." + _profile.id )
	{}

	ShellSession::~ShellSession(){
		// The last shared_ptr can be released from inside a queue block (a lambda's
		// captured `self` going out of scope), which means the destructor runs on
		// the session queue itself. `sync` onto the current queue would deadlock,
		// so tear down inline in that case - the serial queue guarantees no other
		// block is running concurrently. Otherwise serialise against in-flight
		// source handlers with a sync.
		if( _queue.is_current() )
			teardown();
		else
			_queue.sync( [this] { teardown(); } );
	}

	void ShellSession::enqueue( void ( ShellSession::*work )() ){
		_queue.async( [weak = weak_from_this(), work]{
			if( auto self = weak.lock() )
				( ( *self ).*work )();
		} );
	}

	void ShellSession::set_state( State state ){
		if( _state.exchange( state, std::memory_order_release ) != state )
			state_changed( state );
	}

	void ShellSession::start(){
		enqueue( &ShellSession::run_start );
	}

	void ShellSession::run_start(){
		if( _state.load( std::memory_order_relaxed ) != State::IDLE )
			return;

		set_state( State::CONNECTING );

		_connection = std::make_unique<SshConnection>( _profile );

		if( _interaction ){
			auto interaction = _interaction;
			_connection->set_host_key_prompt(
				[interaction]( HostKeyInfo const& info ) { return interaction->confirm_host_key( info ); } );
			_connection->set_credential_prompt( [interaction]( std::string const& prompt, bool echo ){
				return interaction->ask_credential( prompt, echo );
			} );
		}

		set_state( State::AUTHENTICATING );
		if( auto status = _connection->open(); !status ){
			_connection.reset();
			set_state( State::FAILED );
			failed( status.error() );
			return;
		}

		if( auto status = open_shell( _pending_columns, _pending_rows ); !status ){
			teardown();
			set_state( State::FAILED );
			failed( status.error() );
			return;
		}

		// From here on the session is event driven, so it must not block.
		_connection->set_blocking( false );

		_read_source = ReadSource( _connection->socket_descriptor(), _queue, [this] { drain_channel(); } );
		_read_source.resume();

		_poll_timer = Timer( POLL_INTERVAL, _queue, [this] { drain_channel(); } );
		_poll_timer.start();

		if( _profile.keep_alive_seconds > 0 ){
			_keep_alive_timer =
				Timer( std::chrono::seconds( _profile.keep_alive_seconds ), _queue, [this] { send_keep_alive(); } );
			_keep_alive_timer.start();
		}

		set_state( State::READY );
		connected( _connection->banner(), auth_method_name( _connection->used_auth_method() ) );

		if( !_profile.startup_command.empty() ){
			std::string command = _profile.startup_command;
			if( !command.ends_with( '\n' ) )
				command.push_back( '\n' );
			_pending_write += command;
		}

		if( !_pending_write.empty() ){
			std::string const buffered = std::exchange( _pending_write, std::string{} );
			if( auto status = write_all( buffered ); !status ){
				teardown();
				set_state( State::FAILED );
				failed( status.error() );
				return;
			}
		}

		drain_channel();
	}

	Status ShellSession::open_shell( int columns, int rows ){
		LIBSSH2_SESSION* session = _connection->session();

		_channel = libssh2_channel_open_session( session );
		if( _channel == nullptr )
			return std::unexpected( _connection->last_error( ErrorKind::CHANNEL, "Cannot open a session channel" ) );

		// xterm-256color matches what the bundled colour palette implements.
		static constexpr char TERM[] = "xterm-256color";
		if( libssh2_channel_request_pty_ex( _channel, TERM, sizeof( TERM ) - 1, nullptr, 0, columns, rows, 0, 0 ) !=
			0 ){
			return std::unexpected(
				_connection->last_error( ErrorKind::CHANNEL, "Cannot allocate a pseudo terminal" ) );
		}

		if( libssh2_channel_shell( _channel ) != 0 )
			return std::unexpected( _connection->last_error( ErrorKind::CHANNEL, "Cannot start the remote shell" ) );

		return {};
	}

	void ShellSession::drain_channel(){
		if( _channel == nullptr || _state.load( std::memory_order_relaxed ) != State::READY )
			return;

		std::string buffer( READ_CHUNK, '\0' );
		bool        saw_eof = false;

		// Read both streams until libssh2 has nothing left; a single socket wakeup
		// often carries several channel windows worth of data.
		for( int stream = 0; stream < 2; ++stream ){
			int const stream_id = ( stream == 0 ) ? 0 : SSH_EXTENDED_DATA_STDERR;

			while( true ){
				auto const count = libssh2_channel_read_ex( _channel, stream_id, buffer.data(), READ_CHUNK );

				if( count > 0 ){
					std::string const chunk( buffer.data(), static_cast<std::size_t>( count ) );
					if( stream_id == 0 )
						data_received( chunk );
					else
						error_output_received( chunk );
					continue;
				}

				if( count == LIBSSH2_ERROR_EAGAIN )
					break;

				if( count == 0 ){
					// Zero only means "nothing right now"; EOF is reported separately.
					break;
				}

				log_warning( "ssh", "channel read failed with {}", count );
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
			closed( exit_status );
		}
	}

	void ShellSession::write( std::string data ){
		if( data.empty() )
			return;

		_queue.async( [weak = weak_from_this(), data = std::move( data )]{
			auto self = weak.lock();
			if( !self )
				return;

			State const state = self->_state.load( std::memory_order_relaxed );
			if( state != State::READY || self->_channel == nullptr ){
				// Keep early keystrokes instead of dropping them on the floor.
				if( state == State::CONNECTING || state == State::AUTHENTICATING )
					self->_pending_write += data;
				return;
			}

			if( auto status = self->write_all( data ); !status ){
				self->teardown();
				self->set_state( State::FAILED );
				self->failed( status.error() );
			}
		} );
	}

	Status ShellSession::write_all( std::string const& data ){
		// Bound the wait so a wedged peer cannot pin the queue forever.
		constexpr int MAX_STALLED_WAITS = 60;

		std::size_t offset        = 0;
		int         stalled_waits = 0;

		while( offset < data.size() ){
			auto const written = libssh2_channel_write_ex( _channel, 0, data.data() + offset, data.size() - offset );

			if( written == LIBSSH2_ERROR_EAGAIN ){
				// The remote window is full; wait for the socket instead of spinning.
				if( !_connection->wait_socket( 500 ) && ++stalled_waits >= MAX_STALLED_WAITS )
					return fail( ErrorKind::CHANNEL, "The remote shell stopped accepting input" );
				continue;
			}

			stalled_waits = 0;

			if( written < 0 )
				return std::unexpected( _connection->last_error( ErrorKind::CHANNEL, "Cannot write to the shell" ) );

			offset += static_cast<std::size_t>( written );
		}

		return {};
	}

	void ShellSession::resize( int columns, int rows, int pixel_width, int pixel_height ){
		_queue.async( [weak = weak_from_this(), columns, rows, pixel_width, pixel_height]{
			auto self = weak.lock();
			if( !self )
				return;

			self->_pending_columns = columns;
			self->_pending_rows    = rows;

			if( self->_channel == nullptr || self->_state.load( std::memory_order_relaxed ) != State::READY )
				return;

			int rc = 0;
			do{
				rc = libssh2_channel_request_pty_size_ex( self->_channel, columns, rows, pixel_width, pixel_height );
				if( rc == LIBSSH2_ERROR_EAGAIN && !self->_connection->wait_socket( 200 ) )
					break;
			} while( rc == LIBSSH2_ERROR_EAGAIN );

			if( rc != 0 && rc != LIBSSH2_ERROR_EAGAIN )
				log_debug( "ssh", "pty resize rejected with {}", rc );
		} );
	}

	void ShellSession::send_keep_alive(){
		if( _connection == nullptr || !_connection->is_open() )
			return;

		int       seconds_to_next = 0;
		int const rc              = libssh2_keepalive_send( _connection->session(), &seconds_to_next );
		if( rc != 0 && rc != LIBSSH2_ERROR_EAGAIN )
			log_debug( "ssh", "keepalive failed with {}", rc );
	}

	void ShellSession::shutdown(){
		enqueue( &ShellSession::run_shutdown );
	}

	void ShellSession::run_shutdown(){
		State const state = _state.load( std::memory_order_relaxed );
		if( state == State::CLOSED || state == State::FAILED ){
			teardown();
			return;
		}

		set_state( State::CLOSING );
		teardown();
		set_state( State::CLOSED );
		closed( 0 );
	}

	void ShellSession::teardown(){
		// Re-entrancy guard: drain_channel() can call teardown() while a signal
		// emitted from inside it is still unwinding.
		if( _torndown )
			return;
		_torndown = true;

		_poll_timer.cancel();
		_keep_alive_timer.cancel();
		_read_source.cancel();

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
