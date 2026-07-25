#include "ssh/SshLibrary.hpp"

#include <QLoggingCategory>

#include <libssh2.h>

Q_DECLARE_LOGGING_CATEGORY(lcSsh)

namespace arterm::ssh {

SshLibrary &SshLibrary::instance()
{
    static SshLibrary library;
    return library;
}

void SshLibrary::ensureInitialised()
{
    std::call_once(m_once, [this] {
        const int rc = libssh2_init(0);
        m_initialised = (rc == 0);
        if (!m_initialised)
            qCCritical(lcSsh) << "libssh2_init failed with code" << rc;
        else
            qCDebug(lcSsh) << "libssh2" << LIBSSH2_VERSION << "initialised";
    });
}

SshLibrary::~SshLibrary()
{
    if (m_initialised)
        libssh2_exit();
}

} // namespace arterm::ssh
