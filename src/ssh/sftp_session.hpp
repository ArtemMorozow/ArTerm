#pragma once

#include "core/result.hpp"
#include "ssh/ssh_types.hpp"

#include <QObject>
#include <QSet>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>

using LIBSSH2_SFTP = struct _LIBSSH2_SFTP;

namespace arterm::ssh
{

	class SshConnection;
	class SessionInteraction;

	/// Worker that owns the second SSH connection of a session and serves every
	/// remote filesystem request.
	///
	/// Requests are addressed by an id chosen by the caller so results can be
	/// matched to the widget that asked for them. All slots run on the worker
	/// thread; `request_cancel` is the one method safe to call from anywhere while
	/// a transfer is in flight.
	class SftpSession : public QObject
	{
		Q_OBJECT

	public:
		SftpSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction, QObject* parent = nullptr );
		~SftpSession() override;

		void                          set_transfer_backend( TransferBackend backend ) noexcept { _backend = backend; }
		[[nodiscard]] TransferBackend transfer_backend() const noexcept { return _backend; }

		/// Thread safe. Marks a queued or running operation for cancellation; a
		/// running transfer notices at the next chunk boundary.
		void request_cancel( quint64 request_id );

	public Q_SLOTS:
		void start();
		void shutdown();

		void list_directory( quint64 request_id, QString const& path );
		void resolve_path( quint64 request_id, QString const& path );
		void make_directory( quint64 request_id, QString const& path );
		void remove_entry( quint64 request_id, QString const& path, bool recursive );
		void rename_entry( quint64 request_id, QString const& from, QString const& to );
		void change_permissions( quint64 request_id, QString const& path, quint32 mode );

		/// Copies a remote file or directory tree to `local_path`.
		void download( quint64 request_id, QString const& remote_path, QString const& local_path );

		/// Copies a local file or directory tree to `remote_path`.
		void upload( quint64 request_id, QString const& local_path, QString const& remote_path );

	Q_SIGNALS:
		void ready( QString const& home_directory );
		void failed( arterm::Error const& error );

		void listing_ready( quint64 request_id, QString const& path, arterm::ssh::RemoteListing const& entries );
		void path_resolved( quint64 request_id, QString const& path );
		void operation_finished( quint64 request_id );
		void operation_failed( quint64 request_id, arterm::Error const& error );

		void transfer_started( quint64 request_id, quint64 total_bytes );
		void transfer_progress( quint64 request_id, arterm::ssh::TransferProgress const& progress );
		void transfer_finished( quint64 request_id );

	private:
		[[nodiscard]] bool is_cancelled( quint64 request_id ) const;
		void               clear_cancel( quint64 request_id );

		[[nodiscard]] Error                   sftp_error( QString const& context ) const;
		[[nodiscard]] Result<RemoteListing>   read_directory( QString const& path );
		[[nodiscard]] Result<RemoteFileEntry> stat_entry( QString const& path );

		/// Recursive helpers; `moved` accumulates across a whole directory tree so
		/// progress stays monotonic.
		[[nodiscard]] Status download_tree( quint64 request_id, QString const& remote_path, QString const& local_path,
											quint64 total, quint64& moved );
		[[nodiscard]] Status upload_tree( quint64 request_id, QString const& local_path, QString const& remote_path,
										  quint64 total, quint64& moved );
		[[nodiscard]] Status download_file( quint64 request_id, QString const& remote_path, QString const& local_path,
											quint64 total, quint64& moved );
		[[nodiscard]] Status upload_file( quint64 request_id, QString const& local_path, QString const& remote_path,
										  quint64 total, quint64& moved );
		[[nodiscard]] Status remove_tree( quint64 request_id, QString const& path );

		[[nodiscard]] Result<quint64> remote_tree_size( QString const& path );
		[[nodiscard]] static quint64  local_tree_size( QString const& path );

		/// Emits a throttled progress update for `request_id`.
		void publish_progress( quint64 request_id, quint64 moved, quint64 total );

		HostProfile                         _profile;
		std::shared_ptr<SessionInteraction> _interaction;
		std::unique_ptr<SshConnection>      _connection;
		LIBSSH2_SFTP*                       _sftp{ nullptr };

		TransferBackend _backend{ TransferBackend::SFTP };
		QString         _home_directory;

		mutable std::mutex _cancel_mutex;
		QSet<quint64>      _cancel_requests;

		qint64  _last_progress_tick{ 0 };
		quint64 _last_progress_bytes{ 0 };
	};

} // namespace arterm::ssh
