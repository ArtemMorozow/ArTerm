#pragma once

#include <expected>
#include <string>
#include <utility>

namespace arterm
{

	/// Category of a failure, used to decide how the UI should present it.
	enum class ErrorKind{
		NONE,
		NETWORK,        ///< DNS/connect/socket level failure.
		HANDSHAKE,      ///< SSH protocol negotiation failure.
		HOST_KEY,       ///< known_hosts mismatch or unknown host.
		AUTHENTICATION, ///< Credentials rejected.
		CHANNEL,        ///< Channel/PTY/shell failure on an established session.
		SFTP,           ///< Remote filesystem operation failure.
		LOCAL_IO,       ///< Local filesystem failure.
		CANCELLED,      ///< The user aborted the operation.
		INTERNAL,
	};

	/// A failure carrying a human readable message plus the raw libssh2 code when
	/// one is available. Messages are UTF-8, like every string in the core.
	struct Error
	{
		ErrorKind   kind{ ErrorKind::INTERNAL };
		std::string message;
		int         code{ 0 };

		Error() = default;

		Error( ErrorKind error_kind, std::string error_message, int error_code = 0 )
			: kind( error_kind )
			, message( std::move( error_message ) )
			, code( error_code )
		{}

		[[nodiscard]] bool is_cancellation() const noexcept { return kind == ErrorKind::CANCELLED; }
	};

	/// `std::expected` specialised on our error type; the project-wide result idiom.
	template <typename T_>
	using Result = std::expected<T_, Error>;

	using Status = Result<void>;

	[[nodiscard]] inline std::unexpected<Error> fail( ErrorKind kind, std::string message, int code = 0 ){
		return std::unexpected( Error{ kind, std::move( message ), code } );
	}

	[[nodiscard]] inline std::unexpected<Error> cancelled(){
		return std::unexpected( Error{ ErrorKind::CANCELLED, "Operation cancelled", 0 } );
	}

} // namespace arterm
