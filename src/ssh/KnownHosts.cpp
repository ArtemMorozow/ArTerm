#include "ssh/KnownHosts.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <libssh2.h>

namespace arterm::ssh {
namespace {

/// Translate a LIBSSH2_HOSTKEY_TYPE_* into the matching knownhost key bit.
int knownHostKeyBit(int hostKeyType)
{
    switch (hostKeyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS:
        return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        return LIBSSH2_KNOWNHOST_KEY_ED25519;
    default:
        return 0;
    }
}

} // namespace

KnownHosts::KnownHosts()
    : m_filePath(QDir::homePath() + QLatin1String("/.ssh/known_hosts"))
{
}

KnownHosts::KnownHosts(QString filePath)
    : m_filePath(std::move(filePath))
{
}

QByteArray KnownHosts::knownHostsHost(const QString &hostname, quint16 port)
{
    if (port == 22)
        return hostname.toUtf8();
    return QStringLiteral("[%1]:%2").arg(hostname).arg(port).toUtf8();
}

QString KnownHosts::keyTypeName(int libssh2HostKeyType)
{
    switch (libssh2HostKeyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        return QStringLiteral("ssh-rsa");
    case LIBSSH2_HOSTKEY_TYPE_DSS:
        return QStringLiteral("ssh-dss");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        return QStringLiteral("ecdsa-sha2-nistp256");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        return QStringLiteral("ecdsa-sha2-nistp384");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        return QStringLiteral("ecdsa-sha2-nistp521");
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        return QStringLiteral("ssh-ed25519");
    default:
        return QObject::tr("unknown");
    }
}

QString KnownHosts::sha256Fingerprint(LIBSSH2_SESSION *session)
{
    const char *hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (hash == nullptr)
        return {};

    const QByteArray digest(hash, 32);
    // OpenSSH prints unpadded base64.
    QByteArray encoded = digest.toBase64();
    while (encoded.endsWith('='))
        encoded.chop(1);

    return QLatin1String("SHA256:") + QString::fromLatin1(encoded);
}

QString KnownHosts::md5Fingerprint(LIBSSH2_SESSION *session)
{
    const char *hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_MD5);
    if (hash == nullptr)
        return {};

    QStringList parts;
    parts.reserve(16);
    for (int i = 0; i < 16; ++i) {
        parts << QStringLiteral("%1").arg(static_cast<unsigned char>(hash[i]), 2, 16, QLatin1Char('0'));
    }
    return QLatin1String("MD5:") + parts.join(QLatin1Char(':'));
}

Result<HostKeyInfo> KnownHosts::check(LIBSSH2_SESSION *session, const QString &hostname,
                                      quint16 port) const
{
    HostKeyInfo info;
    info.hostname = hostname;
    info.port = port;

    size_t keyLength = 0;
    int keyType = 0;
    const char *key = libssh2_session_hostkey(session, &keyLength, &keyType);
    if (key == nullptr) {
        return fail(ErrorKind::HostKey,
                    QObject::tr("The server did not present a host key"));
    }

    info.rawKey = QByteArray(key, static_cast<qsizetype>(keyLength));
    info.rawKeyType = keyType;
    info.keyType = keyTypeName(keyType);
    info.sha256 = sha256Fingerprint(session);
    info.md5 = md5Fingerprint(session);

    LIBSSH2_KNOWNHOSTS *hosts = libssh2_knownhost_init(session);
    if (hosts == nullptr) {
        info.verdict = HostKeyVerdict::Unusable;
        return info;
    }

    struct HostsGuard {
        LIBSSH2_KNOWNHOSTS *hosts;
        ~HostsGuard() { libssh2_knownhost_free(hosts); }
    } guard{hosts};

    const QByteArray filePath = m_filePath.toUtf8();
    const int readCount = libssh2_knownhost_readfile(hosts, filePath.constData(),
                                                     LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    if (readCount < 0 && QFile::exists(m_filePath)) {
        // The file exists but libssh2 refused to parse it.
        info.verdict = HostKeyVerdict::Unusable;
        return info;
    }

    const QByteArray host = hostname.toUtf8();
    libssh2_knownhost *match = nullptr;
    const int rc = libssh2_knownhost_checkp(
        hosts, host.constData(), port,
        key, keyLength,
        LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
        &match);

    switch (rc) {
    case LIBSSH2_KNOWNHOST_CHECK_MATCH:
        info.verdict = HostKeyVerdict::Match;
        break;
    case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
        info.verdict = HostKeyVerdict::Mismatch;
        break;
    case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
        info.verdict = HostKeyVerdict::Unknown;
        break;
    default:
        info.verdict = HostKeyVerdict::Unusable;
        break;
    }

    return info;
}

Status KnownHosts::store(LIBSSH2_SESSION *session, const HostKeyInfo &info) const
{
    LIBSSH2_KNOWNHOSTS *hosts = libssh2_knownhost_init(session);
    if (hosts == nullptr)
        return fail(ErrorKind::HostKey, QObject::tr("Cannot open the known_hosts database"));

    struct HostsGuard {
        LIBSSH2_KNOWNHOSTS *hosts;
        ~HostsGuard() { libssh2_knownhost_free(hosts); }
    } guard{hosts};

    const QByteArray filePath = m_filePath.toUtf8();
    libssh2_knownhost_readfile(hosts, filePath.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    const QByteArray host = knownHostsHost(info.hostname, info.port);

    // On a rotation the stale entry must go first, otherwise the file keeps two
    // conflicting keys and every later check reports a mismatch.
    if (info.verdict == HostKeyVerdict::Mismatch) {
        libssh2_knownhost *stale = nullptr;
        while (libssh2_knownhost_checkp(hosts, info.hostname.toUtf8().constData(), info.port,
                                        info.rawKey.constData(),
                                        static_cast<size_t>(info.rawKey.size()),
                                        LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                        &stale)
                   == LIBSSH2_KNOWNHOST_CHECK_MISMATCH
               && stale != nullptr) {
            if (libssh2_knownhost_del(hosts, stale) != 0)
                break;
            stale = nullptr;
        }
    }

    const int typeMask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW
                         | knownHostKeyBit(info.rawKeyType);

    const int rc = libssh2_knownhost_addc(hosts, host.constData(), nullptr,
                                          info.rawKey.constData(),
                                          static_cast<size_t>(info.rawKey.size()),
                                          "added by ArTerm", 15, typeMask, nullptr);
    if (rc != 0)
        return fail(ErrorKind::HostKey, QObject::tr("Cannot add the host key"), rc);

    // Make sure ~/.ssh exists with the permissions OpenSSH insists on.
    const QFileInfo fileInfo(m_filePath);
    QDir().mkpath(fileInfo.absolutePath());
    QFile::setPermissions(fileInfo.absolutePath(),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    const int written = libssh2_knownhost_writefile(hosts, filePath.constData(),
                                                    LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    if (written != 0)
        return fail(ErrorKind::HostKey, QObject::tr("Cannot write %1").arg(m_filePath), written);

    QFile::setPermissions(m_filePath, QFile::ReadOwner | QFile::WriteOwner);
    return {};
}

Status KnownHosts::remove(LIBSSH2_SESSION *session, const QString &hostname, quint16 port) const
{
    LIBSSH2_KNOWNHOSTS *hosts = libssh2_knownhost_init(session);
    if (hosts == nullptr)
        return fail(ErrorKind::HostKey, QObject::tr("Cannot open the known_hosts database"));

    struct HostsGuard {
        LIBSSH2_KNOWNHOSTS *hosts;
        ~HostsGuard() { libssh2_knownhost_free(hosts); }
    } guard{hosts};

    const QByteArray filePath = m_filePath.toUtf8();
    if (libssh2_knownhost_readfile(hosts, filePath.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) < 0)
        return fail(ErrorKind::HostKey, QObject::tr("Cannot read %1").arg(m_filePath));

    const QByteArray wanted = knownHostsHost(hostname, port);

    libssh2_knownhost *entry = nullptr;
    libssh2_knownhost *previous = nullptr;
    bool removedAny = false;

    while (libssh2_knownhost_get(hosts, &entry, previous) == 0) {
        libssh2_knownhost *current = entry;
        previous = entry;

        if (current->name != nullptr && wanted == current->name) {
            if (libssh2_knownhost_del(hosts, current) == 0) {
                removedAny = true;
                // The deleted node can no longer be used as an iteration cursor.
                previous = nullptr;
            }
        }
    }

    if (!removedAny)
        return {};

    const int written = libssh2_knownhost_writefile(hosts, filePath.constData(),
                                                    LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    if (written != 0)
        return fail(ErrorKind::HostKey, QObject::tr("Cannot write %1").arg(m_filePath), written);

    return {};
}

} // namespace arterm::ssh
