#include "ssh/ssh_library.hpp"

#include "core/log.hpp"

#include <libssh2.h>

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
				log_error( "ssh", "libssh2_init failed with code {}", rc );
			else
				log_debug( "ssh", "libssh2 {} initialised", LIBSSH2_VERSION );
		} );
	}

	SshLibrary::~SshLibrary(){
		if( _initialised )
			libssh2_exit();
	}

} // namespace arterm::ssh
