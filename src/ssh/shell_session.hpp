#pragma once

#include "core/result.hpp"
#include "ssh/ssh_types.hpp"

#include <QByteArray>
#include <QObject>
#include <QSize>

#include <memory>

class QSocketNotifier;
class QTimer;

using LIBSSH2_CHANNEL = struct _LIBSSH2_CHANNEL;

namespace arterm::ssh
{

	class SshConnection;
	class SessionInteraction;

	/// Drives one interactive shell: connect, allocate a PTY, then pump bytes both
	/// ways.
	///
	/// The object is moved onto a dedicated worker thread by `ShellSessionHandle`.
	/// Every public slot is meant to be reached with a queued connection; only the
	/// signals cross back to the GUI thread.
	class ShellSession : public QObject
	{
		Q_OBJECT

	public:
		enum class State { IDLE, CONNECTING, AUTHENTICATING, READY, CLOSING, CLOSED, FAILED };
		Q_ENUM( State )

		ShellSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction, QObject* parent = nullptr );
		~ShellSession() override;

		[[nodiscard]] State state() const noexcept { return _state; }

	public Q_SLOTS:
		/// Open the connection and start the shell. Runs on the worker thread.
		void start();

		/// Queue bytes for the remote PTY (keyboard input, pasted text).
		void write( QByteArray const& data );

		/// Tell the remote side the terminal was resized.
		void resize( int columns, int rows, int pixel_width, int pixel_height );

		/// Politely close the channel and the connection.
		void shutdown();

	Q_SIGNALS:
		void state_changed( arterm::ssh::ShellSession::State state );
		void connected( QString const& banner, QString const& auth_method );
		void data_received( QByteArray const& data );
		void error_output_received( QByteArray const& data );
		void failed( arterm::Error const& error );
		void closed( int exit_status );

	private:
		void                 set_state( State state );
		void                 drain_channel();
		void                 send_keep_alive();
		void                 teardown();
		[[nodiscard]] Status open_shell( int columns, int rows );
		[[nodiscard]] Status write_all( QByteArray const& data );

		HostProfile                         _profile;
		std::shared_ptr<SessionInteraction> _interaction;
		std::unique_ptr<SshConnection>      _connection;
		LIBSSH2_CHANNEL*                    _channel{ nullptr };

		QSocketNotifier* _read_notifier{ nullptr };
		QTimer*          _poll_timer{ nullptr };
		QTimer*          _keep_alive_timer{ nullptr };

		State      _state{ State::IDLE };
		QSize      _pending_size{ 80, 24 };
		QByteArray _pending_write; ///< Bytes typed before the shell became ready.
		bool       _torndown{ false };
	};

} // namespace arterm::ssh

Q_DECLARE_METATYPE( arterm::ssh::ShellSession::State )
