#include "ssh/ssh_connection.hpp"

#include "ssh/known_hosts.hpp"
#include "ssh/ssh_library.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include <libssh2.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

Q_LOGGING_CATEGORY( lc_ssh, "arterm.ssh" )

namespace arterm::ssh
{
	namespace
	{

		constexpr int CONNECT_TIMEOUT_MS   = 15'000;
		constexpr int HANDSHAKE_TIMEOUT_MS = 20'000;

		/// Keyboard-interactive callback. libssh2 hands us the prompts and expects
		/// malloc'd response strings that it will free itself.
		void kbd_interactive_callback( char const*, int, char const*, int, int num_prompts,
									   LIBSSH2_USERAUTH_KBDINT_PROMPT const*,
									   LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract ){
			auto const* answer = static_cast<std::string const*>( *abstract );
			if( answer == nullptr )
				return;

			for( int i = 0; i < num_prompts; ++i ){
				// Only the first prompt gets the password; extra prompts are answered
				// with an empty string so the exchange still completes.
				std::string_view const value  = ( i == 0 ) ? std::string_view{ *answer } : std::string_view{};
				auto*                  buffer = static_cast<char*>( std::malloc( value.size() + 1 ) );
				if( buffer == nullptr ){
					responses[i].text   = nullptr;
					responses[i].length = 0;
					continue;
				}
				std::memcpy( buffer, value.data(), value.size() );
				buffer[value.size()] = '\0';
				responses[i].text    = buffer;
				responses[i].length  = static_cast<unsigned int>( value.size() );
			}
		}

		QString expand_path( QString const& path ){
			if( path.startsWith( QLatin1String( "~/" ) ) )
				return QDir::homePath() + path.mid( 1 );
			return path;
		}

	} // namespace

	SshConnection::SshConnection( HostProfile profile )
		: _profile( std::move( profile ) )
	{
		SshLibrary::instance().ensure_initialised();
	}

	SshConnection::~SshConnection(){
		close();
	}

	Status SshConnection::open(){
		if( auto status = open_socket(); !status )
			return status;
		if( auto status = handshake(); !status ){
			close();
			return status;
		}
		if( auto status = verify_host_key(); !status ){
			close();
			return status;
		}
		if( auto status = authenticate(); !status ){
			close();
			return status;
		}

		if( char const* banner = libssh2_session_banner_get( _session ) )
			_banner = QString::fromUtf8( banner );

		qCInfo( lc_ssh ) << "connected to" << _profile.endpoint() << "as" << _profile.username << "using"
						 << auth_method_name( _used_auth );
		return {};
	}

	Status SshConnection::open_socket(){
		addrinfo hints{};
		hints.ai_family   = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		QByteArray const host = _profile.hostname.toUtf8();
		QByteArray const port = QByteArray::number( _profile.port );

		addrinfo* resolved = nullptr;
		int const rc       = ::getaddrinfo( host.constData(), port.constData(), &hints, &resolved );
		if( rc != 0 || resolved == nullptr ){
			return fail( ErrorKind::NETWORK, QObject::tr( "Cannot resolve %1: %2" )
												 .arg( _profile.hostname, QString::fromUtf8( ::gai_strerror( rc ) ) ) );
		}

		Error last_failure{ ErrorKind::NETWORK, QObject::tr( "No usable address for %1" ).arg( _profile.hostname ) };

		for( addrinfo* candidate = resolved; candidate != nullptr; candidate = candidate->ai_next ){
			int const sock = ::socket( candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol );
			if( sock < 0 )
				continue;

			// Non-blocking connect so we can enforce our own timeout.
			int const flags = ::fcntl( sock, F_GETFL, 0 );
			::fcntl( sock, F_SETFL, flags | O_NONBLOCK );

			bool connected = false;
			if( ::connect( sock, candidate->ai_addr, candidate->ai_addrlen ) == 0 ){
				connected = true;
			}
			else if( errno == EINPROGRESS ){
				pollfd pfd{ sock, POLLOUT, 0 };
				if( ::poll( &pfd, 1, CONNECT_TIMEOUT_MS ) > 0 ){
					int       so_error = 0;
					socklen_t length   = sizeof( so_error );
					::getsockopt( sock, SOL_SOCKET, SO_ERROR, &so_error, &length );
					if( so_error == 0 ){
						connected = true;
					}
					else{
						last_failure =
							Error{ ErrorKind::NETWORK,
								   QObject::tr( "Cannot connect to %1: %2" )
									   .arg( _profile.endpoint(), QString::fromUtf8( ::strerror( so_error ) ) ) };
					}
				}
				else{
					last_failure = Error{ ErrorKind::NETWORK,
										  QObject::tr( "Connection to %1 timed out" ).arg( _profile.endpoint() ) };
				}
			}
			else{
				last_failure = Error{ ErrorKind::NETWORK,
									  QObject::tr( "Cannot connect to %1: %2" )
										  .arg( _profile.endpoint(), QString::fromUtf8( ::strerror( errno ) ) ) };
			}

			if( !connected ){
				::close( sock );
				continue;
			}

			::fcntl( sock, F_SETFL, flags );

			int one = 1;
			::setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof( one ) );
			::setsockopt( sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof( one ) );

			_socket = sock;
			break;
		}

		::freeaddrinfo( resolved );

		if( _socket < 0 )
			return std::unexpected( last_failure );
		return {};
	}

	Status SshConnection::handshake(){
		_session = libssh2_session_init();
		if( _session == nullptr )
			return fail( ErrorKind::HANDSHAKE, QObject::tr( "Cannot allocate an SSH session" ) );

		libssh2_session_set_blocking( _session, 1 );
		libssh2_session_set_timeout( _session, HANDSHAKE_TIMEOUT_MS );
		libssh2_session_banner_set( _session, "SSH-2.0-ArTerm_" ARTERM_VERSION );

		if( _profile.compression )
			libssh2_session_flag( _session, LIBSSH2_FLAG_COMPRESS, 1 );

		if( libssh2_session_handshake( _session, _socket ) != 0 )
			return std::unexpected( last_error( ErrorKind::HANDSHAKE, QObject::tr( "SSH handshake failed" ) ) );

		if( _profile.keep_alive_seconds > 0 )
			libssh2_keepalive_config( _session, 1, static_cast<unsigned>( _profile.keep_alive_seconds ) );

		// The handshake timeout is deliberately not kept for the session lifetime:
		// an idle interactive shell would otherwise be torn down.
		libssh2_session_set_timeout( _session, 0 );
		return {};
	}

	Status SshConnection::verify_host_key(){
		KnownHosts known_hosts;
		auto       info = known_hosts.check( _session, _profile.hostname, _profile.port );
		if( !info )
			return std::unexpected( info.error() );

		switch( info->verdict ){
			case HostKeyVerdict::MATCH:
				return {};

			case HostKeyVerdict::UNUSABLE:
				if( !_profile.strict_host_key_checking ){
					qCWarning( lc_ssh ) << "known_hosts unreadable, continuing without verification";
					return {};
				}
				return fail( ErrorKind::HOST_KEY,
							 QObject::tr( "The known_hosts file could not be read, so the identity of %1 "
										  "cannot be verified." )
								 .arg( _profile.hostname ) );

			case HostKeyVerdict::UNKNOWN:
			case HostKeyVerdict::MISMATCH:
				break;
		}

		if( !_host_key_prompt ){
			return fail( ErrorKind::HOST_KEY,
						 info->verdict == HostKeyVerdict::MISMATCH
							 ? QObject::tr( "The host key for %1 has changed." ).arg( _profile.hostname )
							 : QObject::tr( "The host key for %1 is not known." ).arg( _profile.hostname ) );
		}

		if( !_host_key_prompt( *info ) )
			return std::unexpected( Error{ ErrorKind::HOST_KEY, QObject::tr( "Host key rejected" ) } );

		if( auto stored = known_hosts.store( _session, *info ); !stored )
			qCWarning( lc_ssh ) << "cannot persist host key:" << stored.error().message;

		return {};
	}

	Status SshConnection::authenticate(){
		QByteArray const user = _profile.username.toUtf8();

		char const* methods =
			libssh2_userauth_list( _session, user.constData(), static_cast<unsigned int>( user.size() ) );
		if( methods == nullptr && libssh2_userauth_authenticated( _session ) ){
			// Some servers accept "none" authentication outright.
			_used_auth = AuthMethod::PASSWORD;
			return {};
		}

		QString const available = QString::fromUtf8( methods == nullptr ? "" : methods );
		qCDebug( lc_ssh ) << "server offers" << available;

		Error last_failure{ ErrorKind::AUTHENTICATION,
							QObject::tr( "No authentication method succeeded for %1" ).arg( _profile.username ) };

		for( AuthMethod const method : _profile.auth_order() ){
			// Skip methods the server did not advertise, unless it advertised none
			// at all (in which case we simply try everything we have).
			if( !available.isEmpty() ){
				bool const offered = [&]{
					switch( method ){
						case AuthMethod::AGENT:
						case AuthMethod::PUBLIC_KEY:
							return available.contains( QLatin1String( "publickey" ) );
						case AuthMethod::PASSWORD:
							return available.contains( QLatin1String( "password" ) );
						case AuthMethod::KEYBOARD_INTERACTIVE:
							return available.contains( QLatin1String( "keyboard-interactive" ) );
					}
					return false;
				}();
				if( !offered )
					continue;
			}

			Status result;
			switch( method ){
				case AuthMethod::AGENT:
					result = auth_agent();
					break;
				case AuthMethod::PUBLIC_KEY:
					result = auth_public_key();
					break;
				case AuthMethod::PASSWORD:
					result = auth_password();
					break;
				case AuthMethod::KEYBOARD_INTERACTIVE:
					result = auth_keyboard_interactive();
					break;
			}

			if( result ){
				_used_auth = method;
				return {};
			}

			if( result.error().is_cancellation() )
				return result;

			qCDebug( lc_ssh ) << auth_method_name( method ) << "failed:" << result.error().message;
			last_failure = result.error();
		}

		return std::unexpected( last_failure );
	}

	Status SshConnection::auth_agent(){
		LIBSSH2_AGENT* agent = libssh2_agent_init( _session );
		if( agent == nullptr )
			return fail( ErrorKind::AUTHENTICATION, QObject::tr( "Cannot initialise the SSH agent" ) );

		struct AgentGuard
		{
			LIBSSH2_AGENT* agent;
			~AgentGuard(){
				libssh2_agent_disconnect( agent );
				libssh2_agent_free( agent );
			}
		} guard{ agent };

		if( libssh2_agent_connect( agent ) != 0 ){
			return fail( ErrorKind::AUTHENTICATION,
						 QObject::tr( "No SSH agent is running (SSH_AUTH_SOCK is unset or stale)" ) );
		}
		if( libssh2_agent_list_identities( agent ) != 0 )
			return fail( ErrorKind::AUTHENTICATION, QObject::tr( "The SSH agent has no identities" ) );

		QByteArray const         user     = _profile.username.toUtf8();
		libssh2_agent_publickey* identity = nullptr;
		libssh2_agent_publickey* previous = nullptr;

		while( true ){
			int const rc = libssh2_agent_get_identity( agent, &identity, previous );
			if( rc == 1 ) // No more identities.
				break;
			if( rc < 0 )
				return fail( ErrorKind::AUTHENTICATION, QObject::tr( "Cannot read agent identities" ) );

			if( libssh2_agent_userauth( agent, user.constData(), identity ) == 0 )
				return {};

			previous = identity;
		}

		return fail( ErrorKind::AUTHENTICATION,
					 QObject::tr( "The SSH agent holds no key accepted by %1" ).arg( _profile.hostname ) );
	}

	Status SshConnection::auth_public_key(){
		if( _profile.private_key_path.isEmpty() )
			return fail( ErrorKind::AUTHENTICATION, QObject::tr( "No private key configured" ) );

		QString const private_path = expand_path( _profile.private_key_path );
		if( !QFileInfo::exists( private_path ) ){
			return fail( ErrorKind::AUTHENTICATION,
						 QObject::tr( "Private key %1 does not exist" ).arg( private_path ) );
		}

		QString const    public_path  = private_path + QLatin1String( ".pub" );
		QByteArray const private_utf8 = private_path.toUtf8();
		QByteArray const public_utf8  = public_path.toUtf8();
		QByteArray const user         = _profile.username.toUtf8();

		bool const have_public = QFileInfo::exists( public_path );

		// First try without a passphrase - unencrypted keys are the common case and
		// this avoids prompting for nothing.
		int rc = libssh2_userauth_publickey_fromfile_ex(
			_session, user.constData(), static_cast<unsigned int>( user.size() ),
			have_public ? public_utf8.constData() : nullptr, private_utf8.constData(), "" );
		if( rc == 0 )
			return {};

		if( rc == LIBSSH2_ERROR_FILE || rc == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED ){
			auto const passphrase = resolve_passphrase();
			if( !passphrase )
				return cancelled();

			QByteArray const secret = passphrase->toUtf8();
			rc                      = libssh2_userauth_publickey_fromfile_ex(
                _session, user.constData(), static_cast<unsigned int>( user.size() ),
                have_public ? public_utf8.constData() : nullptr, private_utf8.constData(), secret.constData() );
			if( rc == 0 )
				return {};
		}

		return std::unexpected(
			last_error( ErrorKind::AUTHENTICATION,
						QObject::tr( "Public key authentication with %1 failed" ).arg( private_path ) ) );
	}

	Status SshConnection::auth_password(){
		auto const password = resolve_password();
		if( !password )
			return cancelled();

		QByteArray const user   = _profile.username.toUtf8();
		QByteArray const secret = password->toUtf8();

		int const rc =
			libssh2_userauth_password_ex( _session, user.constData(), static_cast<unsigned int>( user.size() ),
										  secret.constData(), static_cast<unsigned int>( secret.size() ), nullptr );
		if( rc == 0 )
			return {};

		return std::unexpected( last_error( ErrorKind::AUTHENTICATION, QObject::tr( "Password rejected" ) ) );
	}

	Status SshConnection::auth_keyboard_interactive(){
		auto const password = resolve_password();
		if( !password )
			return cancelled();

		_kbd_response                         = password->toStdString();
		*libssh2_session_abstract( _session ) = &_kbd_response;

		QByteArray const user = _profile.username.toUtf8();
		int const        rc   = libssh2_userauth_keyboard_interactive_ex(
            _session, user.constData(), static_cast<unsigned int>( user.size() ), &kbd_interactive_callback );

		*libssh2_session_abstract( _session ) = nullptr;
		_kbd_response.clear();

		if( rc == 0 )
			return {};

		return std::unexpected(
			last_error( ErrorKind::AUTHENTICATION, QObject::tr( "Keyboard interactive authentication failed" ) ) );
	}

	std::optional<QString> SshConnection::resolve_password(){
		if( _cached_password )
			return _cached_password;
		if( !_profile.password.isEmpty() ){
			_cached_password = _profile.password;
			return _cached_password;
		}
		if( !_credential_prompt )
			return std::nullopt;

		_cached_password = _credential_prompt(
			QObject::tr( "Password for %1@%2" ).arg( _profile.username, _profile.hostname ), false );
		return _cached_password;
	}

	std::optional<QString> SshConnection::resolve_passphrase(){
		if( _cached_passphrase )
			return _cached_passphrase;
		if( !_profile.key_passphrase.isEmpty() ){
			_cached_passphrase = _profile.key_passphrase;
			return _cached_passphrase;
		}
		if( !_credential_prompt )
			return std::nullopt;

		_cached_passphrase = _credential_prompt(
			QObject::tr( "Passphrase for %1" ).arg( QFileInfo( expand_path( _profile.private_key_path ) ).fileName() ),
			false );
		return _cached_passphrase;
	}

	void SshConnection::set_blocking( bool blocking ){
		if( _session != nullptr )
			libssh2_session_set_blocking( _session, blocking ? 1 : 0 );
	}

	bool SshConnection::wait_socket( int timeout_ms ) const{
		if( _socket < 0 || _session == nullptr )
			return false;

		int const directions = libssh2_session_block_directions( _session );

		pollfd pfd{ _socket, 0, 0 };
		if( directions & LIBSSH2_SESSION_BLOCK_INBOUND )
			pfd.events |= POLLIN;
		if( directions & LIBSSH2_SESSION_BLOCK_OUTBOUND )
			pfd.events |= POLLOUT;
		if( pfd.events == 0 )
			pfd.events = POLLIN;

		return ::poll( &pfd, 1, timeout_ms ) > 0;
	}

	Error SshConnection::last_error( ErrorKind kind, QString const& context ) const{
		if( _session == nullptr )
			return Error{ kind, context };

		char*     message = nullptr;
		int       length  = 0;
		int const code    = libssh2_session_last_error( _session, &message, &length, 0 );

		QString detail = QString::fromUtf8( message == nullptr ? "" : message, length );
		if( detail.isEmpty() )
			detail = QObject::tr( "libssh2 error %1" ).arg( code );

		return Error{ kind, context + QLatin1String( ": " ) + detail, code };
	}

	void SshConnection::close(){
		if( _session != nullptr ){
			libssh2_session_set_blocking( _session, 1 );
			libssh2_session_disconnect( _session, "ArTerm session closed" );
			libssh2_session_free( _session );
			_session = nullptr;
		}
		if( _socket >= 0 ){
			::close( _socket );
			_socket = -1;
		}
	}

} // namespace arterm::ssh
