#pragma once

#include "core/result.hpp"
#include "ssh/shell_session.hpp"
#include "ssh/ssh_types.hpp"

#include <QObject>

#include <memory>

class QThread;

namespace arterm::ssh
{
	class SessionInteraction;
}

namespace arterm::session
{

	/// GUI-thread facade over `ssh::ShellSession`.
	class ShellService : public QObject
	{
		Q_OBJECT

	public:
		ShellService( ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
					  QObject* parent = nullptr );
		~ShellService() override;

		void start();
		void stop();

		void write( QByteArray const& data );
		void resize( int columns, int rows, int pixel_width, int pixel_height );

		[[nodiscard]] ssh::ShellSession::State state() const noexcept { return _state; }
		[[nodiscard]] bool is_ready() const noexcept { return _state == ssh::ShellSession::State::READY; }

	Q_SIGNALS:
		void state_changed( arterm::ssh::ShellSession::State state );
		void connected( QString const& banner, QString const& auth_method );
		void data_received( QByteArray const& data );
		void failed( arterm::Error const& error );
		void closed( int exit_status );

	private:
		QThread*                 _thread{ nullptr };
		ssh::ShellSession*       _session{ nullptr }; ///< Owned by the worker thread.
		ssh::ShellSession::State _state{ ssh::ShellSession::State::IDLE };
	};

} // namespace arterm::session
