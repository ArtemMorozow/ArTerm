#include "ssh/scp_transfer.hpp"

#include <libssh2.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

#include <sys/stat.h>

namespace arterm::ssh::scp
{
	namespace
	{

		constexpr std::size_t CHUNK_SIZE = 64 * 1024;

		using FilePtr = std::unique_ptr<std::FILE, decltype( &std::fclose )>;

		Error session_error( LIBSSH2_SESSION* session, std::string const& context ){
			char*     message = nullptr;
			int       length  = 0;
			int const code    = libssh2_session_last_error( session, &message, &length, 0 );

			std::string detail = ( message == nullptr || length <= 0 )
									 ? std::string{}
									 : std::string( message, static_cast<std::size_t>( length ) );
			if( detail.empty() )
				detail = std::format( "libssh2 error {}", code );

			return Error{ ErrorKind::SFTP, context + ": " + detail, code };
		}

		struct ChannelGuard
		{
			LIBSSH2_CHANNEL* channel;
			~ChannelGuard(){
				libssh2_channel_close( channel );
				libssh2_channel_free( channel );
			}
		};

	} // namespace

	Status receive_file( LIBSSH2_SESSION* session, std::string const& remote_path, std::string const& local_path,
						 ProgressCallback const& on_progress ){
		libssh2_struct_stat file_info{};

		LIBSSH2_CHANNEL* channel = libssh2_scp_recv2( session, remote_path.c_str(), &file_info );
		if( channel == nullptr )
			return std::unexpected( session_error( session, std::format( "SCP download of {} failed", remote_path ) ) );

		ChannelGuard const guard{ channel };

		FilePtr output( std::fopen( local_path.c_str(), "wb" ), &std::fclose );
		if( !output )
			return fail( ErrorKind::LOCAL_IO,
						 std::format( "Cannot write {}: {}", local_path, std::strerror( errno ) ) );

		auto const discard_partial_file = [&output, &local_path]{
			output.reset();
			std::error_code ignored;
			std::filesystem::remove( local_path, ignored );
		};

		auto const    total    = static_cast<std::uint64_t>( file_info.st_size );
		std::uint64_t received = 0;
		std::string   buffer( CHUNK_SIZE, '\0' );

		while( received < total ){
			auto const wanted = static_cast<std::size_t>( std::min<std::uint64_t>( CHUNK_SIZE, total - received ) );
			auto const count  = libssh2_channel_read( channel, buffer.data(), wanted );

			if( count == LIBSSH2_ERROR_EAGAIN )
				continue;
			if( count < 0 ){
				discard_partial_file();
				return std::unexpected(
					session_error( session, std::format( "SCP download of {} failed", remote_path ) ) );
			}
			if( count == 0 )
				break;

			auto const chunk = static_cast<std::size_t>( count );
			if( std::fwrite( buffer.data(), 1, chunk, output.get() ) != chunk ){
				int const write_errno = errno;
				discard_partial_file();
				return fail( ErrorKind::LOCAL_IO,
							 std::format( "Cannot write {}: {}", local_path, std::strerror( write_errno ) ) );
			}

			received += chunk;

			if( on_progress && !on_progress( received, total ) ){
				discard_partial_file();
				return cancelled();
			}
		}

		if( std::fflush( output.get() ) != 0 )
			return fail( ErrorKind::LOCAL_IO,
						 std::format( "Cannot flush {}: {}", local_path, std::strerror( errno ) ) );
		output.reset();

		// Make sure the file is usable by its owner whatever the remote mode was.
		namespace fs = std::filesystem;
		std::error_code ignored;
		fs::permissions( local_path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add, ignored );
		return {};
	}

	Status send_file( LIBSSH2_SESSION* session, std::string const& local_path, std::string const& remote_path,
					  ProgressCallback const& on_progress ){
		FilePtr input( std::fopen( local_path.c_str(), "rb" ), &std::fclose );
		if( !input )
			return fail( ErrorKind::LOCAL_IO, std::format( "Cannot read {}: {}", local_path, std::strerror( errno ) ) );

		struct ::stat info{};
		if( ::stat( local_path.c_str(), &info ) != 0 )
			return fail( ErrorKind::LOCAL_IO, std::format( "Cannot stat {}: {}", local_path, std::strerror( errno ) ) );

		auto const total = static_cast<std::uint64_t>( info.st_size );

		// Carry over the executable bit; everything else uses a sane default.
		int const mode = ( info.st_mode & S_IXUSR ) != 0 ? 0755 : 0644;

		LIBSSH2_CHANNEL* channel = libssh2_scp_send64(
			session, remote_path.c_str(), mode, static_cast<libssh2_int64_t>( total ), info.st_mtime, info.st_atime );
		if( channel == nullptr )
			return std::unexpected( session_error( session, std::format( "SCP upload of {} failed", local_path ) ) );

		ChannelGuard const guard{ channel };

		std::uint64_t sent = 0;
		std::string   buffer( CHUNK_SIZE, '\0' );

		while( sent < total ){
			std::size_t const read = std::fread( buffer.data(), 1, CHUNK_SIZE, input.get() );
			if( read == 0 ){
				if( std::ferror( input.get() ) != 0 )
					return fail( ErrorKind::LOCAL_IO, std::format( "Cannot read {}", local_path ) );
				break;
			}

			std::size_t offset = 0;
			while( offset < read ){
				auto const written = libssh2_channel_write( channel, buffer.data() + offset, read - offset );
				if( written == LIBSSH2_ERROR_EAGAIN )
					continue;
				if( written < 0 ){
					return std::unexpected(
						session_error( session, std::format( "SCP upload of {} failed", local_path ) ) );
				}
				offset += static_cast<std::size_t>( written );
			}

			sent += read;

			if( on_progress && !on_progress( sent, total ) )
				return cancelled();
		}

		// SCP requires the explicit EOF/ack exchange before the file is committed.
		libssh2_channel_send_eof( channel );
		libssh2_channel_wait_eof( channel );
		libssh2_channel_wait_closed( channel );

		return {};
	}

} // namespace arterm::ssh::scp
