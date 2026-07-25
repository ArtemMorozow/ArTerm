#pragma once

#include "ssh/SshTypes.hpp"

#include <QObject>
#include <QVector>

namespace arterm::model {

/// The saved host list.
///
/// Profiles live in a JSON file under the application data directory; secrets
/// never appear in it and are looked up from `SecretStore` by profile id when a
/// connection is made.
class HostStore : public QObject {
    Q_OBJECT

public:
    explicit HostStore(QObject *parent = nullptr);

    /// Default location: ~/Library/Application Support/ArTerm/hosts.json.
    [[nodiscard]] static QString defaultFilePath();

    [[nodiscard]] const QVector<ssh::HostProfile> &profiles() const noexcept { return m_profiles; }
    [[nodiscard]] int count() const noexcept { return static_cast<int>(m_profiles.size()); }

    [[nodiscard]] std::optional<ssh::HostProfile> profileById(const QString &id) const;

    /// The group names present, in the order they should be shown.
    [[nodiscard]] QStringList groups() const;

    /// Adds a profile, assigning an id when it has none. Returns the id.
    QString add(ssh::HostProfile profile);
    void update(const ssh::HostProfile &profile);
    void remove(const QString &id);

    /// Loads secrets from the keychain into a copy of the profile, ready to be
    /// handed to a connection.
    [[nodiscard]] ssh::HostProfile withSecrets(const QString &id) const;

    /// Persists `profile`'s secrets and strips them from the stored copy.
    void saveSecrets(const ssh::HostProfile &profile);

    [[nodiscard]] bool load();
    [[nodiscard]] bool save() const;

    /// Imports hosts from ~/.ssh/config. Returns how many were added.
    int importFromSshConfig(const QString &configPath = {});

Q_SIGNALS:
    void changed();
    void profileAdded(const QString &id);
    void profileUpdated(const QString &id);
    void profileRemoved(const QString &id);

private:
    [[nodiscard]] int indexOf(const QString &id) const;

    QVector<ssh::HostProfile> m_profiles;
    QString m_filePath;
};

} // namespace arterm::model
