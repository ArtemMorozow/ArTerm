#include "model/host_store.hpp"

#include "core/uuid.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace arterm;
namespace fs = std::filesystem;

namespace
{

	/// A throwaway directory, deleted on scope exit. Keeps the tests away from
	/// the developer's real host list.
	class TemporaryDir
	{
	public:
		TemporaryDir()
			: _path( fs::temp_directory_path() / ( "arterm-test-" + generate_uuid() ) )
		{
			fs::create_directories( _path );
		}

		~TemporaryDir(){
			std::error_code ignored;
			fs::remove_all( _path, ignored );
		}

		[[nodiscard]] std::string file( std::string const& name ) const { return ( _path / name ).string(); }

	private:
		fs::path _path;
	};

	void write_file( std::string const& path, std::string_view contents ){
		std::ofstream out( path, std::ios::binary );
		out << contents;
	}

} // namespace

TEST_CASE( "add assigns an id", "[hoststore]" ){
	TemporaryDir     directory;
	model::HostStore store;
	store.set_file_path( directory.file( "hosts.json" ) );

	ssh::HostProfile profile;
	profile.hostname = "example.com";
	profile.username = "deploy";

	std::string const id = store.add( profile );

	CHECK( !id.empty() );
	CHECK( store.count() == 1 );
	REQUIRE( store.profile_by_id( id ).has_value() );
	CHECK( store.profile_by_id( id )->hostname == "example.com" );
}

TEST_CASE( "update replaces in place", "[hoststore]" ){
	TemporaryDir     directory;
	model::HostStore store;
	store.set_file_path( directory.file( "hosts.json" ) );

	ssh::HostProfile profile;
	profile.hostname     = "old.example.com";
	std::string const id = store.add( profile );

	ssh::HostProfile edited = *store.profile_by_id( id );
	edited.hostname         = "new.example.com";
	edited.port             = 2222;
	store.update( edited );

	CHECK( store.count() == 1 );
	CHECK( store.profile_by_id( id )->hostname == "new.example.com" );
	CHECK( store.profile_by_id( id )->port == 2222 );
}

TEST_CASE( "remove drops the profile", "[hoststore]" ){
	TemporaryDir     directory;
	model::HostStore store;
	store.set_file_path( directory.file( "hosts.json" ) );

	ssh::HostProfile profile;
	profile.hostname     = "gone.example.com";
	std::string const id = store.add( profile );

	store.remove( id );

	CHECK( store.count() == 0 );
	CHECK( !store.profile_by_id( id ).has_value() );
}

TEST_CASE( "round trips through disk", "[hoststore]" ){
	TemporaryDir directory;
	std::string  id;

	{
		model::HostStore store;
		store.set_file_path( directory.file( "hosts.json" ) );

		ssh::HostProfile profile;
		profile.label            = "Build server";
		profile.hostname         = "build.example.com";
		profile.username         = "ci";
		profile.port             = 2202;
		profile.group            = "Production";
		profile.preferred_auth   = ssh::AuthMethod::PUBLIC_KEY;
		profile.private_key_path = "/home/ci/.ssh/id_ed25519";
		profile.compression      = true;
		// A secret set here must not reach the JSON file.
		profile.password = "hunter2";

		id = store.add( profile );
		REQUIRE( store.save() );
	}

	model::HostStore reloaded;
	reloaded.set_file_path( directory.file( "hosts.json" ) );
	REQUIRE( reloaded.load() );

	auto const profile = reloaded.profile_by_id( id );
	REQUIRE( profile.has_value() );
	CHECK( profile->label == "Build server" );
	CHECK( profile->port == 2202 );
	CHECK( profile->preferred_auth == ssh::AuthMethod::PUBLIC_KEY );
	CHECK( profile->compression );
	CHECK( profile->password.empty() );

	// The password went into the real keychain; leave nothing behind.
	reloaded.remove( id );
}

TEST_CASE( "the JSON file itself never contains a secret", "[hoststore]" ){
	TemporaryDir directory;
	std::string  id;

	{
		model::HostStore store;
		store.set_file_path( directory.file( "hosts.json" ) );

		ssh::HostProfile profile;
		profile.hostname = "secret.example.com";
		profile.password = "hunter2";
		id               = store.add( profile );
		REQUIRE( store.save() );
	}

	std::ifstream     file( directory.file( "hosts.json" ), std::ios::binary );
	std::string const contents( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
	CHECK( contents.find( "hunter2" ) == std::string::npos );

	model::HostStore cleanup;
	cleanup.set_file_path( directory.file( "hosts.json" ) );
	REQUIRE( cleanup.load() );
	cleanup.remove( id );
}

TEST_CASE( "imports from ssh config", "[hoststore]" ){
	TemporaryDir directory;

	std::string const config_path = directory.file( "config" );
	write_file( config_path, "# a comment\n"
							 "Host build\n"
							 "    HostName build.internal\n"
							 "    User ci\n"
							 "    Port 2222\n"
							 "    IdentityFile ~/.ssh/build_key\n"
							 "\n"
							 "Host db\n"
							 "    HostName db.internal\n"
							 "    User postgres\n" );

	model::HostStore store;
	store.set_file_path( directory.file( "hosts.json" ) );
	int const imported = store.import_from_ssh_config( config_path );

	CHECK( imported == 2 );

	auto const& profiles = store.profiles();
	auto const  build =
		std::ranges::find_if( profiles, []( ssh::HostProfile const& p ) { return p.hostname == "build.internal"; } );
	REQUIRE( build != profiles.end() );
	CHECK( build->username == "ci" );
	CHECK( build->port == 2222 );
	CHECK( build->preferred_auth == ssh::AuthMethod::PUBLIC_KEY );
}

TEST_CASE( "import skips patterns and duplicates", "[hoststore]" ){
	TemporaryDir directory;

	std::string const config_path = directory.file( "config" );
	write_file( config_path, "Host *\n"
							 "    ServerAliveInterval 60\n"
							 "\n"
							 "Host web\n"
							 "    HostName web.internal\n"
							 "    User www\n" );

	model::HostStore store;
	store.set_file_path( directory.file( "hosts.json" ) );

	CHECK( store.import_from_ssh_config( config_path ) == 1 );
	// A second import of the same file must not duplicate anything.
	CHECK( store.import_from_ssh_config( config_path ) == 0 );
}

TEST_CASE( "the transfer backend survives a reload", "[hoststore]" ){
	TemporaryDir directory;
	std::string  id;

	{
		model::HostStore store;
		store.set_file_path( directory.file( "hosts.json" ) );

		ssh::HostProfile profile;
		profile.hostname         = "scp-only.example.com";
		profile.transfer_backend = ssh::TransferBackend::SCP;

		id = store.add( profile );
		REQUIRE( store.save() );
	}

	model::HostStore reloaded;
	reloaded.set_file_path( directory.file( "hosts.json" ) );
	REQUIRE( reloaded.load() );
	CHECK( reloaded.profile_by_id( id )->transfer_backend == ssh::TransferBackend::SCP );

	// An older profile file has no such key and must default to SFTP rather
	// than to whatever zero happens to mean.
	ssh::HostProfile fresh;
	CHECK( fresh.transfer_backend == ssh::TransferBackend::SFTP );
}

TEST_CASE( "auth order prefers the configured method", "[hoststore]" ){
	ssh::HostProfile profile;
	profile.preferred_auth = ssh::AuthMethod::PASSWORD;
	profile.password       = "secret";
	profile.use_agent      = false;

	auto const order = profile.auth_order();

	REQUIRE( !order.empty() );
	CHECK( order.front() == ssh::AuthMethod::PASSWORD );
	// Without an agent or a key those methods must not be attempted at all.
	CHECK( std::ranges::find( order, ssh::AuthMethod::AGENT ) == order.end() );
	CHECK( std::ranges::find( order, ssh::AuthMethod::PUBLIC_KEY ) == order.end() );
}

TEST_CASE( "permission string matches ls format", "[hoststore]" ){
	ssh::RemoteFileEntry entry;
	entry.is_directory = true;
	entry.permissions  = 0755;
	CHECK( entry.permission_string() == "drwxr-xr-x" );

	entry.is_directory = false;
	entry.permissions  = 0644;
	CHECK( entry.permission_string() == "-rw-r--r--" );

	entry.is_symlink  = true;
	entry.permissions = 0777;
	CHECK( entry.permission_string() == "lrwxrwxrwx" );
}
