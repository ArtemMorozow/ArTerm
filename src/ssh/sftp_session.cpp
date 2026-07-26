#include "ssh/sftp_session.hpp"

#include "ssh/scp_transfer.hpp"
#include "ssh/session_interaction.hpp"
#include "ssh/ssh_connection.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY( lc_ssh )

namespace arterm::ssh
{
	namespace
	{

		constexpr qint64 CHUNK_SIZE = 64 * 1024;

		/// Minimum gap between two progress signals, so a fast local transfer does not
		/// flood the GUI thread's event queue.
		constexpr qint64 PROGRESS_INTERVAL_MS = 80;

		QString join_remote( QString const& directory, QString const& name ){
			if( directory.isEmpty() || directory == QLatin1String( "/" ) )
				return QLatin1Char( '/' ) + name;
			if( directory.endsWith( QLatin1Char( '/' ) ) )
				return directory + name;
			return directory + QLatin1Char( '/' ) + name;
		}

		RemoteFileEntry entry_from_attributes( QString const& directory, QString const& name,
											   LIBSSH2_SFTP_ATTRIBUTES const& attributes ){
			RemoteFileEntry entry;
			entry.name = name;
			entry.path = join_remote( directory, name );

			if( attributes.flags & LIBSSH2_SFTP_ATTR_SIZE )
				entry.size = attributes.filesize;
			if( attributes.flags & LIBSSH2_SFTP_ATTR_UIDGID ){
				entry.uid = static_cast<quint32>( attributes.uid );
				entry.gid = static_cast<quint32>( attributes.gid );
			}
			if( attributes.flags & LIBSSH2_SFTP_ATTR_ACMODTIME )
				entry.modified = QDateTime::fromSecsSinceEpoch( static_cast<qint64>( attributes.mtime ) );
			if( attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS ){
				entry.permissions   = static_cast<quint32>( attributes.permissions & 0xFFF );
				entry.is_directory  = LIBSSH2_SFTP_S_ISDIR( attributes.permissions );
				entry.is_symlink    = LIBSSH2_SFTP_S_ISLNK( attributes.permissions );
				entry.is_executable = ( attributes.permissions & 0111 ) != 0;
			}

			return entry;
		}

	} // namespace

	SftpSession::SftpSession( HostProfile profile, std::shared_ptr<SessionInteraction> interaction, QObject* parent )
		: QObject( parent )
		, _profile( std::move( profile ) )
		, _interaction( std::move( interaction ) )
	{}

	SftpSession::~SftpSession(){
		shutdown();
	}

	void SftpSession::start(){
		_connection = std::make_unique<SshConnection>( _profile );

		if( _interaction ){
			auto interaction = _interaction;
			_connection->set_host_key_prompt(
				[interaction]( HostKeyInfo const& info ) { return interaction->confirm_host_key( info ); } );
			_connection->set_credential_prompt( [interaction]( QString const& prompt, bool echo ){
				return interaction->ask_credential( prompt, echo );
			} );
		}

		if( auto status = _connection->open(); !status ){
			_connection.reset();
			Q_EMIT failed( status.error() );
			return;
		}

		// SFTP runs blocking: this worker thread exists precisely so long transfers
		// never touch the GUI thread.
		_connection->set_blocking( true );

		_sftp = libssh2_sftp_init( _connection->session() );
		if( _sftp == nullptr ){
			auto error =
				_connection->last_error( ErrorKind::SFTP, QObject::tr( "The server refused the SFTP subsystem" ) );
			_connection.reset();
			Q_EMIT failed( error );
			return;
		}

		// Resolve "." to learn the login directory.
		char      resolved[1024] = {};
		int const length         = libssh2_sftp_realpath( _sftp, ".", resolved, sizeof( resolved ) - 1 );
		_home_directory          = length > 0 ? QString::fromUtf8( resolved, length ) : QStringLiteral( "/" );

		if( !_profile.startup_directory.isEmpty() )
			_home_directory = _profile.startup_directory;

		Q_EMIT ready( _home_directory );
	}

	void SftpSession::shutdown(){
		if( _sftp != nullptr ){
			libssh2_sftp_shutdown( _sftp );
			_sftp = nullptr;
		}
		_connection.reset();
	}

	void SftpSession::request_cancel( quint64 request_id ){
		std::lock_guard const lock( _cancel_mutex );
		_cancel_requests.insert( request_id );
	}

	bool SftpSession::is_cancelled( quint64 request_id ) const{
		std::lock_guard const lock( _cancel_mutex );
		return _cancel_requests.contains( request_id );
	}

	void SftpSession::clear_cancel( quint64 request_id ){
		std::lock_guard const lock( _cancel_mutex );
		_cancel_requests.remove( request_id );
	}

	Error SftpSession::sftp_error( QString const& context ) const{
		if( _sftp == nullptr )
			return Error{ ErrorKind::SFTP, context };

		auto const code = libssh2_sftp_last_error( _sftp );

		QString reason;
		switch( code ){
			case LIBSSH2_FX_NO_SUCH_FILE:
			case LIBSSH2_FX_NO_SUCH_PATH:
				reason = QObject::tr( "no such file or directory" );
				break;
			case LIBSSH2_FX_PERMISSION_DENIED:
				reason = QObject::tr( "permission denied" );
				break;
			case LIBSSH2_FX_FILE_ALREADY_EXISTS:
				reason = QObject::tr( "already exists" );
				break;
			case LIBSSH2_FX_DIR_NOT_EMPTY:
				reason = QObject::tr( "directory is not empty" );
				break;
			case LIBSSH2_FX_QUOTA_EXCEEDED:
				reason = QObject::tr( "quota exceeded" );
				break;
			case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM:
				reason = QObject::tr( "no space left on the remote filesystem" );
				break;
			case LIBSSH2_FX_OP_UNSUPPORTED:
				reason = QObject::tr( "operation not supported by the server" );
				break;
			case 0:
				// The failure came from the transport rather than the SFTP layer.
				return _connection != nullptr ? _connection->last_error( ErrorKind::SFTP, context )
											  : Error{ ErrorKind::SFTP, context };
			default:
				reason = QObject::tr( "SFTP status %1" ).arg( code );
				break;
		}

		return Error{ ErrorKind::SFTP, context + QLatin1String( ": " ) + reason, static_cast<int>( code ) };
	}

	// ---------------------------------------------------------------------------
	// Browsing
	// ---------------------------------------------------------------------------

	Result<RemoteListing> SftpSession::read_directory( QString const& path ){
		if( _sftp == nullptr )
			return fail( ErrorKind::SFTP, QObject::tr( "Not connected" ) );

		QByteArray const     utf8   = path.toUtf8();
		LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_opendir( _sftp, utf8.constData() );
		if( handle == nullptr )
			return std::unexpected( sftp_error( QObject::tr( "Cannot open %1" ).arg( path ) ) );

		struct HandleGuard
		{
			LIBSSH2_SFTP_HANDLE* handle;
			~HandleGuard() { libssh2_sftp_closedir( handle ); }
		} guard{ handle };

		RemoteListing           entries;
		char                    name[1024];
		LIBSSH2_SFTP_ATTRIBUTES attributes{};

		while( true ){
			int const length = libssh2_sftp_readdir_ex( handle, name, sizeof( name ), nullptr, 0, &attributes );
			if( length == 0 )
				break;
			if( length < 0 )
				return std::unexpected( sftp_error( QObject::tr( "Cannot read %1" ).arg( path ) ) );

			QString const file_name = QString::fromUtf8( name, length );
			if( file_name == QLatin1String( "." ) || file_name == QLatin1String( ".." ) )
				continue;

			RemoteFileEntry entry = entry_from_attributes( path, file_name, attributes );

			// readdir reports the link itself; resolve it so the browser can descend
			// into symlinked directories.
			if( entry.is_symlink ){
				LIBSSH2_SFTP_ATTRIBUTES target{};
				QByteArray const        target_path = entry.path.toUtf8();
				if( libssh2_sftp_stat_ex( _sftp, target_path.constData(),
										  static_cast<unsigned int>( target_path.size() ), LIBSSH2_SFTP_STAT,
										  &target ) == 0 ){
					if( target.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS )
						entry.is_directory = LIBSSH2_SFTP_S_ISDIR( target.permissions );
					if( target.flags & LIBSSH2_SFTP_ATTR_SIZE )
						entry.size = target.filesize;
				}
			}

			entries.append( std::move( entry ) );
		}

		// Directories first, then case-insensitive by name - the ordering every
		// file manager uses.
		std::sort( entries.begin(), entries.end(), []( RemoteFileEntry const& a, RemoteFileEntry const& b ){
			if( a.is_directory != b.is_directory )
				return a.is_directory;
			return a.name.compare( b.name, Qt::CaseInsensitive ) < 0;
		} );

		return entries;
	}

	Result<RemoteFileEntry> SftpSession::stat_entry( QString const& path ){
		if( _sftp == nullptr )
			return fail( ErrorKind::SFTP, QObject::tr( "Not connected" ) );

		LIBSSH2_SFTP_ATTRIBUTES attributes{};
		QByteArray const        utf8 = path.toUtf8();
		if( libssh2_sftp_stat_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ), LIBSSH2_SFTP_STAT,
								  &attributes ) != 0 ){
			return std::unexpected( sftp_error( QObject::tr( "Cannot stat %1" ).arg( path ) ) );
		}

		int const     slash     = path.lastIndexOf( QLatin1Char( '/' ) );
		QString const directory = slash > 0 ? path.left( slash ) : QStringLiteral( "/" );
		QString const name      = slash >= 0 ? path.mid( slash + 1 ) : path;

		return entry_from_attributes( directory, name, attributes );
	}

	void SftpSession::list_directory( quint64 request_id, QString const& path ){
		auto entries = read_directory( path );
		if( !entries ){
			Q_EMIT operation_failed( request_id, entries.error() );
			return;
		}
		Q_EMIT listing_ready( request_id, path, *entries );
	}

	void SftpSession::resolve_path( quint64 request_id, QString const& path ){
		if( _sftp == nullptr ){
			Q_EMIT operation_failed( request_id, Error{ ErrorKind::SFTP, QObject::tr( "Not connected" ) } );
			return;
		}

		char             resolved[1024] = {};
		QByteArray const utf8           = path.toUtf8();
		int const        length = libssh2_sftp_realpath( _sftp, utf8.constData(), resolved, sizeof( resolved ) - 1 );
		if( length < 0 ){
			Q_EMIT operation_failed( request_id, sftp_error( QObject::tr( "Cannot resolve %1" ).arg( path ) ) );
			return;
		}

		Q_EMIT path_resolved( request_id, QString::fromUtf8( resolved, length ) );
	}

	void SftpSession::make_directory( quint64 request_id, QString const& path ){
		QByteArray const utf8 = path.toUtf8();
		if( libssh2_sftp_mkdir_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ), 0755 ) != 0 ){
			Q_EMIT operation_failed( request_id, sftp_error( QObject::tr( "Cannot create %1" ).arg( path ) ) );
			return;
		}
		Q_EMIT operation_finished( request_id );
	}

	void SftpSession::rename_entry( quint64 request_id, QString const& from, QString const& to ){
		QByteArray const source      = from.toUtf8();
		QByteArray const destination = to.toUtf8();

		int const rc = libssh2_sftp_rename_ex( _sftp, source.constData(), static_cast<unsigned int>( source.size() ),
											   destination.constData(), static_cast<unsigned int>( destination.size() ),
											   LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC |
												   LIBSSH2_SFTP_RENAME_NATIVE );

		if( rc != 0 ){
			Q_EMIT operation_failed( request_id,
									 sftp_error( QObject::tr( "Cannot rename %1 to %2" ).arg( from, to ) ) );
			return;
		}
		Q_EMIT operation_finished( request_id );
	}

	void SftpSession::change_permissions( quint64 request_id, QString const& path, quint32 mode ){
		LIBSSH2_SFTP_ATTRIBUTES attributes{};
		attributes.flags       = LIBSSH2_SFTP_ATTR_PERMISSIONS;
		attributes.permissions = mode;

		QByteArray const utf8 = path.toUtf8();
		if( libssh2_sftp_stat_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ),
								  LIBSSH2_SFTP_SETSTAT, &attributes ) != 0 ){
			Q_EMIT operation_failed( request_id,
									 sftp_error( QObject::tr( "Cannot change the mode of %1" ).arg( path ) ) );
			return;
		}
		Q_EMIT operation_finished( request_id );
	}

	void SftpSession::remove_entry( quint64 request_id, QString const& path, bool recursive ){
		auto info = stat_entry( path );
		if( !info ){
			Q_EMIT operation_failed( request_id, info.error() );
			return;
		}

		Status result;
		if( info->is_directory && !info->is_symlink ){
			if( recursive ){
				result = remove_tree( request_id, path );
			}
			else{
				QByteArray const utf8 = path.toUtf8();
				if( libssh2_sftp_rmdir_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ) ) != 0 )
					result = std::unexpected( sftp_error( QObject::tr( "Cannot remove %1" ).arg( path ) ) );
			}
		}
		else{
			QByteArray const utf8 = path.toUtf8();
			if( libssh2_sftp_unlink_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ) ) != 0 )
				result = std::unexpected( sftp_error( QObject::tr( "Cannot delete %1" ).arg( path ) ) );
		}

		clear_cancel( request_id );

		if( !result ){
			Q_EMIT operation_failed( request_id, result.error() );
			return;
		}
		Q_EMIT operation_finished( request_id );
	}

	Status SftpSession::remove_tree( quint64 request_id, QString const& path ){
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
			else{
				QByteArray const utf8 = entry.path.toUtf8();
				if( libssh2_sftp_unlink_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ) ) != 0 )
					return std::unexpected( sftp_error( QObject::tr( "Cannot delete %1" ).arg( entry.path ) ) );
			}
		}

		QByteArray const utf8 = path.toUtf8();
		if( libssh2_sftp_rmdir_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ) ) != 0 )
			return std::unexpected( sftp_error( QObject::tr( "Cannot remove %1" ).arg( path ) ) );

		return {};
	}

	// ---------------------------------------------------------------------------
	// Transfers
	// ---------------------------------------------------------------------------

	void SftpSession::publish_progress( quint64 request_id, quint64 moved, quint64 total ){
		static thread_local QElapsedTimer timer;
		if( !timer.isValid() )
			timer.start();

		qint64 const now = timer.elapsed();
		if( _last_progress_tick != 0 && now - _last_progress_tick < PROGRESS_INTERVAL_MS && moved < total )
			return;

		TransferProgress progress;
		progress.transferred = moved;
		progress.total       = total;

		if( _last_progress_tick != 0 && now > _last_progress_tick ){
			double const seconds      = static_cast<double>( now - _last_progress_tick ) / 1000.0;
			progress.bytes_per_second = static_cast<double>( moved - _last_progress_bytes ) / seconds;
		}

		_last_progress_tick  = now;
		_last_progress_bytes = moved;

		Q_EMIT transfer_progress( request_id, progress );
	}

	Result<quint64> SftpSession::remote_tree_size( QString const& path ){
		auto info = stat_entry( path );
		if( !info )
			return std::unexpected( info.error() );

		if( !info->is_directory )
			return info->size;

		auto entries = read_directory( path );
		if( !entries )
			return std::unexpected( entries.error() );

		quint64 total = 0;
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

	quint64 SftpSession::local_tree_size( QString const& path ){
		QFileInfo const info( path );
		if( !info.isDir() )
			return static_cast<quint64>( info.size() );

		quint64      total = 0;
		QDirIterator iterator( path, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories );
		while( iterator.hasNext() ){
			iterator.next();
			total += static_cast<quint64>( iterator.fileInfo().size() );
		}
		return total;
	}

	void SftpSession::download( quint64 request_id, QString const& remote_path, QString const& local_path ){
		_last_progress_tick  = 0;
		_last_progress_bytes = 0;

		auto total = remote_tree_size( remote_path );
		if( !total ){
			clear_cancel( request_id );
			Q_EMIT operation_failed( request_id, total.error() );
			return;
		}

		Q_EMIT transfer_started( request_id, *total );

		quint64 moved  = 0;
		auto    status = download_tree( request_id, remote_path, local_path, *total, moved );

		clear_cancel( request_id );

		if( !status ){
			Q_EMIT operation_failed( request_id, status.error() );
			return;
		}

		publish_progress( request_id, *total, *total );
		Q_EMIT transfer_finished( request_id );
	}

	Status SftpSession::download_tree( quint64 request_id, QString const& remote_path, QString const& local_path,
									   quint64 total, quint64& moved ){
		if( is_cancelled( request_id ) )
			return cancelled();

		auto info = stat_entry( remote_path );
		if( !info )
			return std::unexpected( info.error() );

		if( !info->is_directory )
			return download_file( request_id, remote_path, local_path, total, moved );

		if( !QDir().mkpath( local_path ) )
			return fail( ErrorKind::LOCAL_IO, QObject::tr( "Cannot create %1" ).arg( local_path ) );

		auto entries = read_directory( remote_path );
		if( !entries )
			return std::unexpected( entries.error() );

		for( RemoteFileEntry const& entry : *entries ){
			QString const target = QDir( local_path ).filePath( entry.name );
			if( auto status = download_tree( request_id, entry.path, target, total, moved ); !status )
				return status;
		}

		return {};
	}

	Status SftpSession::download_file( quint64 request_id, QString const& remote_path, QString const& local_path,
									   quint64 total, quint64& moved ){
		if( _backend == TransferBackend::SCP ){
			quint64 const base   = moved;
			auto          status = scp::receive_file( _connection->session(), remote_path, local_path,
													  [this, request_id, base, total, &moved]( quint64 transferred, quint64 ){
                                                 moved = base + transferred;
                                                 publish_progress( request_id, moved, total );
                                                 return !is_cancelled( request_id );
                                             } );
			return status;
		}

		QByteArray const     utf8 = remote_path.toUtf8();
		LIBSSH2_SFTP_HANDLE* handle =
			libssh2_sftp_open_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ), LIBSSH2_FXF_READ,
								  0, LIBSSH2_SFTP_OPENFILE );
		if( handle == nullptr )
			return std::unexpected( sftp_error( QObject::tr( "Cannot open %1" ).arg( remote_path ) ) );

		struct HandleGuard
		{
			LIBSSH2_SFTP_HANDLE* handle;
			~HandleGuard() { libssh2_sftp_close_handle( handle ); }
		} guard{ handle };

		QFile output( local_path );
		if( !output.open( QIODevice::WriteOnly | QIODevice::Truncate ) ){
			return fail( ErrorKind::LOCAL_IO,
						 QObject::tr( "Cannot write %1: %2" ).arg( local_path, output.errorString() ) );
		}

		QByteArray buffer( CHUNK_SIZE, Qt::Uninitialized );

		while( true ){
			if( is_cancelled( request_id ) ){
				output.close();
				output.remove();
				return cancelled();
			}

			auto const count = libssh2_sftp_read( handle, buffer.data(), CHUNK_SIZE );
			if( count == 0 )
				break;
			if( count < 0 ){
				output.close();
				output.remove();
				return std::unexpected( sftp_error( QObject::tr( "Cannot read %1" ).arg( remote_path ) ) );
			}

			if( output.write( buffer.constData(), static_cast<qint64>( count ) ) != static_cast<qint64>( count ) ){
				QString const reason = output.errorString();
				output.close();
				output.remove();
				return fail( ErrorKind::LOCAL_IO, QObject::tr( "Cannot write %1: %2" ).arg( local_path, reason ) );
			}

			moved += static_cast<quint64>( count );
			publish_progress( request_id, moved, total );
		}

		if( !output.flush() ){
			return fail( ErrorKind::LOCAL_IO,
						 QObject::tr( "Cannot flush %1: %2" ).arg( local_path, output.errorString() ) );
		}

		return {};
	}

	void SftpSession::upload( quint64 request_id, QString const& local_path, QString const& remote_path ){
		_last_progress_tick  = 0;
		_last_progress_bytes = 0;

		if( !QFileInfo::exists( local_path ) ){
			clear_cancel( request_id );
			Q_EMIT operation_failed(
				request_id, Error{ ErrorKind::LOCAL_IO, QObject::tr( "%1 does not exist" ).arg( local_path ) } );
			return;
		}

		quint64 const total = local_tree_size( local_path );
		Q_EMIT transfer_started( request_id, total );

		quint64 moved  = 0;
		auto    status = upload_tree( request_id, local_path, remote_path, total, moved );

		clear_cancel( request_id );

		if( !status ){
			Q_EMIT operation_failed( request_id, status.error() );
			return;
		}

		publish_progress( request_id, total, total );
		Q_EMIT transfer_finished( request_id );
	}

	Status SftpSession::upload_tree( quint64 request_id, QString const& local_path, QString const& remote_path,
									 quint64 total, quint64& moved ){
		if( is_cancelled( request_id ) )
			return cancelled();

		QFileInfo const info( local_path );
		if( !info.isDir() )
			return upload_file( request_id, local_path, remote_path, total, moved );

		QByteArray const utf8 = remote_path.toUtf8();
		int const rc = libssh2_sftp_mkdir_ex( _sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ), 0755 );
		if( rc != 0 && libssh2_sftp_last_error( _sftp ) != LIBSSH2_FX_FILE_ALREADY_EXISTS )
			return std::unexpected( sftp_error( QObject::tr( "Cannot create %1" ).arg( remote_path ) ) );

		QDir const directory( local_path );
		auto const children = directory.entryInfoList( QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden );

		for( QFileInfo const& child : children ){
			QString const target = join_remote( remote_path, child.fileName() );
			if( auto status = upload_tree( request_id, child.absoluteFilePath(), target, total, moved ); !status )
				return status;
		}

		return {};
	}

	Status SftpSession::upload_file( quint64 request_id, QString const& local_path, QString const& remote_path,
									 quint64 total, quint64& moved ){
		if( _backend == TransferBackend::SCP ){
			quint64 const base = moved;
			return scp::send_file( _connection->session(), local_path, remote_path,
								   [this, request_id, base, total, &moved]( quint64 transferred, quint64 ){
									   moved = base + transferred;
									   publish_progress( request_id, moved, total );
									   return !is_cancelled( request_id );
								   } );
		}

		QFile input( local_path );
		if( !input.open( QIODevice::ReadOnly ) ){
			return fail( ErrorKind::LOCAL_IO,
						 QObject::tr( "Cannot read %1: %2" ).arg( local_path, input.errorString() ) );
		}

		long const mode = QFileInfo( local_path ).isExecutable() ? 0755 : 0644;

		QByteArray const     utf8   = remote_path.toUtf8();
		LIBSSH2_SFTP_HANDLE* handle = libssh2_sftp_open_ex(
			_sftp, utf8.constData(), static_cast<unsigned int>( utf8.size() ),
			LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, mode, LIBSSH2_SFTP_OPENFILE );
		if( handle == nullptr )
			return std::unexpected( sftp_error( QObject::tr( "Cannot create %1" ).arg( remote_path ) ) );

		struct HandleGuard
		{
			LIBSSH2_SFTP_HANDLE* handle;
			~HandleGuard() { libssh2_sftp_close_handle( handle ); }
		} guard{ handle };

		QByteArray buffer( CHUNK_SIZE, Qt::Uninitialized );

		while( true ){
			if( is_cancelled( request_id ) )
				return cancelled();

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
					libssh2_sftp_write( handle, buffer.constData() + offset, static_cast<size_t>( read - offset ) );
				if( written < 0 )
					return std::unexpected( sftp_error( QObject::tr( "Cannot write %1" ).arg( remote_path ) ) );

				offset += static_cast<qint64>( written );
				moved += static_cast<quint64>( written );
				publish_progress( request_id, moved, total );
			}
		}

		return {};
	}

} // namespace arterm::ssh
