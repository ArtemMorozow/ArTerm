#pragma once

#include <QString>

#include <optional>

namespace arterm::model {

/// Stores passwords and key passphrases outside the profile file.
///
/// On macOS this is the login keychain, reached through Security.framework's C
/// API, so secrets are protected by the same mechanism the system uses and are
/// never written to disk by ArTerm itself. On other platforms the store reports
/// itself unavailable and ArTerm falls back to prompting on every connection -
/// deliberately, because writing a password to a plain file would be worse than
/// asking for it.
class SecretStore {
public:
    /// True when a real secure store backs this instance.
    [[nodiscard]] static bool isAvailable();

    /// Human-readable name of the backing store, for the settings UI.
    [[nodiscard]] static QString backendName();

    /// `account` is the host profile id; `kind` distinguishes the password from
    /// the key passphrase.
    [[nodiscard]] static bool store(const QString &account, const QString &kind,
                                    const QString &secret);

    [[nodiscard]] static std::optional<QString> retrieve(const QString &account, const QString &kind);

    static bool remove(const QString &account, const QString &kind);

    /// Drops every secret belonging to a profile.
    static void removeAll(const QString &account);

    static constexpr const char *kPassword = "password";
    static constexpr const char *kPassphrase = "passphrase";
};

} // namespace arterm::model
