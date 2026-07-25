#include "model/SecretStore.hpp"

#include <QLoggingCategory>
#include <QObject>

#ifdef Q_OS_MACOS
#include <Security/Security.h>
#endif

Q_LOGGING_CATEGORY(lcSecrets, "arterm.secrets")

namespace arterm::model {
namespace {

// Only the Keychain implementation needs these; on other platforms the store
// reports itself unavailable and every entry point is a stub.
#ifdef Q_OS_MACOS

/// Keychain service name; the account is "<profile id>/<kind>" so a single
/// profile can hold both a password and a passphrase.
constexpr char kServiceName[] = "ArTerm";

QByteArray accountKey(const QString &account, const QString &kind)
{
    return (account + QLatin1Char('/') + kind).toUtf8();
}

#endif

} // namespace

bool SecretStore::isAvailable()
{
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

QString SecretStore::backendName()
{
#ifdef Q_OS_MACOS
    return QObject::tr("macOS Keychain");
#else
    return QObject::tr("none (secrets are not saved)");
#endif
}

#ifdef Q_OS_MACOS

bool SecretStore::store(const QString &account, const QString &kind, const QString &secret)
{
    const QByteArray key = accountKey(account, kind);
    const QByteArray value = secret.toUtf8();

    // Replace any existing item rather than accumulating duplicates.
    SecKeychainItemRef existing = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr, static_cast<UInt32>(sizeof(kServiceName) - 1), kServiceName,
        static_cast<UInt32>(key.size()), key.constData(), nullptr, nullptr, &existing);

    if (status == errSecSuccess && existing != nullptr) {
        status = SecKeychainItemModifyAttributesAndData(existing, nullptr,
                                                        static_cast<UInt32>(value.size()),
                                                        value.constData());
        CFRelease(existing);
    } else {
        status = SecKeychainAddGenericPassword(
            nullptr, static_cast<UInt32>(sizeof(kServiceName) - 1), kServiceName,
            static_cast<UInt32>(key.size()), key.constData(), static_cast<UInt32>(value.size()),
            value.constData(), nullptr);
    }

    if (status != errSecSuccess) {
        qCWarning(lcSecrets) << "cannot store secret for" << account << "status" << status;
        return false;
    }
    return true;
}

std::optional<QString> SecretStore::retrieve(const QString &account, const QString &kind)
{
    const QByteArray key = accountKey(account, kind);

    UInt32 length = 0;
    void *data = nullptr;

    const OSStatus status = SecKeychainFindGenericPassword(
        nullptr, static_cast<UInt32>(sizeof(kServiceName) - 1), kServiceName,
        static_cast<UInt32>(key.size()), key.constData(), &length, &data, nullptr);

    if (status != errSecSuccess || data == nullptr)
        return std::nullopt;

    const QString secret = QString::fromUtf8(static_cast<const char *>(data),
                                             static_cast<qsizetype>(length));
    SecKeychainItemFreeContent(nullptr, data);
    return secret;
}

bool SecretStore::remove(const QString &account, const QString &kind)
{
    const QByteArray key = accountKey(account, kind);

    SecKeychainItemRef item = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(
        nullptr, static_cast<UInt32>(sizeof(kServiceName) - 1), kServiceName,
        static_cast<UInt32>(key.size()), key.constData(), nullptr, nullptr, &item);

    if (status != errSecSuccess || item == nullptr)
        return false;

    const OSStatus deleted = SecKeychainItemDelete(item);
    CFRelease(item);
    return deleted == errSecSuccess;
}

#else // Not macOS.

bool SecretStore::store(const QString &account, const QString &, const QString &)
{
    qCInfo(lcSecrets) << "no secure store on this platform; not saving the secret for" << account;
    return false;
}

std::optional<QString> SecretStore::retrieve(const QString &, const QString &)
{
    return std::nullopt;
}

bool SecretStore::remove(const QString &, const QString &)
{
    return false;
}

#endif

void SecretStore::removeAll(const QString &account)
{
    remove(account, QLatin1String(kPassword));
    remove(account, QLatin1String(kPassphrase));
}

} // namespace arterm::model
