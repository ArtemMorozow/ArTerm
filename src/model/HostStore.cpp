#include "model/HostStore.hpp"

#include "model/SecretStore.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

Q_LOGGING_CATEGORY(lcHosts, "arterm.hosts")

namespace arterm::model {
namespace {

QString authMethodKey(ssh::AuthMethod method)
{
    switch (method) {
    case ssh::AuthMethod::Agent:
        return QStringLiteral("agent");
    case ssh::AuthMethod::PublicKey:
        return QStringLiteral("publickey");
    case ssh::AuthMethod::Password:
        return QStringLiteral("password");
    case ssh::AuthMethod::KeyboardInteractive:
        return QStringLiteral("keyboard-interactive");
    }
    return QStringLiteral("agent");
}

QString transferBackendKey(ssh::TransferBackend backend)
{
    return backend == ssh::TransferBackend::Scp ? QStringLiteral("scp") : QStringLiteral("sftp");
}

ssh::TransferBackend transferBackendFromKey(const QString &key)
{
    return key == QLatin1String("scp") ? ssh::TransferBackend::Scp : ssh::TransferBackend::Sftp;
}

ssh::AuthMethod authMethodFromKey(const QString &key)
{
    if (key == QLatin1String("publickey"))
        return ssh::AuthMethod::PublicKey;
    if (key == QLatin1String("password"))
        return ssh::AuthMethod::Password;
    if (key == QLatin1String("keyboard-interactive"))
        return ssh::AuthMethod::KeyboardInteractive;
    return ssh::AuthMethod::Agent;
}

QJsonObject toJson(const ssh::HostProfile &profile)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), profile.id);
    object.insert(QStringLiteral("label"), profile.label);
    object.insert(QStringLiteral("hostname"), profile.hostname);
    object.insert(QStringLiteral("port"), static_cast<int>(profile.port));
    object.insert(QStringLiteral("username"), profile.username);
    object.insert(QStringLiteral("group"), profile.group);
    object.insert(QStringLiteral("colorTag"), profile.colorTag);
    object.insert(QStringLiteral("auth"), authMethodKey(profile.preferredAuth));
    object.insert(QStringLiteral("privateKeyPath"), profile.privateKeyPath);
    object.insert(QStringLiteral("useAgent"), profile.useAgent);
    object.insert(QStringLiteral("startupDirectory"), profile.startupDirectory);
    object.insert(QStringLiteral("startupCommand"), profile.startupCommand);
    object.insert(QStringLiteral("openFileBrowser"), profile.openFileBrowser);
    object.insert(QStringLiteral("keepAliveSeconds"), profile.keepAliveSeconds);
    object.insert(QStringLiteral("compression"), profile.compression);
    object.insert(QStringLiteral("strictHostKeyChecking"), profile.strictHostKeyChecking);
    object.insert(QStringLiteral("transferBackend"), transferBackendKey(profile.transferBackend));
    // Passwords and passphrases are intentionally absent: they belong to the
    // keychain, keyed by `id`.
    return object;
}

ssh::HostProfile fromJson(const QJsonObject &object)
{
    ssh::HostProfile profile;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.label = object.value(QStringLiteral("label")).toString();
    profile.hostname = object.value(QStringLiteral("hostname")).toString();
    profile.port = static_cast<quint16>(object.value(QStringLiteral("port")).toInt(22));
    profile.username = object.value(QStringLiteral("username")).toString();
    profile.group = object.value(QStringLiteral("group")).toString();
    profile.colorTag = object.value(QStringLiteral("colorTag")).toString();
    profile.preferredAuth = authMethodFromKey(object.value(QStringLiteral("auth")).toString());
    profile.privateKeyPath = object.value(QStringLiteral("privateKeyPath")).toString();
    profile.useAgent = object.value(QStringLiteral("useAgent")).toBool(true);
    profile.startupDirectory = object.value(QStringLiteral("startupDirectory")).toString();
    profile.startupCommand = object.value(QStringLiteral("startupCommand")).toString();
    profile.openFileBrowser = object.value(QStringLiteral("openFileBrowser")).toBool(true);
    profile.keepAliveSeconds = object.value(QStringLiteral("keepAliveSeconds")).toInt(30);
    profile.compression = object.value(QStringLiteral("compression")).toBool(false);
    profile.strictHostKeyChecking = object.value(QStringLiteral("strictHostKeyChecking")).toBool(true);
    profile.transferBackend =
        transferBackendFromKey(object.value(QStringLiteral("transferBackend")).toString());
    return profile;
}

} // namespace

HostStore::HostStore(QObject *parent)
    : QObject(parent)
    , m_filePath(defaultFilePath())
{
}

QString HostStore::defaultFilePath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return directory + QLatin1String("/hosts.json");
}

int HostStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == id)
            return i;
    }
    return -1;
}

std::optional<ssh::HostProfile> HostStore::profileById(const QString &id) const
{
    const int index = indexOf(id);
    if (index < 0)
        return std::nullopt;
    return m_profiles.at(index);
}

QStringList HostStore::groups() const
{
    QStringList result;
    for (const ssh::HostProfile &profile : m_profiles) {
        const QString group = profile.group.isEmpty() ? tr("Hosts") : profile.group;
        if (!result.contains(group))
            result << group;
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString HostStore::add(ssh::HostProfile profile)
{
    if (profile.id.isEmpty())
        profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    saveSecrets(profile);
    profile.password.clear();
    profile.keyPassphrase.clear();

    m_profiles.append(profile);

    if (!save())
        qCWarning(lcHosts) << "cannot persist the host list";

    Q_EMIT profileAdded(profile.id);
    Q_EMIT changed();
    return profile.id;
}

void HostStore::update(const ssh::HostProfile &profile)
{
    const int index = indexOf(profile.id);
    if (index < 0)
        return;

    saveSecrets(profile);

    ssh::HostProfile stored = profile;
    stored.password.clear();
    stored.keyPassphrase.clear();
    m_profiles[index] = stored;

    if (!save())
        qCWarning(lcHosts) << "cannot persist the host list";

    Q_EMIT profileUpdated(profile.id);
    Q_EMIT changed();
}

void HostStore::remove(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0)
        return;

    m_profiles.removeAt(index);
    SecretStore::removeAll(id);

    if (!save())
        qCWarning(lcHosts) << "cannot persist the host list";

    Q_EMIT profileRemoved(id);
    Q_EMIT changed();
}

void HostStore::saveSecrets(const ssh::HostProfile &profile)
{
    if (!SecretStore::isAvailable())
        return;

    if (!profile.password.isEmpty())
        static_cast<void>(SecretStore::store(profile.id, QLatin1String(SecretStore::kPassword),
                                             profile.password));
    if (!profile.keyPassphrase.isEmpty())
        static_cast<void>(SecretStore::store(profile.id, QLatin1String(SecretStore::kPassphrase),
                                             profile.keyPassphrase));
}

ssh::HostProfile HostStore::withSecrets(const QString &id) const
{
    const int index = indexOf(id);
    if (index < 0)
        return {};

    ssh::HostProfile profile = m_profiles.at(index);

    if (const auto password = SecretStore::retrieve(id, QLatin1String(SecretStore::kPassword)))
        profile.password = *password;
    if (const auto passphrase = SecretStore::retrieve(id, QLatin1String(SecretStore::kPassphrase)))
        profile.keyPassphrase = *passphrase;

    return profile;
}

bool HostStore::load()
{
    QFile file(m_filePath);
    if (!file.exists())
        return true; // A fresh installation, not an error.

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcHosts) << "cannot read" << m_filePath << file.errorString();
        return false;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(lcHosts) << "cannot parse" << m_filePath << error.errorString();
        return false;
    }

    const QJsonArray array = document.isArray()
                                 ? document.array()
                                 : document.object().value(QStringLiteral("hosts")).toArray();

    m_profiles.clear();
    m_profiles.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        ssh::HostProfile profile = fromJson(value.toObject());
        if (profile.id.isEmpty())
            profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!profile.hostname.isEmpty())
            m_profiles.append(std::move(profile));
    }

    Q_EMIT changed();
    return true;
}

bool HostStore::save() const
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());

    QJsonArray array;
    for (const ssh::HostProfile &profile : m_profiles)
        array.append(toJson(profile));

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("hosts"), array);

    // QSaveFile writes to a temporary and renames, so a crash mid-write cannot
    // leave the user without a host list.
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcHosts) << "cannot open" << m_filePath << file.errorString();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcHosts) << "cannot commit" << m_filePath << file.errorString();
        return false;
    }

    QFile::setPermissions(m_filePath, QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

int HostStore::importFromSshConfig(const QString &configPath)
{
    const QString path = configPath.isEmpty()
                             ? QDir::homePath() + QLatin1String("/.ssh/config")
                             : configPath;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    QTextStream stream(&file);

    int imported = 0;
    ssh::HostProfile current;
    bool inHostBlock = false;

    const auto flush = [&] {
        if (!inHostBlock || current.hostname.isEmpty())
            return;

        // Skip hosts already known by endpoint so a second import is a no-op.
        const bool duplicate = std::any_of(m_profiles.begin(), m_profiles.end(),
                                           [&current](const ssh::HostProfile &existing) {
                                               return existing.hostname == current.hostname
                                                      && existing.port == current.port
                                                      && existing.username == current.username;
                                           });
        if (duplicate) {
            current = {};
            return;
        }

        current.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        current.group = tr("Imported");
        m_profiles.append(current);
        ++imported;
        current = {};
    };

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        // Keywords are case-insensitive and separated by whitespace or '='.
        line.replace(QLatin1Char('='), QLatin1Char(' '));
        const QString keyword = line.section(QLatin1Char(' '), 0, 0).toLower();
        const QString value = line.section(QLatin1Char(' '), 1).trimmed();
        if (value.isEmpty())
            continue;

        if (keyword == QLatin1String("host")) {
            flush();
            // Patterns cannot be connected to, so they are not imported.
            if (value.contains(QLatin1Char('*')) || value.contains(QLatin1Char('?'))) {
                inHostBlock = false;
                continue;
            }
            inHostBlock = true;
            current = {};
            current.label = value.section(QLatin1Char(' '), 0, 0);
            current.hostname = current.label;
            current.username = qEnvironmentVariable("USER");
        } else if (!inHostBlock) {
            continue;
        } else if (keyword == QLatin1String("hostname")) {
            current.hostname = value;
        } else if (keyword == QLatin1String("user")) {
            current.username = value;
        } else if (keyword == QLatin1String("port")) {
            current.port = static_cast<quint16>(value.toUInt());
        } else if (keyword == QLatin1String("identityfile")) {
            current.privateKeyPath = value;
            current.preferredAuth = ssh::AuthMethod::PublicKey;
        } else if (keyword == QLatin1String("compression")) {
            current.compression = value.compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0;
        }
    }

    flush();

    if (imported > 0) {
        static_cast<void>(save());
        Q_EMIT changed();
    }

    return imported;
}

} // namespace arterm::model
