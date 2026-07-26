#include "model/host_store.hpp"

#include "core/json.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/uuid.hpp"
#include "model/secret_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arterm::model
{
	namespace
	{

		namespace fs = std::filesystem;

		std::string auth_method_key( ssh::AuthMethod method ){
			switch( method ){
				case ssh::AuthMethod::AGENT:
					return "agent";
				case ssh::AuthMethod::PUBLIC_KEY:
					return "publickey";
				case ssh::AuthMethod::PASSWORD:
					return "password";
				case ssh::AuthMethod::KEYBOARD_INTERACTIVE:
					return "keyboard-interactive";
			}
			return "agent";
		}

		std::string transfer_backend_key( ssh::TransferBackend backend ){
			return backend == ssh::TransferBackend::SCP ? "scp" : "sftp";
		}

		ssh::TransferBackend transfer_backend_from_key( std::string const& key ){
			return key == "scp" ? ssh::TransferBackend::SCP : ssh::TransferBackend::SFTP;
		}

		ssh::AuthMethod auth_method_from_key( std::string const& key ){
			if( key == "publickey" )
				return ssh::AuthMethod::PUBLIC_KEY;
			if( key == "password" )
				return ssh::AuthMethod::PASSWORD;
			if( key == "keyboard-interactive" )
				return ssh::AuthMethod::KEYBOARD_INTERACTIVE;
			return ssh::AuthMethod::AGENT;
		}

		json::Value to_json( ssh::HostProfile const& profile ){
			json::Object object;
			object.emplace( "id", profile.id );
			object.emplace( "label", profile.label );
			object.emplace( "hostname", profile.hostname );
			object.emplace( "port", static_cast<int>( profile.port ) );
			object.emplace( "username", profile.username );
			object.emplace( "group", profile.group );
			object.emplace( "colorTag", profile.color_tag );
			object.emplace( "auth", auth_method_key( profile.preferred_auth ) );
			object.emplace( "privateKeyPath", profile.private_key_path );
			object.emplace( "useAgent", profile.use_agent );
			object.emplace( "startupDirectory", profile.startup_directory );
			object.emplace( "startupCommand", profile.startup_command );
			object.emplace( "openFileBrowser", profile.open_file_browser );
			object.emplace( "keepAliveSeconds", profile.keep_alive_seconds );
			object.emplace( "compression", profile.compression );
			object.emplace( "strictHostKeyChecking", profile.strict_host_key_checking );
			object.emplace( "transferBackend", transfer_backend_key( profile.transfer_backend ) );
			// Passwords and passphrases are intentionally absent: they belong to the
			// keychain, keyed by `id`.
			return object;
		}

		ssh::HostProfile from_json( json::Value const& object ){
			ssh::HostProfile profile;
			profile.id                       = object["id"].to_string();
			profile.label                    = object["label"].to_string();
			profile.hostname                 = object["hostname"].to_string();
			profile.port                     = static_cast<std::uint16_t>( object["port"].to_int( 22 ) );
			profile.username                 = object["username"].to_string();
			profile.group                    = object["group"].to_string();
			profile.color_tag                = object["colorTag"].to_string();
			profile.preferred_auth           = auth_method_from_key( object["auth"].to_string() );
			profile.private_key_path         = object["privateKeyPath"].to_string();
			profile.use_agent                = object["useAgent"].to_bool( true );
			profile.startup_directory        = object["startupDirectory"].to_string();
			profile.startup_command          = object["startupCommand"].to_string();
			profile.open_file_browser        = object["openFileBrowser"].to_bool( true );
			profile.keep_alive_seconds       = object["keepAliveSeconds"].to_int( 30 );
			profile.compression              = object["compression"].to_bool( false );
			profile.strict_host_key_checking = object["strictHostKeyChecking"].to_bool( true );
			profile.transfer_backend         = transfer_backend_from_key( object["transferBackend"].to_string() );
			return profile;
		}

		std::string lowercased( std::string_view text ){
			std::string out( text );
			for( char& c : out )
				c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
			return out;
		}

		std::string trimmed( std::string_view text ){
			auto const begin = text.find_first_not_of( " \t\r\n" );
			if( begin == std::string_view::npos )
				return {};
			auto const end = text.find_last_not_of( " \t\r\n" );
			return std::string( text.substr( begin, end - begin + 1 ) );
		}

	} // namespace

	HostStore::HostStore()
		: _file_path( default_file_path() )
	{}

	std::string HostStore::default_file_path(){
		return application_data_directory() + "/hosts.json";
	}

	int HostStore::index_of( std::string const& id ) const{
		for( std::size_t i = 0; i < _profiles.size(); ++i ){
			if( _profiles[i].id == id )
				return static_cast<int>( i );
		}
		return -1;
	}

	std::optional<ssh::HostProfile> HostStore::profile_by_id( std::string const& id ) const{
		int const index = index_of( id );
		if( index < 0 )
			return std::nullopt;
		return _profiles[static_cast<std::size_t>( index )];
	}

	std::vector<std::string> HostStore::groups() const{
		std::vector<std::string> result;
		for( ssh::HostProfile const& profile : _profiles ){
			std::string const group = profile.group.empty() ? "Hosts" : profile.group;
			if( std::ranges::find( result, group ) == result.end() )
				result.push_back( group );
		}
		std::ranges::sort(
			result, []( std::string const& a, std::string const& b ) { return lowercased( a ) < lowercased( b ); } );
		return result;
	}

	std::string HostStore::add( ssh::HostProfile profile ){
		if( profile.id.empty() )
			profile.id = generate_uuid();

		save_secrets( profile );
		profile.password.clear();
		profile.key_passphrase.clear();

		_profiles.push_back( profile );

		if( !save() )
			log_warning( "hosts", "cannot persist the host list" );

		profile_added( profile.id );
		changed();
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
		_profiles[static_cast<std::size_t>( index )] = stored;

		if( !save() )
			log_warning( "hosts", "cannot persist the host list" );

		profile_updated( profile.id );
		changed();
	}

	void HostStore::remove( std::string const& id ){
		int const index = index_of( id );
		if( index < 0 )
			return;

		_profiles.erase( _profiles.begin() + index );
		SecretStore::remove_all( id );

		if( !save() )
			log_warning( "hosts", "cannot persist the host list" );

		profile_removed( id );
		changed();
	}

	void HostStore::save_secrets( ssh::HostProfile const& profile ){
		if( !profile.password.empty() )
			static_cast<void>( SecretStore::store( profile.id, SecretStore::PASSWORD, profile.password ) );
		if( !profile.key_passphrase.empty() )
			static_cast<void>( SecretStore::store( profile.id, SecretStore::PASSPHRASE, profile.key_passphrase ) );
	}

	ssh::HostProfile HostStore::with_secrets( std::string const& id ) const{
		int const index = index_of( id );
		if( index < 0 )
			return {};

		ssh::HostProfile profile = _profiles[static_cast<std::size_t>( index )];

		if( auto const password = SecretStore::retrieve( id, SecretStore::PASSWORD ) )
			profile.password = *password;
		if( auto const passphrase = SecretStore::retrieve( id, SecretStore::PASSPHRASE ) )
			profile.key_passphrase = *passphrase;

		return profile;
	}

	bool HostStore::load(){
		std::error_code ignored;
		if( !fs::exists( _file_path, ignored ) )
			return true; // A fresh installation, not an error.

		std::ifstream file( _file_path, std::ios::binary );
		if( !file ){
			log_warning( "hosts", "cannot read {}", _file_path );
			return false;
		}

		std::ostringstream contents;
		contents << file.rdbuf();

		auto const document = json::parse( contents.view() );
		if( !document ){
			log_warning( "hosts", "cannot parse {}", _file_path );
			return false;
		}

		json::Array const& array = document->is_array() ? document->array() : ( *document )["hosts"].array();

		_profiles.clear();
		_profiles.reserve( array.size() );
		for( json::Value const& value : array ){
			if( !value.is_object() )
				continue;
			ssh::HostProfile profile = from_json( value );
			if( profile.id.empty() )
				profile.id = generate_uuid();
			if( !profile.hostname.empty() )
				_profiles.push_back( std::move( profile ) );
		}

		changed();
		return true;
	}

	bool HostStore::save() const{
		fs::path const  file( _file_path );
		std::error_code ignored;
		fs::create_directories( file.parent_path(), ignored );

		json::Array array;
		array.reserve( _profiles.size() );
		for( ssh::HostProfile const& profile : _profiles )
			array.push_back( to_json( profile ) );

		json::Object root;
		root.emplace( "version", 1 );
		root.emplace( "hosts", std::move( array ) );

		// Write to a sibling temporary and rename, so a crash mid-write cannot
		// leave the user without a host list.
		std::string const temporary = _file_path + ".tmp";
		{
			std::ofstream out( temporary, std::ios::binary | std::ios::trunc );
			if( !out ){
				log_warning( "hosts", "cannot open {}", temporary );
				return false;
			}
			out << json::dump( json::Value( std::move( root ) ) );
			if( !out.flush() ){
				log_warning( "hosts", "cannot write {}", temporary );
				return false;
			}
		}

		fs::rename( temporary, _file_path, ignored );
		if( ignored ){
			log_warning( "hosts", "cannot commit {}: {}", _file_path, ignored.message() );
			fs::remove( temporary, ignored );
			return false;
		}

		fs::permissions( _file_path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace,
						 ignored );
		return true;
	}

	int HostStore::import_from_ssh_config( std::string const& config_path ){
		std::string const path = config_path.empty() ? home_directory() + "/.ssh/config" : config_path;

		std::ifstream file( path );
		if( !file )
			return 0;

		int              imported = 0;
		ssh::HostProfile current;
		bool             in_host_block = false;

		auto const flush = [&]{
			if( !in_host_block || current.hostname.empty() )
				return;

			// Skip hosts already known by endpoint so a second import is a no-op.
			bool const duplicate = std::ranges::any_of( _profiles, [&current]( ssh::HostProfile const& existing ){
				return existing.hostname == current.hostname && existing.port == current.port &&
					   existing.username == current.username;
			} );
			if( duplicate ){
				current = {};
				return;
			}

			current.id    = generate_uuid();
			current.group = "Imported";
			_profiles.push_back( current );
			++imported;
			current = {};
		};

		std::string raw_line;
		while( std::getline( file, raw_line ) ){
			std::string line = trimmed( raw_line );
			if( line.empty() || line.starts_with( '#' ) )
				continue;

			// Keywords are case-insensitive and separated by whitespace or '='.
			std::ranges::replace( line, '=', ' ' );
			auto const        space   = line.find( ' ' );
			std::string const keyword = lowercased( line.substr( 0, space ) );
			std::string const value = space == std::string::npos ? std::string{} : trimmed( line.substr( space + 1 ) );
			if( value.empty() )
				continue;

			if( keyword == "host" ){
				flush();
				// Patterns cannot be connected to, so they are not imported.
				if( value.contains( '*' ) || value.contains( '?' ) ){
					in_host_block = false;
					continue;
				}
				in_host_block    = true;
				current          = {};
				current.label    = value.substr( 0, value.find( ' ' ) );
				current.hostname = current.label;
				if( char const* user = std::getenv( "USER" ); user != nullptr )
					current.username = user;
			}
			else if( !in_host_block ){
				continue;
			}
			else if( keyword == "hostname" ){
				current.hostname = value;
			}
			else if( keyword == "user" ){
				current.username = value;
			}
			else if( keyword == "port" ){
				current.port = static_cast<std::uint16_t>( std::strtoul( value.c_str(), nullptr, 10 ) );
			}
			else if( keyword == "identityfile" ){
				current.private_key_path = value;
				current.preferred_auth   = ssh::AuthMethod::PUBLIC_KEY;
			}
			else if( keyword == "compression" ){
				current.compression = lowercased( value ) == "yes";
			}
		}

		flush();

		if( imported > 0 ){
			static_cast<void>( save() );
			changed();
		}

		return imported;
	}

} // namespace arterm::model
