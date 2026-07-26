#include "ssh/known_hosts.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <libssh2.h>

namespace arterm::ssh
{
	namespace
	{

		/// Translate a LIBSSH2_HOSTKEY_TYPE_* into the matching knownhost key bit.
		int known_host_key_bit( int host_key_type ){
			switch( host_key_type ){
				case LIBSSH2_HOSTKEY_TYPE_RSA:
					return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
				case LIBSSH2_HOSTKEY_TYPE_DSS:
					return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
				case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
					return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
				case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
					return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
				case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
					return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
				case LIBSSH2_HOSTKEY_TYPE_ED25519:
					return LIBSSH2_KNOWNHOST_KEY_ED25519;
				default:
					return 0;
			}
		}

	} // namespace

	KnownHosts::KnownHosts()
		: _file_path( QDir::homePath() + QLatin1String( "/.ssh/known_hosts" ) )
	{}

	KnownHosts::KnownHosts( QString file_path )
		: _file_path( std::move( file_path ) )
	{}

	QByteArray KnownHosts::known_hosts_host( QString const& hostname, quint16 port ){
		if( port == 22 )
			return hostname.toUtf8();
		return QStringLiteral( "[%1]:%2" ).arg( hostname ).arg( port ).toUtf8();
	}

	QString KnownHosts::key_type_name( int libssh2_host_key_type ){
		switch( libssh2_host_key_type ){
			case LIBSSH2_HOSTKEY_TYPE_RSA:
				return QStringLiteral( "ssh-rsa" );
			case LIBSSH2_HOSTKEY_TYPE_DSS:
				return QStringLiteral( "ssh-dss" );
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
				return QStringLiteral( "ecdsa-sha2-nistp256" );
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
				return QStringLiteral( "ecdsa-sha2-nistp384" );
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
				return QStringLiteral( "ecdsa-sha2-nistp521" );
			case LIBSSH2_HOSTKEY_TYPE_ED25519:
				return QStringLiteral( "ssh-ed25519" );
			default:
				return QObject::tr( "unknown" );
		}
	}

	QString KnownHosts::sha256_fingerprint( LIBSSH2_SESSION* session ){
		char const* hash = libssh2_hostkey_hash( session, LIBSSH2_HOSTKEY_HASH_SHA256 );
		if( hash == nullptr )
			return {};

		QByteArray const digest( hash, 32 );
		// OpenSSH prints unpadded base64.
		QByteArray encoded = digest.toBase64();
		while( encoded.endsWith( '=' ) )
			encoded.chop( 1 );

		return QLatin1String( "SHA256:" ) + QString::fromLatin1( encoded );
	}

	QString KnownHosts::md5_fingerprint( LIBSSH2_SESSION* session ){
		char const* hash = libssh2_hostkey_hash( session, LIBSSH2_HOSTKEY_HASH_MD5 );
		if( hash == nullptr )
			return {};

		QStringList parts;
		parts.reserve( 16 );
		for( int i = 0; i < 16; ++i ){
			parts << QStringLiteral( "%1" ).arg( static_cast<unsigned char>( hash[i] ), 2, 16, QLatin1Char( '0' ) );
		}
		return QLatin1String( "MD5:" ) + parts.join( QLatin1Char( ':' ) );
	}

	Result<HostKeyInfo> KnownHosts::check( LIBSSH2_SESSION* session, QString const& hostname, quint16 port ) const{
		HostKeyInfo info;
		info.hostname = hostname;
		info.port     = port;

		size_t      key_length = 0;
		int         key_type   = 0;
		char const* key        = libssh2_session_hostkey( session, &key_length, &key_type );
		if( key == nullptr ){
			return fail( ErrorKind::HOST_KEY, QObject::tr( "The server did not present a host key" ) );
		}

		info.raw_key      = QByteArray( key, static_cast<qsizetype>( key_length ) );
		info.raw_key_type = key_type;
		info.key_type     = key_type_name( key_type );
		info.sha256       = sha256_fingerprint( session );
		info.md5          = md5_fingerprint( session );

		LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init( session );
		if( hosts == nullptr ){
			info.verdict = HostKeyVerdict::UNUSABLE;
			return info;
		}

		struct HostsGuard
		{
			LIBSSH2_KNOWNHOSTS* hosts;
			~HostsGuard() { libssh2_knownhost_free( hosts ); }
		} guard{ hosts };

		QByteArray const file_path = _file_path.toUtf8();
		int const        read_count =
			libssh2_knownhost_readfile( hosts, file_path.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );
		if( read_count < 0 && QFile::exists( _file_path ) ){
			// The file exists but libssh2 refused to parse it.
			info.verdict = HostKeyVerdict::UNUSABLE;
			return info;
		}

		QByteArray const   host  = hostname.toUtf8();
		libssh2_knownhost* match = nullptr;
		int const          rc    = libssh2_knownhost_checkp( hosts, host.constData(), port, key, key_length,
															 LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, &match );

		switch( rc ){
			case LIBSSH2_KNOWNHOST_CHECK_MATCH:
				info.verdict = HostKeyVerdict::MATCH;
				break;
			case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
				info.verdict = HostKeyVerdict::MISMATCH;
				break;
			case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
				info.verdict = HostKeyVerdict::UNKNOWN;
				break;
			default:
				info.verdict = HostKeyVerdict::UNUSABLE;
				break;
		}

		return info;
	}

	Status KnownHosts::store( LIBSSH2_SESSION* session, HostKeyInfo const& info ) const{
		LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init( session );
		if( hosts == nullptr )
			return fail( ErrorKind::HOST_KEY, QObject::tr( "Cannot open the known_hosts database" ) );

		struct HostsGuard
		{
			LIBSSH2_KNOWNHOSTS* hosts;
			~HostsGuard() { libssh2_knownhost_free( hosts ); }
		} guard{ hosts };

		QByteArray const file_path = _file_path.toUtf8();
		libssh2_knownhost_readfile( hosts, file_path.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );

		QByteArray const host = known_hosts_host( info.hostname, info.port );

		// On a rotation the stale entry must go first, otherwise the file keeps two
		// conflicting keys and every later check reports a mismatch.
		if( info.verdict == HostKeyVerdict::MISMATCH ){
			libssh2_knownhost* stale = nullptr;
			while( libssh2_knownhost_checkp( hosts, info.hostname.toUtf8().constData(), info.port,
											 info.raw_key.constData(), static_cast<size_t>( info.raw_key.size() ),
											 LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW,
											 &stale ) == LIBSSH2_KNOWNHOST_CHECK_MISMATCH &&
				   stale != nullptr ){
				if( libssh2_knownhost_del( hosts, stale ) != 0 )
					break;
				stale = nullptr;
			}
		}

		int const type_mask =
			LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | known_host_key_bit( info.raw_key_type );

		int const rc = libssh2_knownhost_addc( hosts, host.constData(), nullptr, info.raw_key.constData(),
											   static_cast<size_t>( info.raw_key.size() ), "added by ArTerm", 15,
											   type_mask, nullptr );
		if( rc != 0 )
			return fail( ErrorKind::HOST_KEY, QObject::tr( "Cannot add the host key" ), rc );

		// Make sure ~/.ssh exists with the permissions OpenSSH insists on.
		QFileInfo const file_info( _file_path );
		QDir().mkpath( file_info.absolutePath() );
		QFile::setPermissions( file_info.absolutePath(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner );

		int const written = libssh2_knownhost_writefile( hosts, file_path.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );
		if( written != 0 )
			return fail( ErrorKind::HOST_KEY, QObject::tr( "Cannot write %1" ).arg( _file_path ), written );

		QFile::setPermissions( _file_path, QFile::ReadOwner | QFile::WriteOwner );
		return {};
	}

	Status KnownHosts::remove( LIBSSH2_SESSION* session, QString const& hostname, quint16 port ) const{
		LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init( session );
		if( hosts == nullptr )
			return fail( ErrorKind::HOST_KEY, QObject::tr( "Cannot open the known_hosts database" ) );

		struct HostsGuard
		{
			LIBSSH2_KNOWNHOSTS* hosts;
			~HostsGuard() { libssh2_knownhost_free( hosts ); }
		} guard{ hosts };

		QByteArray const file_path = _file_path.toUtf8();
		if( libssh2_knownhost_readfile( hosts, file_path.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH ) < 0 )
			return fail( ErrorKind::HOST_KEY, QObject::tr( "Cannot read %1" ).arg( _file_path ) );

		QByteArray const wanted = known_hosts_host( hostname, port );

		libssh2_knownhost* entry       = nullptr;
		libssh2_knownhost* previous    = nullptr;
		bool               removed_any = false;

		while( libssh2_knownhost_get( hosts, &entry, previous ) == 0 ){
			libssh2_knownhost* current = entry;
			previous                   = entry;

			if( current->name != nullptr && wanted == current->name ){
				if( libssh2_knownhost_del( hosts, current ) == 0 ){
					removed_any = true;
					// The deleted node can no longer be used as an iteration cursor.
					previous = nullptr;
				}
			}
		}

		if( !removed_any )
			return {};

		int const written = libssh2_knownhost_writefile( hosts, file_path.constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );
		if( written != 0 )
			return fail( ErrorKind::HOST_KEY, QObject::tr( "Cannot write %1" ).arg( _file_path ), written );

		return {};
	}

} // namespace arterm::ssh
