#include "ui/host_editor_dialog.hpp"

#include "model/secret_store.hpp"
#include "ui/icons.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace arterm::ui
{
	namespace
	{

		/// Order must match the entries added to `_auth_method`.
		constexpr ssh::AuthMethod AUTH_ORDER[] = {
			ssh::AuthMethod::AGENT,
			ssh::AuthMethod::PUBLIC_KEY,
			ssh::AuthMethod::PASSWORD,
		};

		int index_for_auth( ssh::AuthMethod method ){
			for( int i = 0; i < 3; ++i ){
				if( AUTH_ORDER[i] == method )
					return i;
			}
			// Keyboard-interactive shares the password page: both are answered with the
			// same secret, and the connection tries them in turn.
			return method == ssh::AuthMethod::KEYBOARD_INTERACTIVE ? 2 : 0;
		}

	} // namespace

	HostEditorDialog::HostEditorDialog( QWidget* parent )
		: QDialog( parent )
	{
		build_ui();
	}

	void HostEditorDialog::build_ui(){
		setWindowTitle( tr( "Host" ) );
		setModal( true );
		setMinimumWidth( 460 );

		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 20, 18, 20, 16 );
		root->setSpacing( 14 );

		auto* heading = new QLabel( tr( "Connection" ), this );
		heading->setProperty( "role", QStringLiteral( "heading" ) );
		root->addWidget( heading );

		auto* tabs = new QTabWidget( this );

		// -- General -----------------------------------------------------------
		auto* general        = new QWidget( tabs );
		auto* general_layout = new QFormLayout( general );
		general_layout->setContentsMargins( 4, 14, 4, 4 );
		general_layout->setSpacing( 10 );
		general_layout->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );

		_label = new QLineEdit( general );
		_label->setPlaceholderText( tr( "Optional display name" ) );

		_hostname = new QLineEdit( general );
		_hostname->setPlaceholderText( tr( "example.com or 10.0.0.5" ) );

		_port = new QSpinBox( general );
		_port->setRange( 1, 65535 );
		_port->setValue( 22 );

		_username = new QLineEdit( general );
		_username->setPlaceholderText( qEnvironmentVariable( "USER" ) );

		_group = new QComboBox( general );
		_group->setEditable( true );
		_group->addItems( { tr( "Hosts" ), tr( "Production" ), tr( "Staging" ), tr( "Personal" ) } );

		general_layout->addRow( tr( "Label" ), _label );
		general_layout->addRow( tr( "Host" ), _hostname );
		general_layout->addRow( tr( "Port" ), _port );
		general_layout->addRow( tr( "Username" ), _username );
		general_layout->addRow( tr( "Group" ), _group );

		// -- Authentication ----------------------------------------------------
		auto* auth        = new QWidget( tabs );
		auto* auth_layout = new QVBoxLayout( auth );
		auth_layout->setContentsMargins( 4, 14, 4, 4 );
		auth_layout->setSpacing( 10 );

		auto* method_row = new QFormLayout;
		_auth_method     = new QComboBox( auth );
		_auth_method->addItem( tr( "SSH agent" ) );
		_auth_method->addItem( tr( "Private key" ) );
		_auth_method->addItem( tr( "Password" ) );
		method_row->addRow( tr( "Method" ), _auth_method );
		auth_layout->addLayout( method_row );

		_auth_pages = new QStackedWidget( auth );

		// Agent page.
		auto* agent_page   = new QWidget( _auth_pages );
		auto* agent_layout = new QVBoxLayout( agent_page );
		agent_layout->setContentsMargins( 0, 4, 0, 0 );
		auto* agent_hint = new QLabel( tr( "Uses the keys loaded in ssh-agent (SSH_AUTH_SOCK). Add one with\n"
										   "ssh-add ~/.ssh/id_ed25519." ),
									   agent_page );
		agent_hint->setProperty( "role", QStringLiteral( "caption" ) );
		agent_hint->setWordWrap( true );
		agent_layout->addWidget( agent_hint );
		agent_layout->addStretch( 1 );
		_auth_pages->addWidget( agent_page );

		// Key page.
		auto* key_page   = new QWidget( _auth_pages );
		auto* key_layout = new QFormLayout( key_page );
		key_layout->setContentsMargins( 0, 4, 0, 0 );

		auto* key_row        = new QWidget( key_page );
		auto* key_row_layout = new QHBoxLayout( key_row );
		key_row_layout->setContentsMargins( 0, 0, 0, 0 );
		key_row_layout->setSpacing( 6 );

		_key_path = new QLineEdit( key_row );
		_key_path->setPlaceholderText( QDir::homePath() + QLatin1String( "/.ssh/id_ed25519" ) );

		auto* browse_button = new QToolButton( key_row );
		browse_button->setIcon( icon( QStringLiteral( "folder" ) ) );
		browse_button->setToolTip( tr( "Choose a key file" ) );

		key_row_layout->addWidget( _key_path, 1 );
		key_row_layout->addWidget( browse_button );

		_key_passphrase = new QLineEdit( key_page );
		_key_passphrase->setEchoMode( QLineEdit::Password );
		_key_passphrase->setPlaceholderText( tr( "Leave empty to be asked when needed" ) );

		key_layout->addRow( tr( "Private key" ), key_row );
		key_layout->addRow( tr( "Passphrase" ), _key_passphrase );
		_auth_pages->addWidget( key_page );

		// Password page.
		auto* password_page   = new QWidget( _auth_pages );
		auto* password_layout = new QFormLayout( password_page );
		password_layout->setContentsMargins( 0, 4, 0, 0 );

		_password = new QLineEdit( password_page );
		_password->setEchoMode( QLineEdit::Password );
		_password->setPlaceholderText( tr( "Leave empty to be asked on every connection" ) );

		password_layout->addRow( tr( "Password" ), _password );
		_auth_pages->addWidget( password_page );

		auth_layout->addWidget( _auth_pages, 1 );

		_save_password =
			new QCheckBox( tr( "Save secrets in the %1" ).arg( model::SecretStore::backend_name() ), auth );
		_save_password->setChecked( model::SecretStore::is_available() );
		_save_password->setEnabled( model::SecretStore::is_available() );
		auth_layout->addWidget( _save_password );

		// -- Advanced ----------------------------------------------------------
		auto* advanced        = new QWidget( tabs );
		auto* advanced_layout = new QFormLayout( advanced );
		advanced_layout->setContentsMargins( 4, 14, 4, 4 );
		advanced_layout->setSpacing( 10 );
		advanced_layout->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );

		_startup_directory = new QLineEdit( advanced );
		_startup_directory->setPlaceholderText( tr( "Login directory" ) );

		_startup_command = new QLineEdit( advanced );
		_startup_command->setPlaceholderText( tr( "e.g. tmux attach || tmux new" ) );

		_open_file_browser = new QCheckBox( tr( "Open the file browser with this session" ), advanced );
		_open_file_browser->setChecked( true );

		_compression = new QCheckBox( tr( "Enable compression" ), advanced );

		_strict_host_key = new QCheckBox( tr( "Verify the host key against known_hosts" ), advanced );
		_strict_host_key->setChecked( true );

		_transfer_backend = new QComboBox( advanced );
		_transfer_backend->addItem( tr( "SFTP (recommended)" ), static_cast<int>( ssh::TransferBackend::SFTP ) );
		_transfer_backend->addItem( tr( "SCP" ), static_cast<int>( ssh::TransferBackend::SCP ) );
		_transfer_backend->setToolTip(
			tr( "Directory browsing always uses SFTP, because SCP cannot list a directory.\n"
				"This setting only chooses how file contents are copied. Pick SCP for hosts\n"
				"that disable the SFTP subsystem, or on very high-latency links." ) );

		_keep_alive = new QSpinBox( advanced );
		_keep_alive->setRange( 0, 600 );
		_keep_alive->setValue( 30 );
		_keep_alive->setSuffix( tr( " s" ) );
		_keep_alive->setSpecialValueText( tr( "off" ) );

		advanced_layout->addRow( tr( "Remote directory" ), _startup_directory );
		advanced_layout->addRow( tr( "Startup command" ), _startup_command );
		advanced_layout->addRow( QString(), _open_file_browser );
		advanced_layout->addRow( QString(), _compression );
		advanced_layout->addRow( QString(), _strict_host_key );
		advanced_layout->addRow( tr( "Keep-alive" ), _keep_alive );
		advanced_layout->addRow( tr( "Transfers via" ), _transfer_backend );

		tabs->addTab( general, tr( "General" ) );
		tabs->addTab( auth, tr( "Authentication" ) );
		tabs->addTab( advanced, tr( "Advanced" ) );

		root->addWidget( tabs, 1 );

		// -- Buttons -----------------------------------------------------------
		auto* buttons = new QDialogButtonBox( QDialogButtonBox::Save | QDialogButtonBox::Cancel, this );
		buttons->button( QDialogButtonBox::Save )->setProperty( "accent", true );
		root->addWidget( buttons );

		connect( browse_button, &QToolButton::clicked, this, &HostEditorDialog::browse_for_key );
		connect( _auth_method, &QComboBox::currentIndexChanged, this, &HostEditorDialog::update_auth_page );
		connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
		connect( buttons, &QDialogButtonBox::accepted, this, [this]{
			if( validate() )
				accept();
		} );

		update_auth_page();
	}

	void HostEditorDialog::update_auth_page(){
		_auth_pages->setCurrentIndex( _auth_method->currentIndex() );
	}

	void HostEditorDialog::browse_for_key(){
		QString const path = QFileDialog::getOpenFileName(
			this, tr( "Select a private key" ), QDir::homePath() + QLatin1String( "/.ssh" ), tr( "All files (*)" ) );
		if( !path.isEmpty() )
			_key_path->setText( path );
	}

	bool HostEditorDialog::validate(){
		if( _hostname->text().trimmed().isEmpty() ){
			QMessageBox::warning( this, tr( "Host" ), tr( "Enter a hostname or an IP address." ) );
			_hostname->setFocus();
			return false;
		}

		if( _username->text().trimmed().isEmpty() && qEnvironmentVariable( "USER" ).isEmpty() ){
			QMessageBox::warning( this, tr( "Host" ), tr( "Enter a username." ) );
			_username->setFocus();
			return false;
		}

		if( AUTH_ORDER[_auth_method->currentIndex()] == ssh::AuthMethod::PUBLIC_KEY &&
			_key_path->text().trimmed().isEmpty() ){
			QMessageBox::warning( this, tr( "Host" ), tr( "Choose a private key file." ) );
			_key_path->setFocus();
			return false;
		}

		return true;
	}

	void HostEditorDialog::set_profile( ssh::HostProfile const& profile ){
		_profile = profile;

		setWindowTitle( profile.id.isEmpty() ? tr( "New Host" ) : tr( "Edit Host" ) );

		_label->setText( profile.label );
		_hostname->setText( profile.hostname );
		_port->setValue( profile.port );
		_username->setText( profile.username );
		_group->setCurrentText( profile.group.isEmpty() ? tr( "Hosts" ) : profile.group );

		_auth_method->setCurrentIndex( index_for_auth( profile.preferred_auth ) );
		_key_path->setText( profile.private_key_path );
		_key_passphrase->setText( profile.key_passphrase );
		_password->setText( profile.password );

		_startup_directory->setText( profile.startup_directory );
		_startup_command->setText( profile.startup_command );
		_open_file_browser->setChecked( profile.open_file_browser );
		_compression->setChecked( profile.compression );
		_strict_host_key->setChecked( profile.strict_host_key_checking );
		_keep_alive->setValue( profile.keep_alive_seconds );
		_transfer_backend->setCurrentIndex(
			_transfer_backend->findData( static_cast<int>( profile.transfer_backend ) ) );

		update_auth_page();
	}

	ssh::HostProfile HostEditorDialog::profile() const{
		ssh::HostProfile profile = _profile;

		profile.label    = _label->text().trimmed();
		profile.hostname = _hostname->text().trimmed();
		profile.port     = static_cast<quint16>( _port->value() );
		profile.username = _username->text().trimmed();
		if( profile.username.isEmpty() )
			profile.username = qEnvironmentVariable( "USER" );
		profile.group = _group->currentText().trimmed();

		profile.preferred_auth   = AUTH_ORDER[_auth_method->currentIndex()];
		profile.private_key_path = _key_path->text().trimmed();
		profile.use_agent        = ( profile.preferred_auth == ssh::AuthMethod::AGENT );

		// Secrets are only carried out of the dialog when the user asked for them
		// to be saved; otherwise the connection prompts each time.
		if( _save_password->isChecked() ){
			profile.password       = _password->text();
			profile.key_passphrase = _key_passphrase->text();
		}
		else{
			profile.password.clear();
			profile.key_passphrase.clear();
		}

		profile.startup_directory        = _startup_directory->text().trimmed();
		profile.startup_command          = _startup_command->text().trimmed();
		profile.open_file_browser        = _open_file_browser->isChecked();
		profile.compression              = _compression->isChecked();
		profile.strict_host_key_checking = _strict_host_key->isChecked();
		profile.keep_alive_seconds       = _keep_alive->value();
		profile.transfer_backend = static_cast<ssh::TransferBackend>( _transfer_backend->currentData().toInt() );

		return profile;
	}

} // namespace arterm::ui
