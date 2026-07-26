#include "core/result.hpp"
#include "ssh/shell_session.hpp"
#include "ssh/ssh_types.hpp"
#include "terminal/terminal.hpp"
#include "ui/main_window.hpp"
#include "ui/theme.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QMetaType>
#include <QPalette>
#include <QStyleHints>

using namespace arterm;

namespace
{

	/// Types crossing thread boundaries in queued signals must be registered before
	/// the first emission, otherwise Qt drops the call with a runtime warning.
	void register_meta_types(){
		qRegisterMetaType<Error>( "arterm::Error" );
		qRegisterMetaType<ssh::HostProfile>( "arterm::ssh::HostProfile" );
		qRegisterMetaType<ssh::HostKeyInfo>( "arterm::ssh::HostKeyInfo" );
		qRegisterMetaType<ssh::RemoteFileEntry>( "arterm::ssh::RemoteFileEntry" );
		qRegisterMetaType<ssh::RemoteListing>( "arterm::ssh::RemoteListing" );
		qRegisterMetaType<ssh::TransferProgress>( "arterm::ssh::TransferProgress" );
		qRegisterMetaType<ssh::ShellSession::State>( "arterm::ssh::ShellSession::State" );
		qRegisterMetaType<term::MouseTracking>( "arterm::term::MouseTracking" );
	}

	/// True when the system is using a dark appearance.
	///
	/// `QStyleHints::colorScheme()` only exists from Qt 6.5, so the fallback reads
	/// the system palette: a window background darker than its text means dark mode.
	bool system_prefers_dark_appearance( QApplication const& app ){
#if QT_VERSION >= QT_VERSION_CHECK( 6, 5, 0 )
		return app.styleHints()->colorScheme() != Qt::ColorScheme::Light;
#else
		QPalette const palette = app.palette();
		return palette.color( QPalette::Window ).lightness() < palette.color( QPalette::WindowText ).lightness();
#endif
	}

	/// ARTERM_THEME=dark|light overrides the system appearance. Useful on setups
	/// where Qt cannot read the platform preference, and for screenshots.
	ui::Theme select_theme( QApplication const& app ){
		QString const requested = qEnvironmentVariable( "ARTERM_THEME" ).trimmed().toLower();

		if( requested == QLatin1String( "dark" ) )
			return ui::Theme::dark();
		if( requested == QLatin1String( "light" ) )
			return ui::Theme::light();

		return system_prefers_dark_appearance( app ) ? ui::Theme::dark() : ui::Theme::light();
	}

} // namespace

int main( int argc, char* argv[] ){
	// Ctrl and Cmd are swapped by default on macOS, which would send Cmd+C to
	// the remote shell as an interrupt. ArTerm wants the physical keys.
	QApplication::setAttribute( Qt::AA_MacDontSwapCtrlAndMeta, true );

	QApplication app( argc, argv );

	QCoreApplication::setOrganizationName( QStringLiteral( "ArTerm" ) );
	QCoreApplication::setOrganizationDomain( QStringLiteral( "arterm.app" ) );
	QCoreApplication::setApplicationName( QStringLiteral( "ArTerm" ) );
	QCoreApplication::setApplicationVersion( QStringLiteral( ARTERM_VERSION ) );

	QCommandLineParser parser;
	parser.setApplicationDescription( QCoreApplication::translate( "main", "SSH client and SFTP/SCP file manager" ) );
	parser.addHelpOption();
	parser.addVersionOption();

	// Only the long form: addVersionOption() already claims "-v".
	QCommandLineOption const verbose_option( QStringLiteral( "verbose" ),
											 QCoreApplication::translate( "main", "Enable debug logging." ) );
	parser.addOption( verbose_option );
	parser.process( app );

	if( parser.isSet( verbose_option ) )
		QLoggingCategory::setFilterRules( QStringLiteral( "arterm.*=true" ) );
	else
		QLoggingCategory::setFilterRules( QStringLiteral( "arterm.*.debug=false" ) );

	register_meta_types();

	ui::Theme::apply( app, select_theme( app ) );

	ui::MainWindow window;
	window.show();

	return app.exec();
}
