#include "model/host_store.hpp"

#include "model/secret_store.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

Q_LOGGING_CATEGORY( lc_hosts, "arterm.hosts" )

namespace arterm::model
{
	namespace
	{

		QString auth_method_key( ssh::AuthMethod method ){
			switch( method ){
				case ssh::AuthMethod::AGENT:
					return QStringLiteral( "agent" );
				case ssh::AuthMethod::PUBLIC_KEY:
					return QStringLiteral( "publickey" );
				case ssh::AuthMethod::PASSWORD:
					return QStringLiteral( "password" );
				case ssh::AuthMethod::KEYBOARD_INTERACTIVE:
					return QStringLiteral( "keyboard-interactive" );
			}
			return QStringLiteral( "agent" );
		}

		QString transfer_backend_key( ssh::TransferBackend backend ){
			return backend == ssh::TransferBackend::SCP ? QStringLiteral( "scp" ) : QStringLiteral( "sftp" );
		}

		ssh::TransferBackend transfer_backend_from_key( QString const& key ){
			return key == QLatin1String( "scp" ) ? ssh::TransferBackend::SCP : ssh::TransferBackend::SFTP;
		}

		ssh::AuthMethod auth_method_from_key( QString const& key ){
			if( key == QLatin1String( "publickey" ) )
				return ssh::AuthMethod::PUBLIC_KEY;
			if( key == QLatin1String( "password" ) )
				return ssh::AuthMethod::PASSWORD;
			if( key == QLatin1String( "keyboard-interactive" ) )
				return ssh::AuthMethod::KEYBOARD_INTERACTIVE;
			return ssh::AuthMethod::AGENT;
		}

		QJsonObject to_json( ssh::HostProfile const& profile ){
			QJsonObject object;
			object.insert( QStringLiteral( "id" ), profile.id );
			object.insert( QStringLiteral( "label" ), profile.label );
			object.insert( QStringLiteral( "hostname" ), profile.hostname );
			object.insert( QStringLiteral( "port" ), static_cast<int>( profile.port ) );
			object.insert( QStringLiteral( "username" ), profile.username );
			object.insert( QStringLiteral( "group" ), profile.group );
			object.insert( QStringLiteral( "colorTag" ), profile.color_tag );
			object.insert( QStringLiteral( "auth" ), auth_method_key( profile.preferred_auth ) );
			object.insert( QStringLiteral( "privateKeyPath" ), profile.private_key_path );
			object.insert( QStringLiteral( "useAgent" ), profile.use_agent );
			object.insert( QStringLiteral( "startupDirectory" ), profile.startup_directory );
			object.insert( QStringLiteral( "startupCommand" ), profile.startup_command );
			object.insert( QStringLiteral( "openFileBrowser" ), profile.open_file_browser );
			object.insert( QStringLiteral( "keepAliveSeconds" ), profile.keep_alive_seconds );
			object.insert( QStringLiteral( "compression" ), profile.compression );
			object.insert( QStringLiteral( "strictHostKeyChecking" ), profile.strict_host_key_checking );
			object.insert( QStringLiteral( "transferBackend" ), transfer_backend_key( profile.transfer_backend ) );
			// Passwords and passphrases are intentionally absent: they belong to the
			// keychain, keyed by `id`.
			return object;
		}

		ssh::HostProfile from_json( QJsonObject const& object ){
			ssh::HostProfile profile;
			profile.id                 = object.value( QStringLiteral( "id" ) ).toString();
			profile.label              = object.value( QStringLiteral( "label" ) ).toString();
			profile.hostname           = object.value( QStringLiteral( "hostname" ) ).toString();
			profile.port               = static_cast<quint16>( object.value( QStringLiteral( "port" ) ).toInt( 22 ) );
			profile.username           = object.value( QStringLiteral( "username" ) ).toString();
			profile.group              = object.value( QStringLiteral( "group" ) ).toString();
			profile.color_tag          = object.value( QStringLiteral( "colorTag" ) ).toString();
			profile.preferred_auth     = auth_method_from_key( object.value( QStringLiteral( "auth" ) ).toString() );
			profile.private_key_path   = object.value( QStringLiteral( "privateKeyPath" ) ).toString();
			profile.use_agent          = object.value( QStringLiteral( "useAgent" ) ).toBool( true );
			profile.startup_directory  = object.value( QStringLiteral( "startupDirectory" ) ).toString();
			profile.startup_command    = object.value( QStringLiteral( "startupCommand" ) ).toString();
			profile.open_file_browser  = object.value( QStringLiteral( "openFileBrowser" ) ).toBool( true );
			profile.keep_alive_seconds = object.value( QStringLiteral( "keepAliveSeconds" ) ).toInt( 30 );
			profile.compression        = object.value( QStringLiteral( "compression" ) ).toBool( false );
			profile.strict_host_key_checking = object.value( QStringLiteral( "strictHostKeyChecking" ) ).toBool( true );
			profile.transfer_backend =
				transfer_backend_from_key( object.value( QStringLiteral( "transferBackend" ) ).toString() );
			return profile;
		}

	} // namespace

	HostStore::HostStore( QObject* parent )
		: QObject( parent )
		, _file_path( default_file_path() )
	{}

	QString HostStore::default_file_path(){
		QString const directory = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
		return directory + QLatin1String( "/hosts.json" );
	}

	int HostStore::index_of( QString const& id ) const{
		for( int i = 0; i < _profiles.size(); ++i ){
			if( _profiles.at( i ).id == id )
				return i;
		}
		return -1;
	}

	std::optional<ssh::HostProfile> HostStore::profile_by_id( QString const& id ) const{
		int const index = index_of( id );
		if( index < 0 )
			return std::nullopt;
		return _profiles.at( index );
	}

	QStringList HostStore::groups() const{
		QStringList result;
		for( ssh::HostProfile const& profile : _profiles ){
			QString const group = profile.group.isEmpty() ? tr( "Hosts" ) : profile.group;
			if( !result.contains( group ) )
				result << group;
		}
		result.sort( Qt::CaseInsensitive );
		return result;
	}

	QString HostStore::add( ssh::HostProfile profile ){
		if( profile.id.isEmpty() )
			profile.id = QUuid::createUuid().toString( QUuid::WithoutBraces );

		save_secrets( profile );
		profile.password.clear();
		profile.key_passphrase.clear();

		_profiles.append( profile );

		if( !save() )
			qCWarning( lc_hosts ) << "cannot persist the host list";

		Q_EMIT profile_added( profile.id );
		Q_EMIT changed();
		return profile.id;
	}

	void HostStore::update( ssh::HostProfile const& profile ){
		int const index = index_of( profile.id );
		if( index < 0 )
			return;

		save_secrets( profile );

		ssh::HostProfile stored = profile;
		stored.password.clear();
		stored.key_passphrase.clear();
		_profiles[index] = stored;

		if( !save() )
			qCWarning( lc_hosts ) << "cannot persist the host list";

		Q_EMIT profile_updated( profile.id );
		Q_EMIT changed();
	}

	void HostStore::remove( QString const& id ){
		int const index = index_of( id );
		if( index < 0 )
			return;

		_profiles.removeAt( index );
		SecretStore::remove_all( id );

		if( !save() )
			qCWarning( lc_hosts ) << "cannot persist the host list";

		Q_EMIT profile_removed( id );
		Q_EMIT changed();
	}

	void HostStore::save_secrets( ssh::HostProfile const& profile ){
		if( !SecretStore::is_available() )
			return;

		if( !profile.password.isEmpty() )
			static_cast<void>(
				SecretStore::store( profile.id, QLatin1String( SecretStore::PASSWORD ), profile.password ) );
		if( !profile.key_passphrase.isEmpty() )
			static_cast<void>(
				SecretStore::store( profile.id, QLatin1String( SecretStore::PASSPHRASE ), profile.key_passphrase ) );
	}

	ssh::HostProfile HostStore::with_secrets( QString const& id ) const{
		int const index = index_of( id );
		if( index < 0 )
			return {};

		ssh::HostProfile profile = _profiles.at( index );

		if( auto const password = SecretStore::retrieve( id, QLatin1String( SecretStore::PASSWORD ) ) )
			profile.password = *password;
		if( auto const passphrase = SecretStore::retrieve( id, QLatin1String( SecretStore::PASSPHRASE ) ) )
			profile.key_passphrase = *passphrase;

		return profile;
	}

	bool HostStore::load(){
		QFile file( _file_path );
		if( !file.exists() )
			return true; // A fresh installation, not an error.

		if( !file.open( QIODevice::ReadOnly ) ){
			qCWarning( lc_hosts ) << "cannot read" << _file_path << file.errorString();
			return false;
		}

		QJsonParseError     error{};
		QJsonDocument const document = QJsonDocument::fromJson( file.readAll(), &error );
		if( error.error != QJsonParseError::NoError ){
			qCWarning( lc_hosts ) << "cannot parse" << _file_path << error.errorString();
			return false;
		}

		QJsonArray const array =
			document.isArray() ? document.array() : document.object().value( QStringLiteral( "hosts" ) ).toArray();

		_profiles.clear();
		_profiles.reserve( array.size() );
		for( QJsonValue const& value : array ){
			if( !value.isObject() )
				continue;
			ssh::HostProfile profile = from_json( value.toObject() );
			if( profile.id.isEmpty() )
				profile.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
			if( !profile.hostname.isEmpty() )
				_profiles.append( std::move( profile ) );
		}

		Q_EMIT changed();
		return true;
	}

	bool HostStore::save() const{
		QDir().mkpath( QFileInfo( _file_path ).absolutePath() );

		QJsonArray array;
		for( ssh::HostProfile const& profile : _profiles )
			array.append( to_json( profile ) );

		QJsonObject root;
		root.insert( QStringLiteral( "version" ), 1 );
		root.insert( QStringLiteral( "hosts" ), array );

		// QSaveFile writes to a temporary and renames, so a crash mid-write cannot
		// leave the user without a host list.
		QSaveFile file( _file_path );
		if( !file.open( QIODevice::WriteOnly ) ){
			qCWarning( lc_hosts ) << "cannot open" << _file_path << file.errorString();
			return false;
		}

		file.write( QJsonDocument( root ).toJson( QJsonDocument::Indented ) );
		if( !file.commit() ){
			qCWarning( lc_hosts ) << "cannot commit" << _file_path << file.errorString();
			return false;
		}

		QFile::setPermissions( _file_path, QFile::ReadOwner | QFile::WriteOwner );
		return true;
	}

	int HostStore::import_from_ssh_config( QString const& config_path ){
		QString const path = config_path.isEmpty() ? QDir::homePath() + QLatin1String( "/.ssh/config" ) : config_path;

		QFile file( path );
		if( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
			return 0;

		QTextStream stream( &file );

		int              imported = 0;
		ssh::HostProfile current;
		bool             in_host_block = false;

		auto const flush = [&]{
			if( !in_host_block || current.hostname.isEmpty() )
				return;

			// Skip hosts already known by endpoint so a second import is a no-op.
			bool const duplicate =
				std::any_of( _profiles.begin(), _profiles.end(), [&current]( ssh::HostProfile const& existing ){
					return existing.hostname == current.hostname && existing.port == current.port &&
						   existing.username == current.username;
				} );
			if( duplicate ){
				current = {};
				return;
			}

			current.id    = QUuid::createUuid().toString( QUuid::WithoutBraces );
			current.group = tr( "Imported" );
			_profiles.append( current );
			++imported;
			current = {};
		};

		while( !stream.atEnd() ){
			QString line = stream.readLine().trimmed();
			if( line.isEmpty() || line.startsWith( QLatin1Char( '#' ) ) )
				continue;

			// Keywords are case-insensitive and separated by whitespace or '='.
			line.replace( QLatin1Char( '=' ), QLatin1Char( ' ' ) );
			QString const keyword = line.section( QLatin1Char( ' ' ), 0, 0 ).toLower();
			QString const value   = line.section( QLatin1Char( ' ' ), 1 ).trimmed();
			if( value.isEmpty() )
				continue;

			if( keyword == QLatin1String( "host" ) ){
				flush();
				// Patterns cannot be connected to, so they are not imported.
				if( value.contains( QLatin1Char( '*' ) ) || value.contains( QLatin1Char( '?' ) ) ){
					in_host_block = false;
					continue;
				}
				in_host_block    = true;
				current          = {};
				current.label    = value.section( QLatin1Char( ' ' ), 0, 0 );
				current.hostname = current.label;
				current.username = qEnvironmentVariable( "USER" );
			}
			else if( !in_host_block ){
				continue;
			}
			else if( keyword == QLatin1String( "hostname" ) ){
				current.hostname = value;
			}
			else if( keyword == QLatin1String( "user" ) ){
				current.username = value;
			}
			else if( keyword == QLatin1String( "port" ) ){
				current.port = static_cast<quint16>( value.toUInt() );
			}
			else if( keyword == QLatin1String( "identityfile" ) ){
				current.private_key_path = value;
				current.preferred_auth   = ssh::AuthMethod::PUBLIC_KEY;
			}
			else if( keyword == QLatin1String( "compression" ) ){
				current.compression = value.compare( QLatin1String( "yes" ), Qt::CaseInsensitive ) == 0;
			}
		}

		flush();

		if( imported > 0 ){
			static_cast<void>( save() );
			Q_EMIT changed();
		}

		return imported;
	}

} // namespace arterm::model
