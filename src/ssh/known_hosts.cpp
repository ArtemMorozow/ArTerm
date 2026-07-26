#include "ssh/known_hosts.hpp"

#include "core/base64.hpp"
#include "core/paths.hpp"

#include <filesystem>
#include <format>

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

		struct HostsGuard
		{
			LIBSSH2_KNOWNHOSTS* hosts;
			~HostsGuard() { libssh2_knownhost_free( hosts ); }
		};

	} // namespace

	KnownHosts::KnownHosts()
		: _file_path( home_directory() + "/.ssh/known_hosts" )
	{}

	KnownHosts::KnownHosts( std::string file_path )
		: _file_path( std::move( file_path ) )
	{}

	std::string KnownHosts::known_hosts_host( std::string const& hostname, std::uint16_t port ){
		if( port == 22 )
			return hostname;
		return std::format( "[{}]:{}", hostname, port );
	}

	std::string KnownHosts::key_type_name( int libssh2_host_key_type ){
		switch( libssh2_host_key_type ){
			case LIBSSH2_HOSTKEY_TYPE_RSA:
				return "ssh-rsa";
			case LIBSSH2_HOSTKEY_TYPE_DSS:
				return "ssh-dss";
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
				return "ecdsa-sha2-nistp256";
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
				return "ecdsa-sha2-nistp384";
			case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
				return "ecdsa-sha2-nistp521";
			case LIBSSH2_HOSTKEY_TYPE_ED25519:
				return "ssh-ed25519";
			default:
				return "unknown";
		}
	}

	std::string KnownHosts::sha256_fingerprint( LIBSSH2_SESSION* session ){
		char const* hash = libssh2_hostkey_hash( session, LIBSSH2_HOSTKEY_HASH_SHA256 );
		if( hash == nullptr )
			return {};

		// OpenSSH prints unpadded base64.
		std::string encoded = base64_encode( std::string_view( hash, 32 ) );
		while( !encoded.empty() && encoded.back() == '=' )
			encoded.pop_back();

		return "SHA256:" + encoded;
	}

	std::string KnownHosts::md5_fingerprint( LIBSSH2_SESSION* session ){
		char const* hash = libssh2_hostkey_hash( session, LIBSSH2_HOSTKEY_HASH_MD5 );
		if( hash == nullptr )
			return {};

		std::string result = "MD5:";
		for( int i = 0; i < 16; ++i ){
			if( i != 0 )
				result += ':';
			result += std::format( "{:02x}", static_cast<unsigned char>( hash[i] ) );
		}
		return result;
	}

	Result<HostKeyInfo> KnownHosts::check( LIBSSH2_SESSION* session, std::string const& hostname,
										   std::uint16_t port ) const{
		HostKeyInfo info;
		info.hostname = hostname;
		info.port     = port;

		size_t      key_length = 0;
		int         key_type   = 0;
		char const* key        = libssh2_session_hostkey( session, &key_length, &key_type );
		if( key == nullptr )
			return fail( ErrorKind::HOST_KEY, "The server did not present a host key" );

		info.raw_key      = std::string( key, key_length );
		info.raw_key_type = key_type;
		info.key_type     = key_type_name( key_type );
		info.sha256       = sha256_fingerprint( session );
		info.md5          = md5_fingerprint( session );

		LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init( session );
		if( hosts == nullptr ){
			info.verdict = HostKeyVerdict::UNUSABLE;
			return info;
		}

		HostsGuard const guard{ hosts };

		int const read_count = libssh2_knownhost_readfile( hosts, _file_path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );
		if( read_count < 0 && std::filesystem::exists( _file_path ) ){
			// The file exists but libssh2 refused to parse it.
			info.verdict = HostKeyVerdict::UNUSABLE;
			return info;
		}

		libssh2_knownhost* match = nullptr;
		int const          rc    = libssh2_knownhost_checkp( hosts, hostname.c_str(), port, key, key_length,
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
			return fail( ErrorKind::HOST_KEY, "Cannot open the known_hosts database" );

		HostsGuard const guard{ hosts };

		libssh2_knownhost_readfile( hosts, _file_path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );

		std::string const host = known_hosts_host( info.hostname, info.port );

		// On a rotation the stale entry must go first, otherwise the file keeps two
		// conflicting keys and every later check reports a mismatch.
		if( info.verdict == HostKeyVerdict::MISMATCH ){
			libssh2_knownhost* stale = nullptr;
			while( libssh2_knownhost_checkp( hosts, info.hostname.c_str(), info.port, info.raw_key.data(),
											 info.raw_key.size(),
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

		int const rc = libssh2_knownhost_addc( hosts, host.c_str(), nullptr, info.raw_key.data(), info.raw_key.size(),
											   "added by ArTerm", 15, type_mask, nullptr );
		if( rc != 0 )
			return fail( ErrorKind::HOST_KEY, "Cannot add the host key", rc );

		// Make sure ~/.ssh exists with the permissions OpenSSH insists on.
		namespace fs = std::filesystem;

		fs::path const  file( _file_path );
		std::error_code ignored;
		fs::create_directories( file.parent_path(), ignored );
		fs::permissions( file.parent_path(), fs::perms::owner_all, fs::perm_options::replace, ignored );

		int const written = libssh2_knownhost_writefile( hosts, _file_path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );
		if( written != 0 )
			return fail( ErrorKind::HOST_KEY, std::format( "Cannot write {}", _file_path ), written );

		fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ignored );
		return {};
	}

	Status KnownHosts::remove( LIBSSH2_SESSION* session, std::string const& hostname, std::uint16_t port ) const{
		LIBSSH2_KNOWNHOSTS* hosts = libssh2_knownhost_init( session );
		if( hosts == nullptr )
			return fail( ErrorKind::HOST_KEY, "Cannot open the known_hosts database" );

		HostsGuard const guard{ hosts };

		if( libssh2_knownhost_readfile( hosts, _file_path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH ) < 0 )
			return fail( ErrorKind::HOST_KEY, std::format( "Cannot read {}", _file_path ) );

		std::string const wanted = known_hosts_host( hostname, port );

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

		int const written = libssh2_knownhost_writefile( hosts, _file_path.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH );
		if( written != 0 )
			return fail( ErrorKind::HOST_KEY, std::format( "Cannot write {}", _file_path ), written );

		return {};
	}

} // namespace arterm::ssh
