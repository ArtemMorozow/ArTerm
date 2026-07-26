#include "ssh/scp_transfer.hpp"

#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <libssh2.h>

#include <sys/stat.h>

namespace arterm::ssh::scp
{
	namespace
	{

		constexpr qint64 CHUNK_SIZE = 64 * 1024;

		Error session_error( LIBSSH2_SESSION* session, QString const& context ){
			char*     message = nullptr;
			int       length  = 0;
			int const code    = libssh2_session_last_error( session, &message, &length, 0 );
			QString   detail  = QString::fromUtf8( message == nullptr ? "" : message, length );
			if( detail.isEmpty() )
				detail = QObject::tr( "libssh2 error %1" ).arg( code );
			return Error{ ErrorKind::SFTP, context + QLatin1String( ": " ) + detail, code };
		}

	} // namespace

	Status receive_file( LIBSSH2_SESSION* session, QString const& remote_path, QString const& local_path,
						 ProgressCallback const& on_progress ){
		libssh2_struct_stat file_info{};
		QByteArray const    remote = remote_path.toUtf8();

		LIBSSH2_CHANNEL* channel = libssh2_scp_recv2( session, remote.constData(), &file_info );
		if( channel == nullptr )
			return std::unexpected(
				session_error( session, QObject::tr( "SCP download of %1 failed" ).arg( remote_path ) ) );

		struct ChannelGuard
		{
			LIBSSH2_CHANNEL* channel;
			~ChannelGuard(){
				libssh2_channel_close( channel );
				libssh2_channel_free( channel );
			}
		} guard{ channel };

		QFile output( local_path );
		if( !output.open( QIODevice::WriteOnly | QIODevice::Truncate ) ){
			return fail( ErrorKind::LOCAL_IO,
						 QObject::tr( "Cannot write %1: %2" ).arg( local_path, output.errorString() ) );
		}

		auto const total    = static_cast<quint64>( file_info.st_size );
		quint64    received = 0;
		QByteArray buffer( CHUNK_SIZE, Qt::Uninitialized );

		while( received < total ){
			auto const wanted = static_cast<qint64>( std::min<quint64>( CHUNK_SIZE, total - received ) );
			auto const count  = libssh2_channel_read( channel, buffer.data(), static_cast<size_t>( wanted ) );

			if( count == LIBSSH2_ERROR_EAGAIN )
				continue;
			if( count < 0 ){
				output.remove();
				return std::unexpected(
					session_error( session, QObject::tr( "SCP download of %1 failed" ).arg( remote_path ) ) );
			}
			if( count == 0 )
				break;

			if( output.write( buffer.constData(), static_cast<qint64>( count ) ) != static_cast<qint64>( count ) ){
				output.remove();
				return fail( ErrorKind::LOCAL_IO,
							 QObject::tr( "Cannot write %1: %2" ).arg( local_path, output.errorString() ) );
			}

			received += static_cast<quint64>( count );

			if( on_progress && !on_progress( received, total ) ){
				output.close();
				output.remove();
				return cancelled();
			}
		}

		if( !output.flush() ){
			return fail( ErrorKind::LOCAL_IO,
						 QObject::tr( "Cannot flush %1: %2" ).arg( local_path, output.errorString() ) );
		}
		output.close();

		QFile::setPermissions( local_path, QFile::permissions( local_path ) | QFile::ReadOwner | QFile::WriteOwner );
		return {};
	}

	Status send_file( LIBSSH2_SESSION* session, QString const& local_path, QString const& remote_path,
					  ProgressCallback const& on_progress ){
		QFile input( local_path );
		if( !input.open( QIODevice::ReadOnly ) ){
			return fail( ErrorKind::LOCAL_IO,
						 QObject::tr( "Cannot read %1: %2" ).arg( local_path, input.errorString() ) );
		}

		QFileInfo const info( local_path );
		auto const      total = static_cast<quint64>( info.size() );

		// Carry over the executable bit; everything else uses a sane default.
		int mode = 0644;
		if( info.isExecutable() )
			mode = 0755;

		QByteArray const remote = remote_path.toUtf8();
		LIBSSH2_CHANNEL* channel =
			libssh2_scp_send64( session, remote.constData(), mode, static_cast<libssh2_int64_t>( total ),
								static_cast<time_t>( info.lastModified().toSecsSinceEpoch() ),
								static_cast<time_t>( info.lastRead().toSecsSinceEpoch() ) );

		if( channel == nullptr )
			return std::unexpected(
				session_error( session, QObject::tr( "SCP upload of %1 failed" ).arg( local_path ) ) );

		struct ChannelGuard
		{
			LIBSSH2_CHANNEL* channel;
			~ChannelGuard(){
				libssh2_channel_close( channel );
				libssh2_channel_free( channel );
			}
		} guard{ channel };

		quint64    sent = 0;
		QByteArray buffer( CHUNK_SIZE, Qt::Uninitialized );

		while( sent < total ){
			qint64 const read = input.read( buffer.data(), CHUNK_SIZE );
			if( read < 0 ){
				return fail( ErrorKind::LOCAL_IO,
							 QObject::tr( "Cannot read %1: %2" ).arg( local_path, input.errorString() ) );
			}
			if( read == 0 )
				break;

			qint64 offset = 0;
			while( offset < read ){
				auto const written =
					libssh2_channel_write( channel, buffer.constData() + offset, static_cast<size_t>( read - offset ) );
				if( written == LIBSSH2_ERROR_EAGAIN )
					continue;
				if( written < 0 ){
					return std::unexpected(
						session_error( session, QObject::tr( "SCP upload of %1 failed" ).arg( local_path ) ) );
				}
				offset += static_cast<qint64>( written );
			}

			sent += static_cast<quint64>( read );

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
