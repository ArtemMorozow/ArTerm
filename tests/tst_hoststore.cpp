#include "files/FileListModel.hpp"
#include "model/HostStore.hpp"

#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace arterm;

class TestHostStore : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void addAssignsAnId();
    void updateReplacesInPlace();
    void removeDropsTheProfile();
    void roundTripsThroughDisk();
    void importsFromSshConfig();
    void importSkipsPatternsAndDuplicates();

    void transferBackendSurvivesAReload();
    void authOrderPrefersTheConfiguredMethod();
    void permissionStringMatchesLsFormat();
    void fileSizeFormatting();

private:
    QTemporaryDir m_dataDir;
};

void TestHostStore::initTestCase()
{
    QVERIFY(m_dataDir.isValid());
    // Keep the tests away from the developer's real host list.
    QStandardPaths::setTestModeEnabled(true);
}

void TestHostStore::addAssignsAnId()
{
    model::HostStore store;

    ssh::HostProfile profile;
    profile.hostname = QStringLiteral("example.com");
    profile.username = QStringLiteral("deploy");

    const QString id = store.add(profile);

    QVERIFY(!id.isEmpty());
    QCOMPARE(store.count(), 1);
    QVERIFY(store.profileById(id).has_value());
    QCOMPARE(store.profileById(id)->hostname, QStringLiteral("example.com"));
}

void TestHostStore::updateReplacesInPlace()
{
    model::HostStore store;

    ssh::HostProfile profile;
    profile.hostname = QStringLiteral("old.example.com");
    const QString id = store.add(profile);

    ssh::HostProfile edited = *store.profileById(id);
    edited.hostname = QStringLiteral("new.example.com");
    edited.port = 2222;
    store.update(edited);

    QCOMPARE(store.count(), 1);
    QCOMPARE(store.profileById(id)->hostname, QStringLiteral("new.example.com"));
    QCOMPARE(store.profileById(id)->port, quint16(2222));
}

void TestHostStore::removeDropsTheProfile()
{
    model::HostStore store;

    ssh::HostProfile profile;
    profile.hostname = QStringLiteral("gone.example.com");
    const QString id = store.add(profile);

    store.remove(id);

    QCOMPARE(store.count(), 0);
    QVERIFY(!store.profileById(id).has_value());
}

void TestHostStore::roundTripsThroughDisk()
{
    QString id;

    {
        model::HostStore store;

        ssh::HostProfile profile;
        profile.label = QStringLiteral("Build server");
        profile.hostname = QStringLiteral("build.example.com");
        profile.username = QStringLiteral("ci");
        profile.port = 2202;
        profile.group = QStringLiteral("Production");
        profile.preferredAuth = ssh::AuthMethod::PublicKey;
        profile.privateKeyPath = QStringLiteral("/home/ci/.ssh/id_ed25519");
        profile.compression = true;
        // A secret set here must not reach the JSON file.
        profile.password = QStringLiteral("hunter2");

        id = store.add(profile);
        QVERIFY(store.save());
    }

    model::HostStore reloaded;
    QVERIFY(reloaded.load());

    const auto profile = reloaded.profileById(id);
    QVERIFY(profile.has_value());
    QCOMPARE(profile->label, QStringLiteral("Build server"));
    QCOMPARE(profile->port, quint16(2202));
    QCOMPARE(profile->preferredAuth, ssh::AuthMethod::PublicKey);
    QVERIFY(profile->compression);
    QVERIFY(profile->password.isEmpty());
}

void TestHostStore::importsFromSshConfig()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString configPath = directory.filePath(QStringLiteral("config"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write(
        "# a comment\n"
        "Host build\n"
        "    HostName build.internal\n"
        "    User ci\n"
        "    Port 2222\n"
        "    IdentityFile ~/.ssh/build_key\n"
        "\n"
        "Host db\n"
        "    HostName db.internal\n"
        "    User postgres\n");
    config.close();

    model::HostStore store;
    const int imported = store.importFromSshConfig(configPath);

    QCOMPARE(imported, 2);

    const auto profiles = store.profiles();
    const auto build = std::find_if(profiles.begin(), profiles.end(),
                                    [](const ssh::HostProfile &p) {
                                        return p.hostname == QLatin1String("build.internal");
                                    });
    QVERIFY(build != profiles.end());
    QCOMPARE(build->username, QStringLiteral("ci"));
    QCOMPARE(build->port, quint16(2222));
    QCOMPARE(build->preferredAuth, ssh::AuthMethod::PublicKey);
}

void TestHostStore::importSkipsPatternsAndDuplicates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString configPath = directory.filePath(QStringLiteral("config"));
    QFile config(configPath);
    QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Text));
    config.write(
        "Host *\n"
        "    ServerAliveInterval 60\n"
        "\n"
        "Host web\n"
        "    HostName web.internal\n"
        "    User www\n");
    config.close();

    model::HostStore store;

    QCOMPARE(store.importFromSshConfig(configPath), 1);
    // A second import of the same file must not duplicate anything.
    QCOMPARE(store.importFromSshConfig(configPath), 0);
}

void TestHostStore::transferBackendSurvivesAReload()
{
    QString id;

    {
        model::HostStore store;

        ssh::HostProfile profile;
        profile.hostname = QStringLiteral("scp-only.example.com");
        profile.transferBackend = ssh::TransferBackend::Scp;

        id = store.add(profile);
        QVERIFY(store.save());
    }

    model::HostStore reloaded;
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.profileById(id)->transferBackend, ssh::TransferBackend::Scp);

    // An older profile file has no such key and must default to SFTP rather
    // than to whatever zero happens to mean.
    ssh::HostProfile fresh;
    QCOMPARE(fresh.transferBackend, ssh::TransferBackend::Sftp);
}

void TestHostStore::authOrderPrefersTheConfiguredMethod()
{
    ssh::HostProfile profile;
    profile.preferredAuth = ssh::AuthMethod::Password;
    profile.password = QStringLiteral("secret");
    profile.useAgent = false;

    const auto order = profile.authOrder();

    QVERIFY(!order.isEmpty());
    QCOMPARE(order.first(), ssh::AuthMethod::Password);
    // Without an agent or a key those methods must not be attempted at all.
    QVERIFY(!order.contains(ssh::AuthMethod::Agent));
    QVERIFY(!order.contains(ssh::AuthMethod::PublicKey));
}

void TestHostStore::permissionStringMatchesLsFormat()
{
    ssh::RemoteFileEntry entry;
    entry.isDirectory = true;
    entry.permissions = 0755;
    QCOMPARE(entry.permissionString(), QStringLiteral("drwxr-xr-x"));

    entry.isDirectory = false;
    entry.permissions = 0644;
    QCOMPARE(entry.permissionString(), QStringLiteral("-rw-r--r--"));

    entry.isSymlink = true;
    entry.permissions = 0777;
    QCOMPARE(entry.permissionString(), QStringLiteral("lrwxrwxrwx"));
}

void TestHostStore::fileSizeFormatting()
{
    QVERIFY(files::formatFileSize(512).contains(QStringLiteral("512")));
    QVERIFY(files::formatFileSize(2048).contains(QStringLiteral("KB")));
    QVERIFY(files::formatFileSize(5ull * 1024 * 1024).contains(QStringLiteral("MB")));
    QVERIFY(files::formatFileSize(3ull * 1024 * 1024 * 1024).contains(QStringLiteral("GB")));
}

QTEST_MAIN(TestHostStore)

#include "tst_hoststore.moc"
