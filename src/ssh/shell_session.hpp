#pragma once

#include "core/dispatch.hpp"
#include "core/non_copyable.hpp"
#include "core/result.hpp"
#include "core/signal.hpp"
#include "ssh/ssh_types.hpp"

#include <atomic>
#include <memory>
#include <string>

using LIBSSH2_CHANNEL = struct _LIBSSH2_CHANNEL;

namespace arterm::ssh
{

	class SshConnection;
	class SessionInteraction;

	/// Drives one interactive shell: connect, allocate a PTY, then pump bytes both
	/// ways.
	///
	/// The session owns a serial queue and does all libssh2 work there; the public
	/// methods only enqueue. Signals fire on that queue - a slot that touches the
	/// UI marshals itself with `on_main`. Instances are shared_ptr-managed so a
	/// block still sitting in the queue after the owner lets go finds an expired
	/// weak_ptr instead of a dangling object.
	class ShellSession : public std::enable_shared_from_this<ShellSession>, NonCopyable
	{
	public:
		enum class State { IDLE, CONNECTING, AUTHENTICATING, READY, CLOSING, CLOSED, FAILED };

		[[nodiscard]] static std::shared_ptr<ShellSession> create( HostProfile                         profile,
																   std::shared_ptr<SessionInteraction> interaction );
		~ShellSession();

		/// Readable from any thread; updated on the session queue.
		[[nodiscard]] State state() const noexcept { return _state.load( std::memory_order_acquire ); }

		/// Open the connection and start the shell.
		void start();

		/// Queue bytes for the remote PTY (keyboard input, pasted text).
		void write( std::string data );

		/// Tell the remote side the terminal was resized.
		void resize( int columns, int rows, int pixel_width, int pixel_height );

		/// Politely close the channel and the connection.
		void shutdown();

		// Signals, all emitted on the session queue.
		Signal<State>                                  state_changed;
		Signal<std::string const&, std::string const&> connected; ///< banner, auth method name.
		Signal<std::string const&>                     data_received;
		Signal<std::string const&>                     error_output_received;
		Signal<Error const&>                           failed;
		Signal<int>                                    closed; ///< exit status.

	private:
		ShellSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction );

		/// Enqueue `work` bound to a weak reference; a no-op once the session died.
		void enqueue( void ( ShellSession::*work )() );

		void run_start();
		void run_shutdown();

		void                 set_state( State state );
		void                 drain_channel();
		void                 send_keep_alive();
		void                 teardown();
		[[nodiscard]] Status open_shell( int columns, int rows );
		[[nodiscard]] Status write_all( std::string const& data );

		HostProfile                         _profile;
		std::shared_ptr<SessionInteraction> _interaction;
		std::unique_ptr<SshConnection>      _connection;
		LIBSSH2_CHANNEL*                    _channel{ nullptr };

		Queue      _queue;
		ReadSource _read_source;
		Timer      _poll_timer;
		Timer      _keep_alive_timer;

		std::atomic<State> _state{ State::IDLE };
		int                _pending_columns{ 80 };
		int                _pending_rows{ 24 };
		std::string        _pending_write; ///< Bytes typed before the shell became ready.
		bool               _torndown{ false };
	};

} // namespace arterm::ssh
