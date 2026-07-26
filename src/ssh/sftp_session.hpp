#pragma once

#include "core/dispatch.hpp"
#include "core/non_copyable.hpp"
#include "core/result.hpp"
#include "core/signal.hpp"
#include "ssh/ssh_types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

using LIBSSH2_SFTP = struct _LIBSSH2_SFTP;

namespace arterm::ssh
{

	class SshConnection;
	class SessionInteraction;

	/// Owns the second SSH connection of a session and serves every remote
	/// filesystem request on its own serial queue.
	///
	/// Requests are addressed by an id chosen by the caller so results can be
	/// matched to the view that asked for them. The public methods only enqueue;
	/// signals fire on the queue. `request_cancel` is the one method that acts
	/// immediately from any thread, so a running transfer can be interrupted.
	class SftpSession : public std::enable_shared_from_this<SftpSession>, NonCopyable
	{
	public:
		[[nodiscard]] static std::shared_ptr<SftpSession> create( HostProfile                         profile,
																  std::shared_ptr<SessionInteraction> interaction );
		~SftpSession();

		void                          set_transfer_backend( TransferBackend backend ) noexcept { _backend = backend; }
		[[nodiscard]] TransferBackend transfer_backend() const noexcept { return _backend; }

		/// Thread safe. Marks a queued or running operation for cancellation; a
		/// running transfer notices at the next chunk boundary.
		void request_cancel( std::uint64_t request_id );

		void start();
		void shutdown();

		void list_directory( std::uint64_t request_id, std::string path );
		void resolve_path( std::uint64_t request_id, std::string path );
		void make_directory( std::uint64_t request_id, std::string path );
		void remove_entry( std::uint64_t request_id, std::string path, bool recursive );
		void rename_entry( std::uint64_t request_id, std::string from, std::string to );
		void change_permissions( std::uint64_t request_id, std::string path, std::uint32_t mode );

		/// Copies a remote file or directory tree to `local_path`.
		void download( std::uint64_t request_id, std::string remote_path, std::string local_path );

		/// Copies a local file or directory tree to `remote_path`.
		void upload( std::uint64_t request_id, std::string local_path, std::string remote_path );

		// Signals, all emitted on the session queue.
		Signal<std::string const&> ready; ///< home directory.
		Signal<Error const&>       failed;

		Signal<std::uint64_t, std::string const&, RemoteListing const&> listing_ready;
		Signal<std::uint64_t, std::string const&>                       path_resolved;
		Signal<std::uint64_t>                                           operation_finished;
		Signal<std::uint64_t, Error const&>                             operation_failed;

		Signal<std::uint64_t, std::uint64_t>           transfer_started; ///< id, total bytes.
		Signal<std::uint64_t, TransferProgress const&> transfer_progress;
		Signal<std::uint64_t>                          transfer_finished;

	private:
		SftpSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction );

		void run_start();
		void run_shutdown();

		[[nodiscard]] bool is_cancelled( std::uint64_t request_id ) const;
		void               clear_cancel( std::uint64_t request_id );

		[[nodiscard]] Error                   sftp_error( std::string const& context ) const;
		[[nodiscard]] Result<RemoteListing>   read_directory( std::string const& path );
		[[nodiscard]] Result<RemoteFileEntry> stat_entry( std::string const& path );

		/// Recursive helpers; `moved` accumulates across a whole directory tree so
		/// progress stays monotonic.
		[[nodiscard]] Status download_tree( std::uint64_t request_id, std::string const& remote_path,
											std::string const& local_path, std::uint64_t total, std::uint64_t& moved );
		[[nodiscard]] Status upload_tree( std::uint64_t request_id, std::string const& local_path,
										  std::string const& remote_path, std::uint64_t total, std::uint64_t& moved );
		[[nodiscard]] Status download_file( std::uint64_t request_id, std::string const& remote_path,
											std::string const& local_path, std::uint64_t total, std::uint64_t& moved );
		[[nodiscard]] Status upload_file( std::uint64_t request_id, std::string const& local_path,
										  std::string const& remote_path, std::uint64_t total, std::uint64_t& moved );
		[[nodiscard]] Status remove_tree( std::uint64_t request_id, std::string const& path );

		[[nodiscard]] Result<std::uint64_t> remote_tree_size( std::string const& path );
		[[nodiscard]] static std::uint64_t  local_tree_size( std::string const& path );

		/// Emits a throttled progress update for `request_id`.
		void publish_progress( std::uint64_t request_id, std::uint64_t moved, std::uint64_t total );

		HostProfile                         _profile;
		std::shared_ptr<SessionInteraction> _interaction;
		std::unique_ptr<SshConnection>      _connection;
		LIBSSH2_SFTP*                       _sftp{ nullptr };

		Queue _queue;

		TransferBackend _backend{ TransferBackend::SFTP };
		std::string     _home_directory;

		mutable std::mutex                _cancel_mutex;
		std::unordered_set<std::uint64_t> _cancel_requests;

		std::chrono::steady_clock::time_point _last_progress_tick{};
		std::uint64_t                         _last_progress_bytes{ 0 };
	};

} // namespace arterm::ssh
