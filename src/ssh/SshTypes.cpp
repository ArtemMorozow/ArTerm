#include "ssh/SshTypes.hpp"

#include <QObject>

#include <sys/stat.h>

namespace arterm::ssh {

QString authMethodName(AuthMethod method)
{
    switch (method) {
    case AuthMethod::Agent:
        return QObject::tr("SSH agent");
    case AuthMethod::PublicKey:
        return QObject::tr("Public key");
    case AuthMethod::Password:
        return QObject::tr("Password");
    case AuthMethod::KeyboardInteractive:
        return QObject::tr("Keyboard interactive");
    }
    return QObject::tr("Unknown");
}

QString transferBackendName(TransferBackend backend)
{
    switch (backend) {
    case TransferBackend::Sftp:
        return QObject::tr("SFTP");
    case TransferBackend::Scp:
        return QObject::tr("SCP");
    }
    return QObject::tr("SFTP");
}

QVector<AuthMethod> HostProfile::authOrder() const
{
    QVector<AuthMethod> order;
    order.reserve(4);

    const auto push = [&order](AuthMethod method) {
        if (!order.contains(method))
            order.append(method);
    };

    push(preferredAuth);

    // Fall back through the remaining methods that this profile can satisfy.
    if (useAgent)
        push(AuthMethod::Agent);
    if (!privateKeyPath.isEmpty())
        push(AuthMethod::PublicKey);
    if (!password.isEmpty()) {
        push(AuthMethod::Password);
        push(AuthMethod::KeyboardInteractive);
    }

    // Drop the preferred method again if the profile cannot actually satisfy it,
    // otherwise we waste a round trip on every connect.
    if (!useAgent)
        order.removeAll(AuthMethod::Agent);
    if (privateKeyPath.isEmpty())
        order.removeAll(AuthMethod::PublicKey);

    if (order.isEmpty())
        order.append(AuthMethod::Password);

    return order;
}

QString RemoteFileEntry::permissionString() const
{
    QString result;
    result.reserve(10);

    if (isSymlink)
        result += QLatin1Char('l');
    else if (isDirectory)
        result += QLatin1Char('d');
    else
        result += QLatin1Char('-');

    static constexpr quint32 bits[9] = {S_IRUSR, S_IWUSR, S_IXUSR,
                                        S_IRGRP, S_IWGRP, S_IXGRP,
                                        S_IROTH, S_IWOTH, S_IXOTH};
    static constexpr char letters[9] = {'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x'};

    for (int i = 0; i < 9; ++i)
        result += (permissions & bits[i]) ? QLatin1Char(letters[i]) : QLatin1Char('-');

    return result;
}

} // namespace arterm::ssh
