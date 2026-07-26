#include "ssh/ssh_library.hpp"

#include <QLoggingCategory>

#include <libssh2.h>

Q_DECLARE_LOGGING_CATEGORY( lc_ssh )

namespace arterm::ssh
{

	SshLibrary& SshLibrary::instance(){
		static SshLibrary library;
		return library;
	}

	void SshLibrary::ensure_initialised(){
		std::call_once( _once, [this]{
			int const rc = libssh2_init( 0 );
			_initialised = ( rc == 0 );
			if( !_initialised )
				qCCritical( lc_ssh ) << "libssh2_init failed with code" << rc;
			else
				qCDebug( lc_ssh ) << "libssh2" << LIBSSH2_VERSION << "initialised";
		} );
	}

	SshLibrary::~SshLibrary(){
		if( _initialised )
			libssh2_exit();
	}

} // namespace arterm::ssh
