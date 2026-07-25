#pragma once

#include "core/Result.hpp"
#include "ssh/SshTypes.hpp"

#include <functional>
#include <optional>
#include <string>

using LIBSSH2_SESSION = struct _LIBSSH2_SESSION;

namespace arterm::ssh {

/// Owns one TCP socket plus its libssh2 session: resolve, connect, verify the
/// host key, authenticate.
///
/// Every method must be called from the thread that created the object.
/// ArTerm opens two connections per host - one driving the interactive shell,
/// one driving SFTP - because a libssh2 session may not be used concurrently
/// from several threads.
class SshConnection {
public:
    /// Invoked when the host key is unknown or has changed. Returning true
    /// accepts (and persists) the key, false aborts the connection.
    using HostKeyPrompt = std::function<bool(const HostKeyInfo &)>;

    /// Invoked when an interactive credential is required and the profile does
    /// not carry one. Returns std::nullopt when the user cancels.
    using CredentialPrompt = std::function<std::optional<QString>(const QString &prompt, bool echo)>;

    explicit SshConnection(HostProfile profile);
    ~SshConnection();

    SshConnection(const SshConnection &) = delete;
    SshConnection &operator=(const SshConnection &) = delete;

    void setHostKeyPrompt(HostKeyPrompt prompt) { m_hostKeyPrompt = std::move(prompt); }
    void setCredentialPrompt(CredentialPrompt prompt) { m_credentialPrompt = std::move(prompt); }

    /// Resolve, connect, handshake, verify the host key and authenticate.
    [[nodiscard]] Status open();

    /// Send a disconnect message and tear everything down. Safe to call twice.
    void close();

    [[nodiscard]] bool isOpen() const noexcept { return m_session != nullptr && m_socket >= 0; }

    [[nodiscard]] LIBSSH2_SESSION *session() const noexcept { return m_session; }
    [[nodiscard]] int socketDescriptor() const noexcept { return m_socket; }
    [[nodiscard]] const HostProfile &profile() const noexcept { return m_profile; }

    /// Blocking mode is used for setup and for SFTP; the interactive shell
    /// switches the session to non-blocking so reads can be event driven.
    void setBlocking(bool blocking);

    /// Wait until the socket is ready for whatever libssh2 last blocked on.
    /// Returns false on timeout. Used to drive EAGAIN retry loops.
    [[nodiscard]] bool waitSocket(int timeoutMs = 250) const;

    /// Build an `Error` from the session's last error string.
    [[nodiscard]] Error lastError(ErrorKind kind, const QString &context) const;

    /// The authentication method that actually succeeded.
    [[nodiscard]] AuthMethod usedAuthMethod() const noexcept { return m_usedAuth; }

    /// Server banner, if the host sent one.
    [[nodiscard]] QString banner() const { return m_banner; }

private:
    [[nodiscard]] Status openSocket();
    [[nodiscard]] Status handshake();
    [[nodiscard]] Status verifyHostKey();
    [[nodiscard]] Status authenticate();

    [[nodiscard]] Status authAgent();
    [[nodiscard]] Status authPublicKey();
    [[nodiscard]] Status authPassword();
    [[nodiscard]] Status authKeyboardInteractive();

    /// Resolve the password lazily so we only prompt when a method needs it.
    [[nodiscard]] std::optional<QString> resolvePassword();
    [[nodiscard]] std::optional<QString> resolvePassphrase();

    HostProfile m_profile;
    HostKeyPrompt m_hostKeyPrompt;
    CredentialPrompt m_credentialPrompt;

    int m_socket{-1};
    LIBSSH2_SESSION *m_session{nullptr};
    AuthMethod m_usedAuth{AuthMethod::Agent};
    QString m_banner;

    std::optional<QString> m_cachedPassword;
    std::optional<QString> m_cachedPassphrase;

    /// Consumed by the keyboard-interactive callback through the session
    /// abstract pointer.
    std::string m_kbdResponse;
};

} // namespace arterm::ssh
