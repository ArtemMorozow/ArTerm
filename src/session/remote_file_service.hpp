#pragma once

#include "core/result.hpp"
#include "ssh/sftp_session.hpp"
#include "ssh/ssh_types.hpp"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class QThread;

namespace arterm::ssh
{
	class SessionInteraction;
}

namespace arterm::session
{

	/// GUI-thread facade over `ssh::SftpSession`.
	///
	/// It owns the worker thread, forwards requests to it with queued invocations
	/// and re-emits the results. Callers get a request id back and match it against
	/// the signals; nothing here ever blocks the GUI thread.
	class RemoteFileService : public QObject
	{
		Q_OBJECT

	public:
		RemoteFileService( ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
						   QObject* parent = nullptr );
		~RemoteFileService() override;

		/// Connect and initialise the SFTP subsystem.
		void start();

		/// Close the connection and join the worker thread.
		void stop();

		[[nodiscard]] bool           is_connected() const noexcept { return _connected; }
		[[nodiscard]] QString const& home_directory() const noexcept { return _home_directory; }

		void set_transfer_backend( ssh::TransferBackend backend );

		// Each call returns the request id the matching signal will carry.
		quint64 list_directory( QString const& path );
		quint64 resolve_path( QString const& path );
		quint64 make_directory( QString const& path );
		quint64 remove_entry( QString const& path, bool recursive );
		quint64 rename_entry( QString const& from, QString const& to );
		quint64 change_permissions( QString const& path, quint32 mode );
		quint64 download( QString const& remote_path, QString const& local_path );
		quint64 upload( QString const& local_path, QString const& remote_path );

		/// Thread safe; a running transfer stops at its next chunk boundary.
		void cancel( quint64 request_id );

	Q_SIGNALS:
		void ready( QString const& home_directory );
		void failed( arterm::Error const& error );
		void disconnected();

		void listing_ready( quint64 request_id, QString const& path, arterm::ssh::RemoteListing const& entries );
		void path_resolved( quint64 request_id, QString const& path );
		void operation_finished( quint64 request_id );
		void operation_failed( quint64 request_id, arterm::Error const& error );

		void transfer_started( quint64 request_id, quint64 total_bytes );
		void transfer_progress( quint64 request_id, arterm::ssh::TransferProgress const& progress );
		void transfer_finished( quint64 request_id );

	private:
		[[nodiscard]] quint64 next_request_id();

		QThread*          _thread{ nullptr };
		ssh::SftpSession* _session{ nullptr }; ///< Owned by the worker thread.

		QString              _home_directory;
		std::atomic<bool>    _connected{ false };
		std::atomic<quint64> _next_request_id{ 1 };
	};

} // namespace arterm::session
