#pragma once

#include <QMetaType>
#include <QString>

#include <expected>
#include <utility>

namespace arterm {

/// Category of a failure, used to decide how the UI should present it.
enum class ErrorKind {
    None,
    Network,        ///< DNS/connect/socket level failure.
    Handshake,      ///< SSH protocol negotiation failure.
    HostKey,        ///< known_hosts mismatch or unknown host.
    Authentication, ///< Credentials rejected.
    Channel,        ///< Channel/PTY/shell failure on an established session.
    Sftp,           ///< Remote filesystem operation failure.
    LocalIo,        ///< Local filesystem failure.
    Cancelled,      ///< The user aborted the operation.
    Internal,
};

/// A failure carrying a human readable message plus the raw libssh2 code when
/// one is available.
struct Error {
    ErrorKind kind{ErrorKind::Internal};
    QString message;
    int code{0};

    Error() = default;
    Error(ErrorKind k, QString msg, int c = 0)
        : kind(k), message(std::move(msg)), code(c) {}

    [[nodiscard]] bool isCancellation() const noexcept { return kind == ErrorKind::Cancelled; }
};

/// `std::expected` specialised on our error type; the project-wide result idiom.
template <typename T>
using Result = std::expected<T, Error>;

using Status = Result<void>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorKind kind, QString message, int code = 0)
{
    return std::unexpected(Error{kind, std::move(message), code});
}

[[nodiscard]] inline std::unexpected<Error> cancelled()
{
    return std::unexpected(Error{ErrorKind::Cancelled, QStringLiteral("Operation cancelled"), 0});
}

} // namespace arterm

Q_DECLARE_METATYPE(arterm::Error)
