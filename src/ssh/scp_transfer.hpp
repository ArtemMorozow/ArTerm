#pragma once

#include "core/result.hpp"
#include "ssh/ssh_types.hpp"

#include <QString>

#include <functional>

using LIBSSH2_SESSION = struct _LIBSSH2_SESSION;

namespace arterm::ssh
{

	/// Single-file transfers over the SCP subsystem.
	///
	/// SCP cannot enumerate directories, so ArTerm always browses over SFTP; these
	/// helpers exist because SCP is measurably faster on high-latency links and
	/// because some hardened hosts disable the SFTP subsystem entirely. Which
	/// backend is used is a per-profile setting.
	namespace scp
	{

		/// Reports bytes moved so far and total size. Returning false cancels.
		using ProgressCallback = std::function<bool( quint64 transferred, quint64 total )>;

		/// Copy a remote file to a local path. The session must be in blocking mode.
		[[nodiscard]] Status receive_file( LIBSSH2_SESSION* session, QString const& remote_path,
										   QString const& local_path, ProgressCallback const& on_progress );

		/// Copy a local file to a remote path, preserving the local mode bits.
		[[nodiscard]] Status send_file( LIBSSH2_SESSION* session, QString const& local_path, QString const& remote_path,
										ProgressCallback const& on_progress );

	} // namespace scp
} // namespace arterm::ssh
