#include "ssh/SshConnection.hpp"

#include "ssh/KnownHosts.hpp"
#include "ssh/SshLibrary.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <libssh2.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcSsh, "arterm.ssh")

namespace arterm::ssh {
namespace {

constexpr int kConnectTimeoutMs = 15'000;
constexpr int kHandshakeTimeoutMs = 20'000;

/// Keyboard-interactive callback. libssh2 hands us the prompts and expects
/// malloc'd response strings that it will free itself.
void kbdInteractiveCallback(const char *, int, const char *, int, int numPrompts,
                            const LIBSSH2_USERAUTH_KBDINT_PROMPT *,
                            LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses, void **abstract)
{
    const auto *answer = static_cast<const std::string *>(*abstract);
    if (answer == nullptr)
        return;

    for (int i = 0; i < numPrompts; ++i) {
        // Only the first prompt gets the password; extra prompts are answered
        // with an empty string so the exchange still completes.
        const std::string_view value = (i == 0) ? std::string_view{*answer} : std::string_view{};
        auto *buffer = static_cast<char *>(std::malloc(value.size() + 1));
        if (buffer == nullptr) {
            responses[i].text = nullptr;
            responses[i].length = 0;
            continue;
        }
        std::memcpy(buffer, value.data(), value.size());
        buffer[value.size()] = '\0';
        responses[i].text = buffer;
        responses[i].length = static_cast<unsigned int>(value.size());
    }
}

QString expandPath(const QString &path)
{
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

} // namespace

SshConnection::SshConnection(HostProfile profile)
    : m_profile(std::move(profile))
{
    SshLibrary::instance().ensureInitialised();
}

SshConnection::~SshConnection()
{
    close();
}

Status SshConnection::open()
{
    if (auto status = openSocket(); !status)
        return status;
    if (auto status = handshake(); !status) {
        close();
        return status;
    }
    if (auto status = verifyHostKey(); !status) {
        close();
        return status;
    }
    if (auto status = authenticate(); !status) {
        close();
        return status;
    }

    if (const char *banner = libssh2_session_banner_get(m_session))
        m_banner = QString::fromUtf8(banner);

    qCInfo(lcSsh) << "connected to" << m_profile.endpoint() << "as" << m_profile.username
                  << "using" << authMethodName(m_usedAuth);
    return {};
}

Status SshConnection::openSocket()
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const QByteArray host = m_profile.hostname.toUtf8();
    const QByteArray port = QByteArray::number(m_profile.port);

    addrinfo *resolved = nullptr;
    const int rc = ::getaddrinfo(host.constData(), port.constData(), &hints, &resolved);
    if (rc != 0 || resolved == nullptr) {
        return fail(ErrorKind::Network,
                    QObject::tr("Cannot resolve %1: %2")
                        .arg(m_profile.hostname, QString::fromUtf8(::gai_strerror(rc))));
    }

    Error lastFailure{ErrorKind::Network, QObject::tr("No usable address for %1").arg(m_profile.hostname)};

    for (addrinfo *candidate = resolved; candidate != nullptr; candidate = candidate->ai_next) {
        const int sock = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (sock < 0)
            continue;

        // Non-blocking connect so we can enforce our own timeout.
        const int flags = ::fcntl(sock, F_GETFL, 0);
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        bool connected = false;
        if (::connect(sock, candidate->ai_addr, candidate->ai_addrlen) == 0) {
            connected = true;
        } else if (errno == EINPROGRESS) {
            pollfd pfd{sock, POLLOUT, 0};
            if (::poll(&pfd, 1, kConnectTimeoutMs) > 0) {
                int soError = 0;
                socklen_t length = sizeof(soError);
                ::getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &length);
                if (soError == 0) {
                    connected = true;
                } else {
                    lastFailure = Error{ErrorKind::Network,
                                        QObject::tr("Cannot connect to %1: %2")
                                            .arg(m_profile.endpoint(), QString::fromUtf8(::strerror(soError)))};
                }
            } else {
                lastFailure = Error{ErrorKind::Network,
                                    QObject::tr("Connection to %1 timed out").arg(m_profile.endpoint())};
            }
        } else {
            lastFailure = Error{ErrorKind::Network,
                                QObject::tr("Cannot connect to %1: %2")
                                    .arg(m_profile.endpoint(), QString::fromUtf8(::strerror(errno)))};
        }

        if (!connected) {
            ::close(sock);
            continue;
        }

        ::fcntl(sock, F_SETFL, flags);

        int one = 1;
        ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

        m_socket = sock;
        break;
    }

    ::freeaddrinfo(resolved);

    if (m_socket < 0)
        return std::unexpected(lastFailure);
    return {};
}

Status SshConnection::handshake()
{
    m_session = libssh2_session_init();
    if (m_session == nullptr)
        return fail(ErrorKind::Handshake, QObject::tr("Cannot allocate an SSH session"));

    libssh2_session_set_blocking(m_session, 1);
    libssh2_session_set_timeout(m_session, kHandshakeTimeoutMs);
    libssh2_session_banner_set(m_session, "SSH-2.0-ArTerm_" ARTERM_VERSION);

    if (m_profile.compression)
        libssh2_session_flag(m_session, LIBSSH2_FLAG_COMPRESS, 1);

    if (libssh2_session_handshake(m_session, m_socket) != 0)
        return std::unexpected(lastError(ErrorKind::Handshake, QObject::tr("SSH handshake failed")));

    if (m_profile.keepAliveSeconds > 0)
        libssh2_keepalive_config(m_session, 1, static_cast<unsigned>(m_profile.keepAliveSeconds));

    // The handshake timeout is deliberately not kept for the session lifetime:
    // an idle interactive shell would otherwise be torn down.
    libssh2_session_set_timeout(m_session, 0);
    return {};
}

Status SshConnection::verifyHostKey()
{
    KnownHosts knownHosts;
    auto info = knownHosts.check(m_session, m_profile.hostname, m_profile.port);
    if (!info)
        return std::unexpected(info.error());

    switch (info->verdict) {
    case HostKeyVerdict::Match:
        return {};

    case HostKeyVerdict::Unusable:
        if (!m_profile.strictHostKeyChecking) {
            qCWarning(lcSsh) << "known_hosts unreadable, continuing without verification";
            return {};
        }
        return fail(ErrorKind::HostKey,
                    QObject::tr("The known_hosts file could not be read, so the identity of %1 "
                                "cannot be verified.")
                        .arg(m_profile.hostname));

    case HostKeyVerdict::Unknown:
    case HostKeyVerdict::Mismatch:
        break;
    }

    if (!m_hostKeyPrompt) {
        return fail(ErrorKind::HostKey,
                    info->verdict == HostKeyVerdict::Mismatch
                        ? QObject::tr("The host key for %1 has changed.").arg(m_profile.hostname)
                        : QObject::tr("The host key for %1 is not known.").arg(m_profile.hostname));
    }

    if (!m_hostKeyPrompt(*info))
        return std::unexpected(Error{ErrorKind::HostKey, QObject::tr("Host key rejected")});

    if (auto stored = knownHosts.store(m_session, *info); !stored)
        qCWarning(lcSsh) << "cannot persist host key:" << stored.error().message;

    return {};
}

Status SshConnection::authenticate()
{
    const QByteArray user = m_profile.username.toUtf8();

    const char *methods = libssh2_userauth_list(m_session, user.constData(),
                                                static_cast<unsigned int>(user.size()));
    if (methods == nullptr && libssh2_userauth_authenticated(m_session)) {
        // Some servers accept "none" authentication outright.
        m_usedAuth = AuthMethod::Password;
        return {};
    }

    const QString available = QString::fromUtf8(methods == nullptr ? "" : methods);
    qCDebug(lcSsh) << "server offers" << available;

    Error lastFailure{ErrorKind::Authentication,
                      QObject::tr("No authentication method succeeded for %1").arg(m_profile.username)};

    for (const AuthMethod method : m_profile.authOrder()) {
        // Skip methods the server did not advertise, unless it advertised none
        // at all (in which case we simply try everything we have).
        if (!available.isEmpty()) {
            const bool offered = [&] {
                switch (method) {
                case AuthMethod::Agent:
                case AuthMethod::PublicKey:
                    return available.contains(QLatin1String("publickey"));
                case AuthMethod::Password:
                    return available.contains(QLatin1String("password"));
                case AuthMethod::KeyboardInteractive:
                    return available.contains(QLatin1String("keyboard-interactive"));
                }
                return false;
            }();
            if (!offered)
                continue;
        }

        Status result;
        switch (method) {
        case AuthMethod::Agent:
            result = authAgent();
            break;
        case AuthMethod::PublicKey:
            result = authPublicKey();
            break;
        case AuthMethod::Password:
            result = authPassword();
            break;
        case AuthMethod::KeyboardInteractive:
            result = authKeyboardInteractive();
            break;
        }

        if (result) {
            m_usedAuth = method;
            return {};
        }

        if (result.error().isCancellation())
            return result;

        qCDebug(lcSsh) << authMethodName(method) << "failed:" << result.error().message;
        lastFailure = result.error();
    }

    return std::unexpected(lastFailure);
}

Status SshConnection::authAgent()
{
    LIBSSH2_AGENT *agent = libssh2_agent_init(m_session);
    if (agent == nullptr)
        return fail(ErrorKind::Authentication, QObject::tr("Cannot initialise the SSH agent"));

    struct AgentGuard {
        LIBSSH2_AGENT *agent;
        ~AgentGuard()
        {
            libssh2_agent_disconnect(agent);
            libssh2_agent_free(agent);
        }
    } guard{agent};

    if (libssh2_agent_connect(agent) != 0) {
        return fail(ErrorKind::Authentication,
                    QObject::tr("No SSH agent is running (SSH_AUTH_SOCK is unset or stale)"));
    }
    if (libssh2_agent_list_identities(agent) != 0)
        return fail(ErrorKind::Authentication, QObject::tr("The SSH agent has no identities"));

    const QByteArray user = m_profile.username.toUtf8();
    libssh2_agent_publickey *identity = nullptr;
    libssh2_agent_publickey *previous = nullptr;

    while (true) {
        const int rc = libssh2_agent_get_identity(agent, &identity, previous);
        if (rc == 1) // No more identities.
            break;
        if (rc < 0)
            return fail(ErrorKind::Authentication, QObject::tr("Cannot read agent identities"));

        if (libssh2_agent_userauth(agent, user.constData(), identity) == 0)
            return {};

        previous = identity;
    }

    return fail(ErrorKind::Authentication,
                QObject::tr("The SSH agent holds no key accepted by %1").arg(m_profile.hostname));
}

Status SshConnection::authPublicKey()
{
    if (m_profile.privateKeyPath.isEmpty())
        return fail(ErrorKind::Authentication, QObject::tr("No private key configured"));

    const QString privatePath = expandPath(m_profile.privateKeyPath);
    if (!QFileInfo::exists(privatePath)) {
        return fail(ErrorKind::Authentication,
                    QObject::tr("Private key %1 does not exist").arg(privatePath));
    }

    const QString publicPath = privatePath + QLatin1String(".pub");
    const QByteArray privateUtf8 = privatePath.toUtf8();
    const QByteArray publicUtf8 = publicPath.toUtf8();
    const QByteArray user = m_profile.username.toUtf8();

    const bool havePublic = QFileInfo::exists(publicPath);

    // First try without a passphrase - unencrypted keys are the common case and
    // this avoids prompting for nothing.
    int rc = libssh2_userauth_publickey_fromfile_ex(
        m_session, user.constData(), static_cast<unsigned int>(user.size()),
        havePublic ? publicUtf8.constData() : nullptr, privateUtf8.constData(), "");
    if (rc == 0)
        return {};

    if (rc == LIBSSH2_ERROR_FILE || rc == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED) {
        const auto passphrase = resolvePassphrase();
        if (!passphrase)
            return cancelled();

        const QByteArray secret = passphrase->toUtf8();
        rc = libssh2_userauth_publickey_fromfile_ex(
            m_session, user.constData(), static_cast<unsigned int>(user.size()),
            havePublic ? publicUtf8.constData() : nullptr, privateUtf8.constData(),
            secret.constData());
        if (rc == 0)
            return {};
    }

    return std::unexpected(lastError(ErrorKind::Authentication,
                                     QObject::tr("Public key authentication with %1 failed").arg(privatePath)));
}

Status SshConnection::authPassword()
{
    const auto password = resolvePassword();
    if (!password)
        return cancelled();

    const QByteArray user = m_profile.username.toUtf8();
    const QByteArray secret = password->toUtf8();

    const int rc = libssh2_userauth_password_ex(
        m_session, user.constData(), static_cast<unsigned int>(user.size()), secret.constData(),
        static_cast<unsigned int>(secret.size()), nullptr);
    if (rc == 0)
        return {};

    return std::unexpected(lastError(ErrorKind::Authentication, QObject::tr("Password rejected")));
}

Status SshConnection::authKeyboardInteractive()
{
    const auto password = resolvePassword();
    if (!password)
        return cancelled();

    m_kbdResponse = password->toStdString();
    *libssh2_session_abstract(m_session) = &m_kbdResponse;

    const QByteArray user = m_profile.username.toUtf8();
    const int rc = libssh2_userauth_keyboard_interactive_ex(
        m_session, user.constData(), static_cast<unsigned int>(user.size()), &kbdInteractiveCallback);

    *libssh2_session_abstract(m_session) = nullptr;
    m_kbdResponse.clear();

    if (rc == 0)
        return {};

    return std::unexpected(
        lastError(ErrorKind::Authentication, QObject::tr("Keyboard interactive authentication failed")));
}

std::optional<QString> SshConnection::resolvePassword()
{
    if (m_cachedPassword)
        return m_cachedPassword;
    if (!m_profile.password.isEmpty()) {
        m_cachedPassword = m_profile.password;
        return m_cachedPassword;
    }
    if (!m_credentialPrompt)
        return std::nullopt;

    m_cachedPassword = m_credentialPrompt(
        QObject::tr("Password for %1@%2").arg(m_profile.username, m_profile.hostname), false);
    return m_cachedPassword;
}

std::optional<QString> SshConnection::resolvePassphrase()
{
    if (m_cachedPassphrase)
        return m_cachedPassphrase;
    if (!m_profile.keyPassphrase.isEmpty()) {
        m_cachedPassphrase = m_profile.keyPassphrase;
        return m_cachedPassphrase;
    }
    if (!m_credentialPrompt)
        return std::nullopt;

    m_cachedPassphrase = m_credentialPrompt(
        QObject::tr("Passphrase for %1").arg(QFileInfo(expandPath(m_profile.privateKeyPath)).fileName()),
        false);
    return m_cachedPassphrase;
}

void SshConnection::setBlocking(bool blocking)
{
    if (m_session != nullptr)
        libssh2_session_set_blocking(m_session, blocking ? 1 : 0);
}

bool SshConnection::waitSocket(int timeoutMs) const
{
    if (m_socket < 0 || m_session == nullptr)
        return false;

    const int directions = libssh2_session_block_directions(m_session);

    pollfd pfd{m_socket, 0, 0};
    if (directions & LIBSSH2_SESSION_BLOCK_INBOUND)
        pfd.events |= POLLIN;
    if (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        pfd.events |= POLLOUT;
    if (pfd.events == 0)
        pfd.events = POLLIN;

    return ::poll(&pfd, 1, timeoutMs) > 0;
}

Error SshConnection::lastError(ErrorKind kind, const QString &context) const
{
    if (m_session == nullptr)
        return Error{kind, context};

    char *message = nullptr;
    int length = 0;
    const int code = libssh2_session_last_error(m_session, &message, &length, 0);

    QString detail = QString::fromUtf8(message == nullptr ? "" : message, length);
    if (detail.isEmpty())
        detail = QObject::tr("libssh2 error %1").arg(code);

    return Error{kind, context + QLatin1String(": ") + detail, code};
}

void SshConnection::close()
{
    if (m_session != nullptr) {
        libssh2_session_set_blocking(m_session, 1);
        libssh2_session_disconnect(m_session, "ArTerm session closed");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
}

} // namespace arterm::ssh
