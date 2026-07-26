#include "model/secret_store.hpp"

#include "core/log.hpp"

#include <Security/Security.h>

namespace arterm::model
{
	namespace
	{

		/// Keychain service name; the account is "<profile id>/<kind>" so a single
		/// profile can hold both a password and a passphrase.
		constexpr char SERVICE_NAME[] = "ArTerm";

		std::string account_key( std::string const& account, std::string const& kind ){
			return account + "/" + kind;
		}

		/// Owns one CoreFoundation reference.
		template <typename Ref_>
		class CfRef
		{
		public:
			CfRef() = default;
			explicit CfRef( Ref_ ref ) noexcept
				: _ref( ref )
			{}

			CfRef( CfRef const& )            = delete;
			CfRef& operator=( CfRef const& ) = delete;

			CfRef( CfRef&& other ) noexcept
				: _ref( other._ref )
			{
				other._ref = nullptr;
			}

			~CfRef(){
				if( _ref != nullptr )
					CFRelease( _ref );
			}

			[[nodiscard]] Ref_ get() const noexcept { return _ref; }

			/// For out-parameters: releases the old value and exposes the slot.
			[[nodiscard]] Ref_* slot() noexcept{
				if( _ref != nullptr ){
					CFRelease( _ref );
					_ref = nullptr;
				}
				return &_ref;
			}

		private:
			Ref_ _ref{ nullptr };
		};

		CfRef<CFStringRef> cf_string( std::string const& text ){
			return CfRef<CFStringRef>(
				CFStringCreateWithBytes( kCFAllocatorDefault, reinterpret_cast<UInt8 const*>( text.data() ),
										 static_cast<CFIndex>( text.size() ), kCFStringEncodingUTF8, false ) );
		}

		CfRef<CFDataRef> cf_data( std::string const& bytes ){
			return CfRef<CFDataRef>( CFDataCreate( kCFAllocatorDefault, reinterpret_cast<UInt8 const*>( bytes.data() ),
												   static_cast<CFIndex>( bytes.size() ) ) );
		}

		/// The lookup dictionary shared by every operation.
		CfRef<CFMutableDictionaryRef> base_query( std::string const& key ){
			CfRef<CFMutableDictionaryRef> query( CFDictionaryCreateMutable(
				kCFAllocatorDefault, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks ) );

			auto const service = cf_string( SERVICE_NAME );
			auto const account = cf_string( key );
			CFDictionarySetValue( query.get(), kSecClass, kSecClassGenericPassword );
			CFDictionarySetValue( query.get(), kSecAttrService, service.get() );
			CFDictionarySetValue( query.get(), kSecAttrAccount, account.get() );
			return query;
		}

	} // namespace

	std::string SecretStore::backend_name(){
		return "macOS Keychain";
	}

	bool SecretStore::store( std::string const& account, std::string const& kind, std::string const& secret ){
		auto const query = base_query( account_key( account, kind ) );
		auto const value = cf_data( secret );

		// Update in place first so repeated saves do not accumulate duplicates.
		CfRef<CFMutableDictionaryRef> update( CFDictionaryCreateMutable(
			kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks ) );
		CFDictionarySetValue( update.get(), kSecValueData, value.get() );

		OSStatus status = SecItemUpdate( query.get(), update.get() );
		if( status == errSecItemNotFound ){
			CFDictionarySetValue( query.get(), kSecValueData, value.get() );
			status = SecItemAdd( query.get(), nullptr );
		}

		if( status != errSecSuccess ){
			log_warning( "secrets", "cannot store secret for {}: status {}", account, status );
			return false;
		}
		return true;
	}

	std::optional<std::string> SecretStore::retrieve( std::string const& account, std::string const& kind ){
		auto const query = base_query( account_key( account, kind ) );
		CFDictionarySetValue( query.get(), kSecReturnData, kCFBooleanTrue );
		CFDictionarySetValue( query.get(), kSecMatchLimit, kSecMatchLimitOne );

		CfRef<CFTypeRef> result;
		if( SecItemCopyMatching( query.get(), result.slot() ) != errSecSuccess || result.get() == nullptr )
			return std::nullopt;

		auto const data = static_cast<CFDataRef>( result.get() );
		return std::string( reinterpret_cast<char const*>( CFDataGetBytePtr( data ) ),
							static_cast<std::size_t>( CFDataGetLength( data ) ) );
	}

	bool SecretStore::remove( std::string const& account, std::string const& kind ){
		auto const query = base_query( account_key( account, kind ) );
		return SecItemDelete( query.get() ) == errSecSuccess;
	}

	void SecretStore::remove_all( std::string const& account ){
		remove( account, PASSWORD );
		remove( account, PASSPHRASE );
	}

} // namespace arterm::model
