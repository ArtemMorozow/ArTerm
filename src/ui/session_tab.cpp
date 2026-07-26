#include "ui/session_tab.hpp"

#include "files/file_browser.hpp"
#include "files/transfer_manager.hpp"
#include "session/remote_file_service.hpp"
#include "session/shell_service.hpp"
#include "ssh/session_interaction.hpp"
#include "terminal/terminal_widget.hpp"
#include "ui/theme.hpp"

#include <QLabel>
#include <QSplitter>
#include <QStyle>
#include <QUuid>
#include <QVBoxLayout>

namespace arterm::ui
{

	SessionTab::SessionTab( ssh::HostProfile profile, std::shared_ptr<ssh::SessionInteraction> interaction,
							QWidget* parent )
		: QWidget( parent )
		, _profile( std::move( profile ) )
		, _session_id( QUuid::createUuid().toString( QUuid::WithoutBraces ) )
		, _interaction( std::move( interaction ) )
	{
		build_ui();
	}

	SessionTab::~SessionTab(){
		disconnect_from_host();
	}

	void SessionTab::build_ui(){
		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 0, 0, 0, 0 );
		root->setSpacing( 0 );

		_banner = new QLabel( this );
		_banner->setObjectName( QStringLiteral( "connectionBanner" ) );
		_banner->setWordWrap( true );
		_banner->hide();

		_terminal = new term::TerminalWidget( this );

		Theme const&      theme = Theme::current();
		term::ColorScheme scheme =
			theme.mode() == Theme::Mode::DARK ? term::ColorScheme::arterm_dark() : term::ColorScheme::arterm_light();
		scheme.set_background( theme.terminal_background );
		_terminal->set_color_scheme( scheme );

		_shell = new session::ShellService( _profile, _interaction, this );
		_files = new session::RemoteFileService( _profile, _interaction, this );
		_files->set_transfer_backend( _profile.transfer_backend );

		_browser = new files::FileBrowser( _files, _session_id, this );
		_browser->set_remote_title( _profile.username.isEmpty()
										? _profile.hostname
										: _profile.username + QLatin1Char( '@' ) + _profile.hostname );

		_splitter = new QSplitter( Qt::Vertical, this );
		_splitter->addWidget( _terminal );
		_splitter->addWidget( _browser );
		_splitter->setStretchFactor( 0, 3 );
		_splitter->setStretchFactor( 1, 2 );
		_splitter->setChildrenCollapsible( false );
		_splitter->setHandleWidth( 1 );

		// Stretch factors alone leave the browser squeezed, because the terminal's
		// size hint is much larger. Seed explicit sizes so both panes start usable.
		_splitter->setSizes( { 560, 380 } );
		_browser->setMinimumHeight( 220 );

		root->addWidget( _banner );
		root->addWidget( _splitter, 1 );

		_browser->setVisible( _profile.open_file_browser );

		wire_shell();
		wire_files();
	}

	void SessionTab::wire_shell(){
		connect( _terminal, &term::TerminalWidget::data_entered, _shell, &session::ShellService::write );

		connect( _terminal, &term::TerminalWidget::terminal_resized, _shell, &session::ShellService::resize );

		connect( _terminal, &term::TerminalWidget::title_changed, this, [this]( QString const& title ){
			_remote_title = title;
			Q_EMIT title_changed( display_title() );
		} );

		// Files dropped on the terminal are uploaded into whatever directory the
		// remote pane is showing, which is the least surprising interpretation.
		connect( _terminal, &term::TerminalWidget::files_dropped, this, [this]( QStringList const& paths ){
			if( !_browser->isVisible() )
				set_file_browser_visible( true );
			_browser->upload_to_current_remote_directory( paths );
		} );

		connect( _shell, &session::ShellService::data_received, _terminal, &term::TerminalWidget::receive );

		connect( _shell, &session::ShellService::connected, this, [this]( QString const&, QString const& auth_method ){
			_shell_ready = true;
			hide_banner();
			Q_EMIT status_message( tr( "Connected to %1 (%2)" ).arg( _profile.endpoint(), auth_method ) );
			Q_EMIT connection_state_changed();

			// The PTY was opened with the default 80x24; push the real size.
			_terminal->terminal()->resize( _terminal->columns(), _terminal->rows() );
			_shell->resize( _terminal->columns(), _terminal->rows(), _terminal->width(), _terminal->height() );
		} );

		connect( _shell, &session::ShellService::state_changed, this, [this]( ssh::ShellSession::State state ){
			switch( state ){
				case ssh::ShellSession::State::CONNECTING:
					show_banner( tr( "Connecting to %1…" ).arg( _profile.endpoint() ), QStringLiteral( "connecting" ) );
					break;
				case ssh::ShellSession::State::AUTHENTICATING:
					show_banner( tr( "Authenticating as %1…" ).arg( _profile.username ),
								 QStringLiteral( "connecting" ) );
					break;
				default:
					break;
			}
			Q_EMIT connection_state_changed();
		} );

		connect( _shell, &session::ShellService::failed, this, [this]( Error const& error ){
			_shell_ready = false;
			show_banner( error.message, QStringLiteral( "error" ) );
			Q_EMIT status_message( error.message );
			Q_EMIT connection_state_changed();
		} );

		connect( _shell, &session::ShellService::closed, this, [this]( int exit_status ){
			_shell_ready = false;
			show_banner( tr( "The session ended." ), QStringLiteral( "error" ) );
			Q_EMIT connection_state_changed();
			Q_EMIT session_closed( exit_status );
		} );
	}

	void SessionTab::wire_files(){
		connect( _files, &session::RemoteFileService::ready, this, [this]( QString const& home ){
			_browser->set_connected( true, home );
			Q_EMIT connection_state_changed();
		} );

		connect( _files, &session::RemoteFileService::failed, this, [this]( Error const& error ){
			_browser->set_connected( false, {} );
			// A failing SFTP subsystem must not look like a failed session: the
			// shell may well be fine, so this is reported as a status message.
			Q_EMIT status_message( tr( "File browser unavailable: %1" ).arg( error.message ) );
		} );

		connect( _browser, &files::FileBrowser::status_message, this, &SessionTab::status_message );
	}

	void SessionTab::connect_to_host(){
		show_banner( tr( "Connecting to %1…" ).arg( _profile.endpoint() ), QStringLiteral( "connecting" ) );

		_shell->start();

		if( _profile.open_file_browser )
			_files->start();
	}

	void SessionTab::disconnect_from_host(){
		if( _shell != nullptr )
			_shell->stop();
		if( _files != nullptr )
			_files->stop();
	}

	bool SessionTab::is_connected() const{
		return _shell_ready;
	}

	bool SessionTab::has_active_transfers() const{
		return _browser != nullptr && _browser->transfers()->has_active_transfers();
	}

	QString SessionTab::display_title() const{
		if( !_remote_title.isEmpty() )
			return _remote_title;
		return _profile.display_name();
	}

	void SessionTab::set_file_browser_visible( bool visible ){
		_browser->setVisible( visible );

		// Start the SFTP connection lazily: a profile with the browser turned off
		// should not open a second connection until the user asks for one.
		if( visible && !_files->is_connected() )
			_files->start();
	}

	bool SessionTab::is_file_browser_visible() const{
		return _browser->isVisible();
	}

	void SessionTab::focus_terminal(){
		_terminal->setFocus( Qt::OtherFocusReason );
	}

	void SessionTab::show_banner( QString const& message, QString const& state ){
		_banner->setText( message );
		_banner->setProperty( "state", state );
		_banner->style()->unpolish( _banner );
		_banner->style()->polish( _banner );
		_banner->show();
	}

	void SessionTab::hide_banner(){
		_banner->hide();
	}

} // namespace arterm::ui
