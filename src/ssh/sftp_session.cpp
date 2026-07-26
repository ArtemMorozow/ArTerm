#include "ssh/sftp_session.hpp"

#include "ssh/scp_transfer.hpp"
#include "ssh/session_interaction.hpp"
#include "ssh/ssh_connection.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <utility>

#include <sys/stat.h>

namespace arterm::ssh
{
	namespace
	{

		namespace fs = std::filesystem;

		constexpr std::size_t CHUNK_SIZE = 64 * 1024;

		/// Minimum gap between two progress signals, so a fast local transfer does not
		/// flood the main queue.
		constexpr auto PROGRESS_INTERVAL = std::chrono::milliseconds( 80 );

		using FilePtr = std::unique_ptr<std::FILE, decltype( &std::fclose )>;

		std::string join_remote( std::string const& directory, std::string const& name ){
			if( directory.empty() || directory == "/" )
				return "/" + name;
			if( directory.ends_with( '/' ) )
				return directory + name;
			return directory + "/" + name;
		}

		/// ASCII-only case folding; matches what the remote side sorts with far more
		/// often than a full Unicode collation would.
		bool name_less_ignoring_case( std::string const& a, std::string const& b ){
			auto const fold = []( char c ) { return std::tolower( static_cast<unsigned char>( c ) ); };
			return std::ranges::lexicographical_compare( a, b, {}, fold, fold );
		}

		RemoteFileEntry entry_from_attributes( std::string const& directory, std::string const& name,
											   LIBSSH2_SFTP_ATTRIBUTES const& attributes ){
			RemoteFileEntry entry;
			entry.name = name;
			entry.path = join_remote( directory, name );

			if( attributes.flags & LIBSSH2_SFTP_ATTR_SIZE )
				entry.size = attributes.filesize;
			if( attributes.flags & LIBSSH2_SFTP_ATTR_UIDGID ){
				entry.uid = static_cast<std::uint32_t>( attributes.uid );
				entry.gid = static_cast<std::uint32_t>( attributes.gid );
			}
			if( attributes.flags & LIBSSH2_SFTP_ATTR_ACMODTIME )
				entry.modified = std::chrono::system_clock::from_time_t( static_cast<time_t>( attributes.mtime ) );
			if( attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS ){
				entry.permissions   = static_cast<std::uint32_t>( attributes.permissions & 0xFFF );
				entry.is_directory  = LIBSSH2_SFTP_S_ISDIR( attributes.permissions );
				entry.is_symlink    = LIBSSH2_SFTP_S_ISLNK( attributes.permissions );
				entry.is_executable = ( attributes.permissions & 0111 ) != 0;
			}

			return entry;
		}

		struct HandleGuard
		{
			LIBSSH2_SFTP_HANDLE* handle;
			~HandleGuard() { libssh2_sftp_close_handle( handle ); }
		};

	} // namespace

	std::shared_ptr<SftpSession> SftpSession::create( HostProfile                         profile,
													  std::shared_ptr<SessionInteraction> interaction ){
		return std::shared_ptr<SftpSession>( new SftpSession( std::move( profile ), std::move( interaction ) ) );
	}

	SftpSession::SftpSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction )
		: _profile( std::move( profile ) )
		, _interaction( std::move( interaction ) )
		, _queue( "arterm.sftp." + _profile.id )
	{}

	SftpSession::~SftpSession(){
		// See ShellSession's destructor: the release can happen on the session
		// queue, where a sync would deadlock.
		if( _queue.is_current() )
			run_shutdown();
		else
			_queue.sync( [this] { run_shutdown(); } );
	}

	void SftpSession::start(){
		_queue.async( [weak = weak_from_this()]{
			if( auto self = weak.lock() )
				self->run_start();
		} );
	}

	void SftpSession::run_start(){
		_connection = std::make_unique<SshConnection>( _profile );

		if( _interaction ){
			auto interaction = _interaction;
			_connection->set_host_key_prompt(
				[interaction]( HostKeyInfo const& info ) { return interaction->confirm_host_key( info ); } );
			_connection->set_credential_prompt( [interaction]( std::string const& prompt, bool echo ){
				return interaction->ask_credential( prompt, echo );
			} );
		}

		if( auto status = _connection->open(); !status ){
			_connection.reset();
			failed( status.error() );
			return;
		}

		// SFTP runs blocking: this queue exists precisely so long transfers never
		// touch the main queue.
		_connection->set_blocking( true );

		_sftp = libssh2_sftp_init( _connection->session() );
		if( _sftp == nullptr ){
			auto error = _connection->last_error( ErrorKind::SFTP, "The server refused the SFTP subsystem" );
			_connection.reset();
			failed( error );
			return;
		}

		// Resolve "." to learn the login directory.
		char      resolved[1024] = {};
		int const length         = libssh2_sftp_realpath( _sftp, ".", resolved, sizeof( resolved ) - 1 );
		_home_directory          = length > 0 ? std::string( resolved, static_cast<std::size_t>( length ) ) : "/";

		if( !_profile.startup_directory.empty() )
			_home_directory = _profile.startup_directory;

		ready( _home_directory );
	}

	void SftpSession::shutdown(){
		_queue.async( [weak = weak_from_this()]{
			if( auto self = weak.lock() )
				self->run_shutdown();
		} );
	}

	void SftpSession::run_shutdown(){
		if( _sftp != nullptr ){
			libssh2_sftp_shutdown( _sftp );
			_sftp = nullptr;
		}
		_connection.reset();
	}

	void SftpSession::request_cancel( std::uint64_t request_id ){
		std::lock_guard const lock( _cancel_mutex );
		_cancel_requests.insert( request_id );
	}

	bool SftpSession::is_cancelled( std::uint64_t request_id ) const{
		std::lock_guard const lock( _cancel_mutex );
		return _cancel_requests.contains( request_id );
	}

	void SftpSession::clear_cancel( std::uint64_t request_id ){
		std::lock_guard const lock( _cancel_mutex );
		_cancel_requests.erase( request_id );
	}

	Error SftpSession::sftp_error( std::string const& context ) const{
		if( _sftp == nullptr )
			return Error{ ErrorKind::SFTP, context };

		auto const code = libssh2_sftp_last_error( _sftp );

		std::string reason;
		switch( code ){
			case LIBSSH2_FX_NO_SUCH_FILE:
			case LIBSSH2_FX_NO_SUCH_PATH:
				reason = "no such file or directory";
				break;
			case LIBSSH2_FX_PERMISSION_DENIED:
				reason = "permission denied";
				break;
			case LIBSSH2_FX_FILE_ALREADY_EXISTS:
				reason = "already exists";
				break;
			case LIBSSH2_FX_DIR_NOT_EMPTY:
				reason = "directory is not empty";
				break;
			case LIBSSH2_FX_QUOTA_EXCEEDED:
				reason = "quota exceeded";
				break;
			case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM:
				reason = "no space left on the remote filesystem";
				break;
			case LIBSSH2_FX_OP_UNSUPPORTED:
				reason = "operation not supported by the server";
				break;
			case 0:
				// The failure came from the transport rather than the SFTP layer.
				return _connection != nullptr ? _connection->last_error( ErrorKind::SFTP, context )
											  : Error{ ErrorKind::SFTP, context };
			default:
				reason = std::format( "SFTP status {}", code );
				break;
		}

		return Error{ ErrorKind::SFTP, context + ": " + reason, static_cast<int>( code ) };
	}

	// ---------------------------------------------------------------------------
	// Browsing
	// ---------------------------------------------------------------------------

	Result<RemoteListing> SftpSession::read_directory( std::string const& path ){
		if( _sftp == nullptr )
			return fail( ErrorKind::SFTP, "Not connected" );

		LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_opendir( _sftp, path.c_str() );
		if( handle == nullptr )
			return std::unexpected( sftp_error( std::format( "Cannot open {}", path ) ) );

		HandleGuard const guard{ handle };

		RemoteListing           entries;
		char                    name[1024];
		LIBSSH2_SFTP_ATTRIBUTES attributes{};

		while( true ){
			int const length = libssh2_sftp_readdir_ex( handle, name, sizeof( name ), nullptr, 0, &attributes );
			if( length == 0 )
				break;
			if( length < 0 )
				return std::unexpected( sftp_error( std::format( "Cannot read {}", path ) ) );

			std::string const file_name( name, static_cast<std::size_t>( length ) );
			if( file_name == "." || file_name == ".." )
				continue;

			RemoteFileEntry entry = entry_from_attributes( path, file_name, attributes );

			// readdir reports the link itself; resolve it so the browser can descend
			// into symlinked directories.
			if( entry.is_symlink ){
				LIBSSH2_SFTP_ATTRIBUTES target{};
				if( libssh2_sftp_stat_ex( _sftp, entry.path.c_str(), static_cast<unsigned int>( entry.path.size() ),
										  LIBSSH2_SFTP_STAT, &target ) == 0 ){
					if( target.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS )
						entry.is_directory = LIBSSH2_SFTP_S_ISDIR( target.permissions );
					if( target.flags & LIBSSH2_SFTP_ATTR_SIZE )
						entry.size = target.filesize;
				}
			}

			entries.push_back( std::move( entry ) );
		}

		// Directories first, then case-insensitive by name - the ordering every
		// file manager uses.
		std::ranges::sort( entries, []( RemoteFileEntry const& a, RemoteFileEntry const& b ){
			if( a.is_directory != b.is_directory )
				return a.is_directory;
			return name_less_ignoring_case( a.name, b.name );
		} );

		return entries;
	}

	Result<RemoteFileEntry> SftpSession::stat_entry( std::string const& path ){
		if( _sftp == nullptr )
			return fail( ErrorKind::SFTP, "Not connected" );

		LIBSSH2_SFTP_ATTRIBUTES attributes{};
		if( libssh2_sftp_stat_ex( _sftp, path.c_str(), static_cast<unsigned int>( path.size() ), LIBSSH2_SFTP_STAT,
								  &attributes ) != 0 ){
			return std::unexpected( sftp_error( std::format( "Cannot stat {}", path ) ) );
		}

		auto const        slash     = path.rfind( '/' );
		std::string const directory = ( slash != std::string::npos && slash > 0 ) ? path.substr( 0, slash ) : "/";
		std::string const name      = slash != std::string::npos ? path.substr( slash + 1 ) : path;

		return entry_from_attributes( directory, name, attributes );
	}

	void SftpSession::list_directory( std::uint64_t request_id, std::string path ){
		_queue.async( [weak = weak_from_this(), request_id, path = std::move( path )]{
			auto self = weak.lock();
			if( !self )
				return;

			auto entries = self->read_directory( path );
			if( !entries ){
				self->operation_failed( request_id, entries.error() );
				return;
			}
			self->listing_ready( request_id, path, *entries );
		} );
	}

	void SftpSession::resolve_path( std::uint64_t request_id, std::string path ){
		_queue.async( [weak = weak_from_this(), request_id, path = std::move( path )]{
			auto self = weak.lock();
			if( !self )
				return;

			if( self->_sftp == nullptr ){
				self->operation_failed( request_id, Error{ ErrorKind::SFTP, "Not connected" } );
				return;
			}

			char      resolved[1024] = {};
			int const length = libssh2_sftp_realpath( self->_sftp, path.c_str(), resolved, sizeof( resolved ) - 1 );
			if( length < 0 ){
				self->operation_failed( request_id, self->sftp_error( std::format( "Cannot resolve {}", path ) ) );
				return;
			}

			self->path_resolved( request_id, std::string( resolved, static_cast<std::size_t>( length ) ) );
		} );
	}

	void SftpSession::make_directory( std::uint64_t request_id, std::string path ){
		_queue.async( [weak = weak_from_this(), request_id, path = std::move( path )]{
			auto self = weak.lock();
			if( !self )
				return;

			if( libssh2_sftp_mkdir_ex( self->_sftp, path.c_str(), static_cast<unsigned int>( path.size() ), 0755 ) !=
				0 ){
				self->operation_failed( request_id, self->sftp_error( std::format( "Cannot create {}", path ) ) );
				return;
			}
			self->operation_finished( request_id );
		} );
	}

	void SftpSession::rename_entry( std::uint64_t request_id, std::string from, std::string to ){
		_queue.async( [weak = weak_from_this(), request_id, from = std::move( from ), to = std::move( to )]{
			auto self = weak.lock();
			if( !self )
				return;

			int const rc = libssh2_sftp_rename_ex( self->_sftp, from.c_str(), static_cast<unsigned int>( from.size() ),
												   to.c_str(), static_cast<unsigned int>( to.size() ),
												   LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC |
													   LIBSSH2_SFTP_RENAME_NATIVE );

			if( rc != 0 ){
				self->operation_failed( request_id,
										self->sftp_error( std::format( "Cannot rename {} to {}", from, to ) ) );
				return;
			}
			self->operation_finished( request_id );
		} );
	}

	void SftpSession::change_permissions( std::uint64_t request_id, std::string path, std::uint32_t mode ){
		_queue.async( [weak = weak_from_this(), request_id, path = std::move( path ), mode]{
			auto self = weak.lock();
			if( !self )
				return;

			LIBSSH2_SFTP_ATTRIBUTES attributes{};
			attributes.flags       = LIBSSH2_SFTP_ATTR_PERMISSIONS;
			attributes.permissions = mode;

			if( libssh2_sftp_stat_ex( self->_sftp, path.c_str(), static_cast<unsigned int>( path.size() ),
									  LIBSSH2_SFTP_SETSTAT, &attributes ) != 0 ){
				self->operation_failed( request_id,
										self->sftp_error( std::format( "Cannot change the mode of {}", path ) ) );
				return;
			}
			self->operation_finished( request_id );
		} );
	}

	void SftpSession::remove_entry( std::uint64_t request_id, std::string path, bool recursive ){
		_queue.async( [weak = weak_from_this(), request_id, path = std::move( path ), recursive]{
			auto self = weak.lock();
			if( !self )
				return;

			auto info = self->stat_entry( path );
			if( !info ){
				self->operation_failed( request_id, info.error() );
				return;
			}

			Status result;
			if( info->is_directory && !info->is_symlink ){
				if( recursive ){
					result = self->remove_tree( request_id, path );
				}
				else if( libssh2_sftp_rmdir_ex( self->_sftp, path.c_str(), static_cast<unsigned int>( path.size() ) ) !=
						 0 ){
					result = std::unexpected( self->sftp_error( std::format( "Cannot remove {}", path ) ) );
				}
			}
			else if( libssh2_sftp_unlink_ex( self->_sftp, path.c_str(), static_cast<unsigned int>( path.size() ) ) !=
					 0 ){
				result = std::unexpected( self->sftp_error( std::format( "Cannot delete {}", path ) ) );
			}

			self->clear_cancel( request_id );

			if( !result ){
				self->operation_failed( request_id, result.error() );
				return;
			}
			self->operation_finished( request_id );
		} );
	}

	Status SftpSession::remove_tree( std::uint64_t request_id, std::string const& path ){
		if( is_cancelled( request_id ) )
			return cancelled();

		auto entries = read_directory( path );
		if( !entries )
			return std::unexpected( entries.error() );

		for( RemoteFileEntry const& entry : *entries ){
			if( is_cancelled( request_id ) )
				return cancelled();

			if( entry.is_directory && !entry.is_symlink ){
				if( auto status = remove_tree( request_id, entry.path ); !status )
					return status;
			}
			else if( libssh2_sftp_unlink_ex( _sftp, entry.path.c_str(),
											 static_cast<unsigned int>( entry.path.size() ) ) != 0 ){
				return std::unexpected( sftp_error( std::format( "Cannot delete {}", entry.path ) ) );
			}
		}

		if( libssh2_sftp_rmdir_ex( _sftp, path.c_str(), static_cast<unsigned int>( path.size() ) ) != 0 )
			return std::unexpected( sftp_error( std::format( "Cannot remove {}", path ) ) );

		return {};
	}

	// ---------------------------------------------------------------------------
	// Transfers
	// ---------------------------------------------------------------------------

	void SftpSession::publish_progress( std::uint64_t request_id, std::uint64_t moved, std::uint64_t total ){
		auto const now     = std::chrono::steady_clock::now();
		bool const first   = _last_progress_tick == std::chrono::steady_clock::time_point{};
		auto const elapsed = now - _last_progress_tick;

		if( !first && elapsed < PROGRESS_INTERVAL && moved < total )
			return;

		TransferProgress progress;
		progress.transferred = moved;
		progress.total       = total;

		if( !first && elapsed > std::chrono::steady_clock::duration::zero() ){
			double const seconds      = std::chrono::duration<double>( elapsed ).count();
			progress.bytes_per_second = static_cast<double>( moved - _last_progress_bytes ) / seconds;
		}

		_last_progress_tick  = now;
		_last_progress_bytes = moved;

		transfer_progress( request_id, progress );
	}

	Result<std::uint64_t> SftpSession::remote_tree_size( std::string const& path ){
		auto info = stat_entry( path );
		if( !info )
			return std::unexpected( info.error() );

		if( !info->is_directory )
			return info->size;

		auto entries = read_directory( path );
		if( !entries )
			return std::unexpected( entries.error() );

		std::uint64_t total = 0;
		for( RemoteFileEntry const& entry : *entries ){
			if( entry.is_directory && !entry.is_symlink ){
				auto sub = remote_tree_size( entry.path );
				if( !sub )
					return sub;
				total += *sub;
			}
			else{
				total += entry.size;
			}
		}
		return total;
	}

	std::uint64_t SftpSession::local_tree_size( std::string const& path ){
		std::error_code ignored;

		if( !fs::is_directory( path, ignored ) ){
			auto const size = fs::file_size( path, ignored );
			return ignored ? 0 : size;
		}

		std::uint64_t total = 0;
		for( auto iterator =
				 fs::recursive_directory_iterator( path, fs::directory_options::skip_permission_denied, ignored );
			 auto const& entry : iterator ){
			if( entry.is_regular_file( ignored ) )
				total += entry.file_size( ignored );
		}
		return total;
	}

	void SftpSession::download( std::uint64_t request_id, std::string remote_path, std::string local_path ){
		_queue.async( [weak = weak_from_this(), request_id, remote_path = std::move( remote_path ),
					   local_path = std::move( local_path )]{
			auto self = weak.lock();
			if( !self )
				return;

			self->_last_progress_tick  = {};
			self->_last_progress_bytes = 0;

			auto total = self->remote_tree_size( remote_path );
			if( !total ){
				self->clear_cancel( request_id );
				self->operation_failed( request_id, total.error() );
				return;
			}

			self->transfer_started( request_id, *total );

			std::uint64_t moved  = 0;
			auto          status = self->download_tree( request_id, remote_path, local_path, *total, moved );

			self->clear_cancel( request_id );

			if( !status ){
				self->operation_failed( request_id, status.error() );
				return;
			}

			self->publish_progress( request_id, *total, *total );
			self->transfer_finished( request_id );
		} );
	}

	Status SftpSession::download_tree( std::uint64_t request_id, std::string const& remote_path,
									   std::string const& local_path, std::uint64_t total, std::uint64_t& moved ){
		if( is_cancelled( request_id ) )
			return cancelled();

		auto info = stat_entry( remote_path );
		if( !info )
			return std::unexpected( info.error() );

		if( !info->is_directory )
			return download_file( request_id, remote_path, local_path, total, moved );

		std::error_code error;
		fs::create_directories( local_path, error );
		if( error )
			return fail( ErrorKind::LOCAL_IO, std::format( "Cannot create {}", local_path ) );

		auto entries = read_directory( remote_path );
		if( !entries )
			return std::unexpected( entries.error() );

		for( RemoteFileEntry const& entry : *entries ){
			std::string const target = local_path + "/" + entry.name;
			if( auto status = download_tree( request_id, entry.path, target, total, moved ); !status )
				return status;
		}

		return {};
	}

	Status SftpSession::download_file( std::uint64_t request_id, std::string const& remote_path,
									   std::string const& local_path, std::uint64_t total, std::uint64_t& moved ){
		if( _backend == TransferBackend::SCP ){
			std::uint64_t const base = moved;
			return scp::receive_file(
				_connection->session(), remote_path, local_path,
				[this, request_id, base, total, &moved]( std::uint64_t transferred, std::uint64_t ){
					moved = base + transferred;
					publish_progress( request_id, moved, total );
					return !is_cancelled( request_id );
				} );
		}

		LIBSSH2_SFTP_HANDLE* handle =
			libssh2_sftp_open_ex( _sftp, remote_path.c_str(), static_cast<unsigned int>( remote_path.size() ),
								  LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE );
		if( handle == nullptr )
			return std::unexpected( sftp_error( std::format( "Cannot open {}", remote_path ) ) );

		HandleGuard const guard{ handle };

		FilePtr output( std::fopen( local_path.c_str(), "wb" ), &std::fclose );
		if( !output )
			return fail( ErrorKind::LOCAL_IO,
						 std::format( "Cannot write {}: {}", local_path, std::strerror( errno ) ) );

		auto const discard_partial_file = [&output, &local_path]{
			output.reset();
			std::error_code ignored;
			fs::remove( local_path, ignored );
		};

		std::string buffer( CHUNK_SIZE, '\0' );

		while( true ){
			if( is_cancelled( request_id ) ){
				discard_partial_file();
				return cancelled();
			}

			auto const count = libssh2_sftp_read( handle, buffer.data(), CHUNK_SIZE );
			if( count == 0 )
				break;
			if( count < 0 ){
				discard_partial_file();
				return std::unexpected( sftp_error( std::format( "Cannot read {}", remote_path ) ) );
			}

			auto const chunk = static_cast<std::size_t>( count );
			if( std::fwrite( buffer.data(), 1, chunk, output.get() ) != chunk ){
				int const write_errno = errno;
				discard_partial_file();
				return fail( ErrorKind::LOCAL_IO,
							 std::format( "Cannot write {}: {}", local_path, std::strerror( write_errno ) ) );
			}

			moved += chunk;
			publish_progress( request_id, moved, total );
		}

		if( std::fflush( output.get() ) != 0 )
			return fail( ErrorKind::LOCAL_IO,
						 std::format( "Cannot flush {}: {}", local_path, std::strerror( errno ) ) );

		return {};
	}

	void SftpSession::upload( std::uint64_t request_id, std::string local_path, std::string remote_path ){
		_queue.async( [weak = weak_from_this(), request_id, local_path = std::move( local_path ),
					   remote_path = std::move( remote_path )]{
			auto self = weak.lock();
			if( !self )
				return;

			self->_last_progress_tick  = {};
			self->_last_progress_bytes = 0;

			std::error_code ignored;
			if( !fs::exists( local_path, ignored ) ){
				self->clear_cancel( request_id );
				self->operation_failed( request_id,
										Error{ ErrorKind::LOCAL_IO, std::format( "{} does not exist", local_path ) } );
				return;
			}

			std::uint64_t const total = local_tree_size( local_path );
			self->transfer_started( request_id, total );

			std::uint64_t moved  = 0;
			auto          status = self->upload_tree( request_id, local_path, remote_path, total, moved );

			self->clear_cancel( request_id );

			if( !status ){
				self->operation_failed( request_id, status.error() );
				return;
			}

			self->publish_progress( request_id, total, total );
			self->transfer_finished( request_id );
		} );
	}

	Status SftpSession::upload_tree( std::uint64_t request_id, std::string const& local_path,
									 std::string const& remote_path, std::uint64_t total, std::uint64_t& moved ){
		if( is_cancelled( request_id ) )
			return cancelled();

		std::error_code ignored;
		if( !fs::is_directory( local_path, ignored ) )
			return upload_file( request_id, local_path, remote_path, total, moved );

		int const rc =
			libssh2_sftp_mkdir_ex( _sftp, remote_path.c_str(), static_cast<unsigned int>( remote_path.size() ), 0755 );
		if( rc != 0 && libssh2_sftp_last_error( _sftp ) != LIBSSH2_FX_FILE_ALREADY_EXISTS )
			return std::unexpected( sftp_error( std::format( "Cannot create {}", remote_path ) ) );

		for( auto const& child :
			 fs::directory_iterator( local_path, fs::directory_options::skip_permission_denied, ignored ) ){
			std::string const name   = child.path().filename().string();
			std::string const target = join_remote( remote_path, name );
			if( auto status = upload_tree( request_id, child.path().string(), target, total, moved ); !status )
				return status;
		}

		return {};
	}

	Status SftpSession::upload_file( std::uint64_t request_id, std::string const& local_path,
									 std::string const& remote_path, std::uint64_t total, std::uint64_t& moved ){
		if( _backend == TransferBackend::SCP ){
			std::uint64_t const base = moved;
			return scp::send_file( _connection->session(), local_path, remote_path,
								   [this, request_id, base, total, &moved]( std::uint64_t transferred, std::uint64_t ){
									   moved = base + transferred;
									   publish_progress( request_id, moved, total );
									   return !is_cancelled( request_id );
								   } );
		}

		FilePtr input( std::fopen( local_path.c_str(), "rb" ), &std::fclose );
		if( !input )
			return fail( ErrorKind::LOCAL_IO, std::format( "Cannot read {}: {}", local_path, std::strerror( errno ) ) );

		struct ::stat info{};
		long const mode = ( ::stat( local_path.c_str(), &info ) == 0 && ( info.st_mode & S_IXUSR ) != 0 ) ? 0755 : 0644;

		LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open_ex(
			_sftp, remote_path.c_str(), static_cast<unsigned int>( remote_path.size() ),
			LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, mode, LIBSSH2_SFTP_OPENFILE );
		if( handle == nullptr )
			return std::unexpected( sftp_error( std::format( "Cannot create {}", remote_path ) ) );

		HandleGuard const guard{ handle };

		std::string buffer( CHUNK_SIZE, '\0' );

		while( true ){
			if( is_cancelled( request_id ) )
				return cancelled();

			std::size_t const read = std::fread( buffer.data(), 1, CHUNK_SIZE, input.get() );
			if( read == 0 ){
				if( std::ferror( input.get() ) != 0 )
					return fail( ErrorKind::LOCAL_IO, std::format( "Cannot read {}", local_path ) );
				break;
			}

			std::size_t offset = 0;
			while( offset < read ){
				auto const written = libssh2_sftp_write( handle, buffer.data() + offset, read - offset );
				if( written < 0 )
					return std::unexpected( sftp_error( std::format( "Cannot write {}", remote_path ) ) );

				offset += static_cast<std::size_t>( written );
				moved += static_cast<std::uint64_t>( written );
				publish_progress( request_id, moved, total );
			}
		}

		return {};
	}

} // namespace arterm::ssh
