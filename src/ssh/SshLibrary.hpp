#pragma once

#include <mutex>

namespace arterm::ssh {

/// Process-wide libssh2 init/teardown. `libssh2_init` is not thread safe and
/// must run exactly once before any session is created.
class SshLibrary {
public:
    static SshLibrary &instance();

    void ensureInitialised();
    [[nodiscard]] bool isInitialised() const noexcept { return m_initialised; }

    SshLibrary(const SshLibrary &) = delete;
    SshLibrary &operator=(const SshLibrary &) = delete;

private:
    SshLibrary() = default;
    ~SshLibrary();

    std::once_flag m_once;
    bool m_initialised{false};
};

} // namespace arterm::ssh
