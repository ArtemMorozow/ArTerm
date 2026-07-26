#include "ui/main_window.hpp"

#include "files/file_browser.hpp"
#include "model/host_store.hpp"
#include "ssh/session_interaction.hpp"
#include "terminal/terminal_widget.hpp"
#include "ui/host_editor_dialog.hpp"
#include "ui/host_key_dialog.hpp"
#include "ui/host_sidebar.hpp"
#include "ui/icons.hpp"
#include "ui/session_tab.hpp"
#include "ui/theme.hpp"

#include <QCloseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace arterm::ui
{

	MainWindow::MainWindow( QWidget* parent )
		: QMainWindow( parent )
		, _store( new model::HostStore( this ) )
		, _interaction( std::make_shared<ssh::SessionInteraction>() )
	{
		static_cast<void>( _store->load() );

		build_ui();
		build_menus();
		build_tool_bar();
		wire_interaction();
		restore_window_state();
	}

	MainWindow::~MainWindow() = default;

	void MainWindow::build_ui(){
		setWindowTitle( QStringLiteral( "ArTerm" ) );
		setMinimumSize( 940, 620 );

		_sidebar = new HostSidebar( _store, this );

		_tabs = new QTabWidget( this );
		_tabs->setObjectName( QStringLiteral( "sessionTabs" ) );
		_tabs->setTabsClosable( true );
		_tabs->setMovable( true );
		_tabs->setDocumentMode( true );

		// Shown instead of the tab widget when nothing is connected.
		_empty_state = new QLabel( this );
		_empty_state->setAlignment( Qt::AlignCenter );
		_empty_state->setWordWrap( true );
		_empty_state->setText( tr( "<div style='line-height:1.7'>"
								   "<span style='font-size:19px;font-weight:600'>No open sessions</span><br>"
								   "Pick a host on the left, or press ⌘N to add one."
								   "</div>" ) );

		auto* stack = new QStackedWidget( this );
		stack->addWidget( _empty_state );
		stack->addWidget( _tabs );

		auto const update_stack = [this, stack] { stack->setCurrentIndex( _tabs->count() > 0 ? 1 : 0 ); };
		connect( _tabs, &QTabWidget::currentChanged, this, update_stack );
		update_stack();

		auto* splitter = new QSplitter( Qt::Horizontal, this );
		splitter->addWidget( _sidebar );
		splitter->addWidget( stack );
		splitter->setStretchFactor( 0, 0 );
		splitter->setStretchFactor( 1, 1 );
		splitter->setCollapsible( 0, true );
		splitter->setCollapsible( 1, false );
		splitter->setHandleWidth( 1 );
		splitter->setObjectName( QStringLiteral( "mainSplitter" ) );

		setCentralWidget( splitter );

		_status_label = new QLabel( this );
		statusBar()->addWidget( _status_label, 1 );
		statusBar()->setSizeGripEnabled( false );

		connect( _sidebar, &HostSidebar::connect_requested, this, &MainWindow::open_session );
		connect( _sidebar, &HostSidebar::edit_requested, this, &MainWindow::edit_host );
		connect( _sidebar, &HostSidebar::new_host_requested, this, &MainWindow::new_host );
		connect( _tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::close_tab );
	}

	void MainWindow::build_menus(){
		QMenu* file_menu = menuBar()->addMenu( tr( "&File" ) );
		file_menu->addAction( tr( "New Host…" ), QKeySequence( QKeySequence::New ), this, &MainWindow::new_host );
		file_menu->addAction( tr( "Import from ~/.ssh/config…" ), this, &MainWindow::import_ssh_config );
		file_menu->addSeparator();
		file_menu->addAction( tr( "Close Session" ), QKeySequence( QKeySequence::Close ), this,
							  &MainWindow::close_current_tab );

		QMenu* edit_menu = menuBar()->addMenu( tr( "&Edit" ) );
		edit_menu->addAction( tr( "Copy" ), QKeySequence( QKeySequence::Copy ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->copy_selection();
		} );
		edit_menu->addAction( tr( "Paste" ), QKeySequence( QKeySequence::Paste ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->paste_from_clipboard();
		} );
		edit_menu->addAction( tr( "Select All" ), QKeySequence( QKeySequence::SelectAll ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->select_all();
		} );
		edit_menu->addSeparator();
		edit_menu->addAction( tr( "Find Host" ), QKeySequence( QKeySequence::Find ), _sidebar,
							  &HostSidebar::focus_search );

		QMenu* view_menu = menuBar()->addMenu( tr( "&View" ) );
		view_menu->addAction( tr( "Toggle File Browser" ), QKeySequence( Qt::CTRL | Qt::Key_B ), this,
							  &MainWindow::toggle_file_browser );
		view_menu->addSeparator();
		view_menu->addAction( tr( "Bigger Text" ), QKeySequence( Qt::CTRL | Qt::Key_Plus ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->increase_font_size();
		} );
		view_menu->addAction( tr( "Smaller Text" ), QKeySequence( Qt::CTRL | Qt::Key_Minus ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->decrease_font_size();
		} );
		view_menu->addAction( tr( "Actual Size" ), QKeySequence( Qt::CTRL | Qt::Key_0 ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->reset_font_size();
		} );
		view_menu->addSeparator();
		view_menu->addAction( tr( "Clear Buffer" ), QKeySequence( Qt::CTRL | Qt::Key_K ), this, [this]{
			if( SessionTab* session = current_session() )
				session->terminal()->clear_screen();
		} );

		QMenu* session_menu = menuBar()->addMenu( tr( "&Session" ) );
		session_menu->addAction( tr( "Reconnect" ), QKeySequence( Qt::CTRL | Qt::Key_R ), this,
								 &MainWindow::reconnect_current );

		QMenu* help_menu = menuBar()->addMenu( tr( "&Help" ) );
		help_menu->addAction( tr( "About ArTerm" ), this, [this]{
			QMessageBox::about( this, tr( "About ArTerm" ),
								tr( "<b>ArTerm %1</b><br><br>"
									"An SSH client and SFTP/SCP file manager for macOS.<br>"
									"Built with Qt 6 and libssh2." )
									.arg( QStringLiteral( ARTERM_VERSION ) ) );
		} );
	}

	void MainWindow::build_tool_bar(){
		auto* tool_bar = addToolBar( tr( "Main" ) );
		tool_bar->setObjectName( QStringLiteral( "appToolBar" ) );
		tool_bar->setMovable( false );
		tool_bar->setFloatable( false );
		tool_bar->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
		tool_bar->setIconSize( QSize( 16, 16 ) );

		tool_bar->addAction( icon( QStringLiteral( "plus" ) ), tr( "New Host" ), this, &MainWindow::new_host );
		tool_bar->addAction( icon( QStringLiteral( "refresh" ) ), tr( "Reconnect" ), this,
							 &MainWindow::reconnect_current );

		QAction* browser_action = tool_bar->addAction( icon( QStringLiteral( "split" ) ), tr( "Files" ), this,
													   &MainWindow::toggle_file_browser );
		browser_action->setCheckable( true );
		browser_action->setChecked( true );

		connect( _tabs, &QTabWidget::currentChanged, this, [this, browser_action]{
			SessionTab* session = current_session();
			browser_action->setEnabled( session != nullptr );
			if( session != nullptr )
				browser_action->setChecked( session->is_file_browser_visible() );
		} );
	}

	void MainWindow::wire_interaction(){
		connect(
			_interaction.get(), &ssh::SessionInteraction::host_key_decision_requested, this,
			[this]( ssh::HostKeyInfo const& info, bool* accepted ) { *accepted = HostKeyDialog::ask( info, this ); } );

		connect( _interaction.get(), &ssh::SessionInteraction::credential_requested, this,
				 [this]( QString const& prompt, bool echo, QString* answer, bool* provided ){
					 bool          ok = false;
					 QString const value =
						 QInputDialog::getText( this, tr( "Authentication" ), prompt,
												echo ? QLineEdit::Normal : QLineEdit::Password, QString(), &ok );
					 *provided = ok;
					 if( ok )
						 *answer = value;
				 } );
	}

	SessionTab* MainWindow::current_session() const{
		return session_at( _tabs->currentIndex() );
	}

	SessionTab* MainWindow::session_at( int index ) const{
		if( index < 0 || index >= _tabs->count() )
			return nullptr;
		return qobject_cast<SessionTab*>( _tabs->widget( index ) );
	}

	void MainWindow::open_session( QString const& host_id ){
		auto const stored = _store->profile_by_id( host_id );
		if( !stored ){
			show_status( tr( "That host no longer exists." ) );
			return;
		}

		// Secrets are pulled from the keychain only at connect time, so they exist
		// in memory for as long as the session and no longer.
		ssh::HostProfile const profile = _store->with_secrets( host_id );

		auto* session = new SessionTab( profile, _interaction, this );

		int const index = _tabs->addTab( session, profile.display_name() );
		_tabs->setTabIcon( index, icon( QStringLiteral( "terminal" ) ) );
		_tabs->setCurrentIndex( index );

		connect( session, &SessionTab::title_changed, this,
				 [this, session]( QString const& ) { update_tab_title( session ); } );
		connect( session, &SessionTab::status_message, this,
				 [this]( QString const& message ) { show_status( message ); } );
		connect( session, &SessionTab::connection_state_changed, this,
				 [this, session] { update_tab_title( session ); } );

		session->connect_to_host();
		session->focus_terminal();
	}

	void MainWindow::update_tab_title( SessionTab* session ){
		int const index = _tabs->indexOf( session );
		if( index < 0 )
			return;

		_tabs->setTabText( index, session->display_title() );
		_tabs->setTabIcon( index,
						   icon( QStringLiteral( "terminal" ),
								 session->is_connected() ? Theme::current().success : Theme::current().text_muted ) );
	}

	void MainWindow::new_host(){
		HostEditorDialog dialog( this );

		ssh::HostProfile fresh;
		fresh.username = qEnvironmentVariable( "USER" );
		dialog.set_profile( fresh );

		if( dialog.exec() != QDialog::Accepted )
			return;

		QString const id = _store->add( dialog.profile() );
		show_status( tr( "Host saved." ) );
		open_session( id );
	}

	void MainWindow::edit_host( QString const& host_id ){
		auto const stored = _store->profile_by_id( host_id );
		if( !stored )
			return;

		HostEditorDialog dialog( this );
		dialog.set_profile( _store->with_secrets( host_id ) );

		if( dialog.exec() != QDialog::Accepted )
			return;

		_store->update( dialog.profile() );
		show_status( tr( "Host updated. Reconnect for the changes to take effect." ) );
	}

	void MainWindow::import_ssh_config(){
		int const imported = _store->import_from_ssh_config();

		if( imported == 0 ){
			show_status( tr( "Nothing new to import from ~/.ssh/config." ) );
			return;
		}
		show_status( tr( "Imported %n host(s) from ~/.ssh/config.", nullptr, imported ) );
	}

	void MainWindow::close_tab( int index ){
		SessionTab* session = session_at( index );
		if( session == nullptr )
			return;

		if( session->has_active_transfers() ){
			auto const answer =
				QMessageBox::warning( this, tr( "Close Session" ),
									  tr( "Transfers are still running on this session. Closing it cancels them." ),
									  QMessageBox::Cancel | QMessageBox::Close, QMessageBox::Cancel );
			if( answer != QMessageBox::Close )
				return;
		}

		_tabs->removeTab( index );
		session->disconnect_from_host();
		session->deleteLater();
	}

	void MainWindow::close_current_tab(){
		if( _tabs->count() > 0 )
			close_tab( _tabs->currentIndex() );
	}

	void MainWindow::reconnect_current(){
		SessionTab* session = current_session();
		if( session == nullptr )
			return;

		ssh::HostProfile const profile = session->profile();
		int const              index   = _tabs->indexOf( session );

		_tabs->removeTab( index );
		session->disconnect_from_host();
		session->deleteLater();

		auto* replacement = new SessionTab( profile, _interaction, this );
		_tabs->insertTab( index, replacement, profile.display_name() );
		_tabs->setCurrentIndex( index );

		connect( replacement, &SessionTab::title_changed, this,
				 [this, replacement]( QString const& ) { update_tab_title( replacement ); } );
		connect( replacement, &SessionTab::status_message, this,
				 [this]( QString const& message ) { show_status( message ); } );
		connect( replacement, &SessionTab::connection_state_changed, this,
				 [this, replacement] { update_tab_title( replacement ); } );

		replacement->connect_to_host();
		replacement->focus_terminal();
	}

	void MainWindow::toggle_file_browser(){
		SessionTab* session = current_session();
		if( session == nullptr )
			return;
		session->set_file_browser_visible( !session->is_file_browser_visible() );
	}

	void MainWindow::show_status( QString const& message, int timeout_ms ){
		_status_label->setText( message );

		if( timeout_ms > 0 ){
			QTimer::singleShot( timeout_ms, this, [this, message]{
				if( _status_label->text() == message )
					_status_label->clear();
			} );
		}
	}

	void MainWindow::restore_window_state(){
		QSettings        settings;
		QByteArray const geometry = settings.value( QStringLiteral( "window/geometry" ) ).toByteArray();
		if( !geometry.isEmpty() )
			restoreGeometry( geometry );
		else
			resize( 1280, 800 );

		QByteArray const state = settings.value( QStringLiteral( "window/state" ) ).toByteArray();
		if( !state.isEmpty() )
			restoreState( state );

		if( auto* splitter = findChild<QSplitter*>( QStringLiteral( "mainSplitter" ) ) ){
			QByteArray const splitter_state = settings.value( QStringLiteral( "window/splitter" ) ).toByteArray();
			if( !splitter_state.isEmpty() )
				splitter->restoreState( splitter_state );
			else
				splitter->setSizes( { 240, 1040 } );
		}
	}

	void MainWindow::save_window_state() const{
		QSettings settings;
		settings.setValue( QStringLiteral( "window/geometry" ), saveGeometry() );
		settings.setValue( QStringLiteral( "window/state" ), saveState() );

		if( auto* splitter = findChild<QSplitter*>( QStringLiteral( "mainSplitter" ) ) )
			settings.setValue( QStringLiteral( "window/splitter" ), splitter->saveState() );
	}

	void MainWindow::closeEvent( QCloseEvent* event ){
		int transferring = 0;
		for( int i = 0; i < _tabs->count(); ++i ){
			if( SessionTab* session = session_at( i ); session != nullptr && session->has_active_transfers() )
				++transferring;
		}

		if( transferring > 0 ){
			auto const answer = QMessageBox::warning(
				this, tr( "Quit ArTerm" ),
				tr( "%n session(s) still have transfers running. Quitting cancels them.", nullptr, transferring ),
				QMessageBox::Cancel | QMessageBox::Close, QMessageBox::Cancel );
			if( answer != QMessageBox::Close ){
				event->ignore();
				return;
			}
		}

		save_window_state();

		// Tear the sessions down explicitly so the worker threads are joined before
		// the Qt event loop goes away.
		while( _tabs->count() > 0 ){
			SessionTab* session = session_at( 0 );
			_tabs->removeTab( 0 );
			if( session != nullptr ){
				session->disconnect_from_host();
				delete session;
			}
		}

		event->accept();
	}

} // namespace arterm::ui
