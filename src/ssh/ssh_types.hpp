#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace arterm::ssh
{

	/// Every string here is UTF-8; conversion to NSString happens in the UI layer.
	using Timestamp = std::chrono::system_clock::time_point;

	/// Authentication strategies offered by the host editor, tried in the order
	/// listed by `HostProfile::auth_order()`.
	enum class AuthMethod{
		AGENT,                ///< ssh-agent / SSH_AUTH_SOCK.
		PUBLIC_KEY,           ///< Explicit private key file (+ optional passphrase).
		PASSWORD,             ///< Plain password.
		KEYBOARD_INTERACTIVE, ///< Challenge/response, answered with the password.
	};

	std::string auth_method_name( AuthMethod method );

	/// Which wire protocol carries the bytes of a transfer.
	///
	/// Browsing is always SFTP because SCP cannot enumerate a directory. SCP is
	/// offered for the actual copying because it is measurably faster on
	/// high-latency links, and because some hardened hosts disable the SFTP
	/// subsystem for shell users while leaving scp available.
	enum class TransferBackend { SFTP, SCP };

	std::string transfer_backend_name( TransferBackend backend );

	/// Everything needed to open a connection to one host.
	struct HostProfile
	{
		std::string   id;    ///< Stable UUID, primary key in the host store.
		std::string   label; ///< Display name in the sidebar.
		std::string   hostname;
		std::uint16_t port{ 22 };
		std::string   username;
		std::string   group; ///< Optional folder name for sidebar grouping.
		std::string   color_tag;

		AuthMethod  preferred_auth{ AuthMethod::AGENT };
		std::string private_key_path;
		bool        use_agent{ true };

		/// Secrets are never serialised into the profile file; they live in the
		/// platform keychain and are looked up by `id`.
		std::string password;
		std::string key_passphrase;

		std::string startup_directory; ///< Initial remote directory for the file pane.
		std::string startup_command;   ///< Optional command run right after login.
		bool        open_file_browser{ true };

		int             keep_alive_seconds{ 30 };
		bool            compression{ false };
		bool            strict_host_key_checking{ true };
		TransferBackend transfer_backend{ TransferBackend::SFTP };

		[[nodiscard]] std::string display_name() const{
			if( !label.empty() )
				return label;
			if( username.empty() )
				return hostname;
			return username + '@' + hostname;
		}

		[[nodiscard]] std::string endpoint() const { return hostname + ':' + std::to_string( port ); }

		/// Authentication methods to attempt, most preferred first.
		[[nodiscard]] std::vector<AuthMethod> auth_order() const;
	};

	/// Outcome of the known_hosts check, surfaced to the user before authenticating.
	enum class HostKeyVerdict{
		MATCH,    ///< Key already trusted.
		UNKNOWN,  ///< Host absent from known_hosts.
		MISMATCH, ///< Host present but the key differs - possible MITM.
		UNUSABLE, ///< known_hosts could not be read.
	};

	struct HostKeyInfo
	{
		HostKeyVerdict verdict{ HostKeyVerdict::UNKNOWN };
		std::string    hostname;
		std::uint16_t  port{ 22 };
		std::string    key_type;          ///< "ssh-ed25519", "ssh-rsa", ...
		std::string    sha256;            ///< "SHA256:base64", as printed by OpenSSH.
		std::string    md5;               ///< Legacy colon-separated fingerprint.
		std::string    raw_key;           ///< Raw bytes; needed to persist the key on acceptance.
		int            raw_key_type{ 0 }; ///< libssh2 LIBSSH2_HOSTKEY_TYPE_*.
	};

	/// A single entry of a remote directory listing.
	struct RemoteFileEntry
	{
		std::string   name;
		std::string   path; ///< Absolute remote path.
		std::uint64_t size{ 0 };
		Timestamp     modified{};
		std::uint32_t permissions{ 0 };
		std::uint32_t uid{ 0 };
		std::uint32_t gid{ 0 };
		bool          is_directory{ false };
		bool          is_symlink{ false };
		bool          is_executable{ false };

		/// "drwxr-xr-x" style rendering of `permissions`.
		[[nodiscard]] std::string permission_string() const;
	};

	using RemoteListing = std::vector<RemoteFileEntry>;

	/// Direction and bookkeeping for one queued file transfer.
	enum class TransferDirection { UPLOAD, DOWNLOAD };

	enum class TransferState { QUEUED, RUNNING, COMPLETED, FAILED, CANCELLED };

	struct TransferProgress
	{
		std::uint64_t transferred{ 0 };
		std::uint64_t total{ 0 };
		double        bytes_per_second{ 0.0 };

		[[nodiscard]] double fraction() const{
			return total == 0 ? 0.0 : static_cast<double>( transferred ) / static_cast<double>( total );
		}
	};

} // namespace arterm::ssh
