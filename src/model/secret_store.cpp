#include "model/secret_store.hpp"

#include <QLoggingCategory>
#include <QObject>

#ifdef Q_OS_MACOS
	#include <Security/Security.h>
#endif

Q_LOGGING_CATEGORY( lc_secrets, "arterm.secrets" )

namespace arterm::model
{
	namespace
	{

// Only the Keychain implementation needs these; on other platforms the store
// reports itself unavailable and every entry point is a stub.
#ifdef Q_OS_MACOS

		/// Keychain service name; the account is "<profile id>/<kind>" so a single
		/// profile can hold both a password and a passphrase.
		constexpr char SERVICE_NAME[] = "ArTerm";

		QByteArray account_key( QString const& account, QString const& kind ){
			return ( account + QLatin1Char( '/' ) + kind ).toUtf8();
		}

#endif

	} // namespace

	bool SecretStore::is_available(){
#ifdef Q_OS_MACOS
		return true;
#else
		return false;
#endif
	}

	QString SecretStore::backend_name(){
#ifdef Q_OS_MACOS
		return QObject::tr( "macOS Keychain" );
#else
		return QObject::tr( "none (secrets are not saved)" );
#endif
	}

#ifdef Q_OS_MACOS

	bool SecretStore::store( QString const& account, QString const& kind, QString const& secret ){
		QByteArray const key   = account_key( account, kind );
		QByteArray const value = secret.toUtf8();

		// Replace any existing item rather than accumulating duplicates.
		SecKeychainItemRef existing = nullptr;
		OSStatus status = SecKeychainFindGenericPassword( nullptr, static_cast<UInt32>( sizeof( SERVICE_NAME ) - 1 ),
														  SERVICE_NAME, static_cast<UInt32>( key.size() ),
														  key.constData(), nullptr, nullptr, &existing );

		if( status == errSecSuccess && existing != nullptr ){
			status = SecKeychainItemModifyAttributesAndData( existing, nullptr, static_cast<UInt32>( value.size() ),
															 value.constData() );
			CFRelease( existing );
		}
		else{
			status = SecKeychainAddGenericPassword( nullptr, static_cast<UInt32>( sizeof( SERVICE_NAME ) - 1 ),
													SERVICE_NAME, static_cast<UInt32>( key.size() ), key.constData(),
													static_cast<UInt32>( value.size() ), value.constData(), nullptr );
		}

		if( status != errSecSuccess ){
			qCWarning( lc_secrets ) << "cannot store secret for" << account << "status" << status;
			return false;
		}
		return true;
	}

	std::optional<QString> SecretStore::retrieve( QString const& account, QString const& kind ){
		QByteArray const key = account_key( account, kind );

		UInt32 length = 0;
		void*  data   = nullptr;

		OSStatus const status = SecKeychainFindGenericPassword(
			nullptr, static_cast<UInt32>( sizeof( SERVICE_NAME ) - 1 ), SERVICE_NAME, static_cast<UInt32>( key.size() ),
			key.constData(), &length, &data, nullptr );

		if( status != errSecSuccess || data == nullptr )
			return std::nullopt;

		QString const secret = QString::fromUtf8( static_cast<char const*>( data ), static_cast<qsizetype>( length ) );
		SecKeychainItemFreeContent( nullptr, data );
		return secret;
	}

	bool SecretStore::remove( QString const& account, QString const& kind ){
		QByteArray const key = account_key( account, kind );

		SecKeychainItemRef item   = nullptr;
		OSStatus const     status = SecKeychainFindGenericPassword(
            nullptr, static_cast<UInt32>( sizeof( SERVICE_NAME ) - 1 ), SERVICE_NAME, static_cast<UInt32>( key.size() ),
            key.constData(), nullptr, nullptr, &item );

		if( status != errSecSuccess || item == nullptr )
			return false;

		OSStatus const deleted = SecKeychainItemDelete( item );
		CFRelease( item );
		return deleted == errSecSuccess;
	}

#else // Not macOS.

	bool SecretStore::store( QString const& account, QString const&, QString const& ){
		qCInfo( lc_secrets ) << "no secure store on this platform; not saving the secret for" << account;
		return false;
	}

	std::optional<QString> SecretStore::retrieve( QString const&, QString const& ){
		return std::nullopt;
	}

	bool SecretStore::remove( QString const&, QString const& ){
		return false;
	}

#endif

	void SecretStore::remove_all( QString const& account ){
		remove( account, QLatin1String( PASSWORD ) );
		remove( account, QLatin1String( PASSPHRASE ) );
	}

} // namespace arterm::model
