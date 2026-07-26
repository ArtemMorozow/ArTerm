#include "files/file_list_model.hpp"
#include "model/host_store.hpp"

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace arterm;

class TestHostStore : public QObject
{
	Q_OBJECT

private Q_SLOTS:
	void initTestCase();

	void add_assigns_an_id();
	void update_replaces_in_place();
	void remove_drops_the_profile();
	void round_trips_through_disk();
	void imports_from_ssh_config();
	void import_skips_patterns_and_duplicates();

	void transfer_backend_survives_a_reload();
	void auth_order_prefers_the_configured_method();
	void permission_string_matches_ls_format();
	void file_size_formatting();

private:
	QTemporaryDir _data_dir;
};

void TestHostStore::initTestCase(){
	QVERIFY( _data_dir.isValid() );
	// Keep the tests away from the developer's real host list.
	QStandardPaths::set_test_mode_enabled( true );
}

void TestHostStore::add_assigns_an_id(){
	model::HostStore store;

	ssh::HostProfile profile;
	profile.hostname = QStringLiteral( "example.com" );
	profile.username = QStringLiteral( "deploy" );

	QString const id = store.add( profile );

	QVERIFY( !id.isEmpty() );
	QCOMPARE( store.count(), 1 );
	QVERIFY( store.profile_by_id( id ).has_value() );
	QCOMPARE( store.profile_by_id( id )->hostname, QStringLiteral( "example.com" ) );
}

void TestHostStore::update_replaces_in_place(){
	model::HostStore store;

	ssh::HostProfile profile;
	profile.hostname = QStringLiteral( "old.example.com" );
	QString const id = store.add( profile );

	ssh::HostProfile edited = *store.profile_by_id( id );
	edited.hostname         = QStringLiteral( "new.example.com" );
	edited.port             = 2222;
	store.update( edited );

	QCOMPARE( store.count(), 1 );
	QCOMPARE( store.profile_by_id( id )->hostname, QStringLiteral( "new.example.com" ) );
	QCOMPARE( store.profile_by_id( id )->port, quint16( 2222 ) );
}

void TestHostStore::remove_drops_the_profile(){
	model::HostStore store;

	ssh::HostProfile profile;
	profile.hostname = QStringLiteral( "gone.example.com" );
	QString const id = store.add( profile );

	store.remove( id );

	QCOMPARE( store.count(), 0 );
	QVERIFY( !store.profile_by_id( id ).has_value() );
}

void TestHostStore::round_trips_through_disk(){
	QString id;

	{
		model::HostStore store;

		ssh::HostProfile profile;
		profile.label            = QStringLiteral( "Build server" );
		profile.hostname         = QStringLiteral( "build.example.com" );
		profile.username         = QStringLiteral( "ci" );
		profile.port             = 2202;
		profile.group            = QStringLiteral( "Production" );
		profile.preferred_auth   = ssh::AuthMethod::PUBLIC_KEY;
		profile.private_key_path = QStringLiteral( "/home/ci/.ssh/id_ed25519" );
		profile.compression      = true;
		// A secret set here must not reach the JSON file.
		profile.password = QStringLiteral( "hunter2" );

		id = store.add( profile );
		QVERIFY( store.save() );
	}

	model::HostStore reloaded;
	QVERIFY( reloaded.load() );

	auto const profile = reloaded.profile_by_id( id );
	QVERIFY( profile.has_value() );
	QCOMPARE( profile->label, QStringLiteral( "Build server" ) );
	QCOMPARE( profile->port, quint16( 2202 ) );
	QCOMPARE( profile->preferred_auth, ssh::AuthMethod::PUBLIC_KEY );
	QVERIFY( profile->compression );
	QVERIFY( profile->password.isEmpty() );
}

void TestHostStore::imports_from_ssh_config(){
	QTemporaryDir directory;
	QVERIFY( directory.isValid() );

	QString const config_path = directory.filePath( QStringLiteral( "config" ) );
	QFile         config( config_path );
	QVERIFY( config.open( QIODevice::WriteOnly | QIODevice::Text ) );
	config.write( "# a comment\n"
				  "Host build\n"
				  "    HostName build.internal\n"
				  "    User ci\n"
				  "    Port 2222\n"
				  "    IdentityFile ~/.ssh/build_key\n"
				  "\n"
				  "Host db\n"
				  "    HostName db.internal\n"
				  "    User postgres\n" );
	config.close();

	model::HostStore store;
	int const        imported = store.import_from_ssh_config( config_path );

	QCOMPARE( imported, 2 );

	auto const profiles = store.profiles();
	auto const build    = std::find_if( profiles.begin(), profiles.end(), []( ssh::HostProfile const& p ){
        return p.hostname == QLatin1String( "build.internal" );
    } );
	QVERIFY( build != profiles.end() );
	QCOMPARE( build->username, QStringLiteral( "ci" ) );
	QCOMPARE( build->port, quint16( 2222 ) );
	QCOMPARE( build->preferred_auth, ssh::AuthMethod::PUBLIC_KEY );
}

void TestHostStore::import_skips_patterns_and_duplicates(){
	QTemporaryDir directory;
	QVERIFY( directory.isValid() );

	QString const config_path = directory.filePath( QStringLiteral( "config" ) );
	QFile         config( config_path );
	QVERIFY( config.open( QIODevice::WriteOnly | QIODevice::Text ) );
	config.write( "Host *\n"
				  "    ServerAliveInterval 60\n"
				  "\n"
				  "Host web\n"
				  "    HostName web.internal\n"
				  "    User www\n" );
	config.close();

	model::HostStore store;

	QCOMPARE( store.import_from_ssh_config( config_path ), 1 );
	// A second import of the same file must not duplicate anything.
	QCOMPARE( store.import_from_ssh_config( config_path ), 0 );
}

void TestHostStore::transfer_backend_survives_a_reload(){
	QString id;

	{
		model::HostStore store;

		ssh::HostProfile profile;
		profile.hostname         = QStringLiteral( "scp-only.example.com" );
		profile.transfer_backend = ssh::TransferBackend::SCP;

		id = store.add( profile );
		QVERIFY( store.save() );
	}

	model::HostStore reloaded;
	QVERIFY( reloaded.load() );
	QCOMPARE( reloaded.profile_by_id( id )->transfer_backend, ssh::TransferBackend::SCP );

	// An older profile file has no such key and must default to SFTP rather
	// than to whatever zero happens to mean.
	ssh::HostProfile fresh;
	QCOMPARE( fresh.transfer_backend, ssh::TransferBackend::SFTP );
}

void TestHostStore::auth_order_prefers_the_configured_method(){
	ssh::HostProfile profile;
	profile.preferred_auth = ssh::AuthMethod::PASSWORD;
	profile.password       = QStringLiteral( "secret" );
	profile.use_agent      = false;

	auto const order = profile.auth_order();

	QVERIFY( !order.isEmpty() );
	QCOMPARE( order.first(), ssh::AuthMethod::PASSWORD );
	// Without an agent or a key those methods must not be attempted at all.
	QVERIFY( !order.contains( ssh::AuthMethod::AGENT ) );
	QVERIFY( !order.contains( ssh::AuthMethod::PUBLIC_KEY ) );
}

void TestHostStore::permission_string_matches_ls_format(){
	ssh::RemoteFileEntry entry;
	entry.is_directory = true;
	entry.permissions  = 0755;
	QCOMPARE( entry.permission_string(), QStringLiteral( "drwxr-xr-x" ) );

	entry.is_directory = false;
	entry.permissions  = 0644;
	QCOMPARE( entry.permission_string(), QStringLiteral( "-rw-r--r--" ) );

	entry.is_symlink  = true;
	entry.permissions = 0777;
	QCOMPARE( entry.permission_string(), QStringLiteral( "lrwxrwxrwx" ) );
}

void TestHostStore::file_size_formatting(){
	QVERIFY( files::format_file_size( 512 ).contains( QStringLiteral( "512" ) ) );
	QVERIFY( files::format_file_size( 2048 ).contains( QStringLiteral( "KB" ) ) );
	QVERIFY( files::format_file_size( 5ull * 1024 * 1024 ).contains( QStringLiteral( "MB" ) ) );
	QVERIFY( files::format_file_size( 3ull * 1024 * 1024 * 1024 ).contains( QStringLiteral( "GB" ) ) );
}

QTEST_MAIN( TestHostStore )

#include "tst_hoststore.moc"
