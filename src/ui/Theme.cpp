#include "ui/theme.hpp"

#include "ui/icons.hpp"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace arterm::ui
{
	namespace
	{

		Theme g_current = Theme::dark();

		QString color_token( QColor const& color ){
			if( color.alpha() == 255 )
				return color.name( QColor::HexRgb );
			return QStringLiteral( "rgba(%1, %2, %3, %4)" )
				.arg( color.red() )
				.arg( color.green() )
				.arg( color.blue() )
				.arg( QString::number( color.alphaF(), 'f', 3 ) );
		}

	} // namespace

	Theme Theme::dark(){
		Theme theme;
		theme._mode = Mode::DARK;

		theme.canvas              = QColor( 0x0E, 0x10, 0x16 );
		theme.sidebar             = QColor( 0x12, 0x14, 0x1A );
		theme.surface             = QColor( 0x17, 0x1A, 0x21 );
		theme.elevated            = QColor( 0x1D, 0x21, 0x2A );
		theme.terminal_background = QColor( 0x12, 0x14, 0x1A );

		theme.border_subtle = QColor( 0x23, 0x27, 0x2F );
		theme.border_strong = QColor( 0x2E, 0x34, 0x40 );

		theme.text_primary   = QColor( 0xE6, 0xE9, 0xEF );
		theme.text_secondary = QColor( 0x9A, 0xA3, 0xB2 );
		theme.text_muted     = QColor( 0x6B, 0x74, 0x82 );
		theme.text_on_accent = QColor( 0xFF, 0xFF, 0xFF );

		theme.accent        = QColor( 0x4C, 0x8D, 0xFF );
		theme.accent_hover  = QColor( 0x6B, 0xA1, 0xFF );
		theme.accent_subtle = QColor( 0x4C, 0x8D, 0xFF, 36 );

		theme.success = QColor( 0x3F, 0xBF, 0x7F );
		theme.warning = QColor( 0xE5, 0xB5, 0x67 );
		theme.danger  = QColor( 0xF2, 0x64, 0x6C );

		return theme;
	}

	Theme Theme::light(){
		Theme theme;
		theme._mode = Mode::LIGHT;

		theme.canvas              = QColor( 0xF4, 0xF6, 0xFA );
		theme.sidebar             = QColor( 0xEE, 0xF1, 0xF7 );
		theme.surface             = QColor( 0xFF, 0xFF, 0xFF );
		theme.elevated            = QColor( 0xF7, 0xF9, 0xFC );
		theme.terminal_background = QColor( 0xFB, 0xFC, 0xFE );

		theme.border_subtle = QColor( 0xE0, 0xE4, 0xEC );
		theme.border_strong = QColor( 0xCB, 0xD2, 0xDE );

		theme.text_primary   = QColor( 0x1B, 0x1F, 0x27 );
		theme.text_secondary = QColor( 0x55, 0x5D, 0x6C );
		theme.text_muted     = QColor( 0x8A, 0x93, 0xA3 );
		theme.text_on_accent = QColor( 0xFF, 0xFF, 0xFF );

		theme.accent        = QColor( 0x2C, 0x6E, 0xE0 );
		theme.accent_hover  = QColor( 0x1E, 0x5C, 0xC8 );
		theme.accent_subtle = QColor( 0x2C, 0x6E, 0xE0, 30 );

		theme.success = QColor( 0x1E, 0x9E, 0x62 );
		theme.warning = QColor( 0xB4, 0x82, 0x1E );
		theme.danger  = QColor( 0xD3, 0x3B, 0x45 );

		return theme;
	}

	Theme const& Theme::current(){
		return g_current;
	}

	QFont Theme::ui_font() const{
		// The system UI font is the right default on macOS; Qt maps this to
		// SF Pro Text there and to the platform default elsewhere.
		QFont font = QFontDatabase::systemFont( QFontDatabase::GeneralFont );
#ifdef Q_OS_MACOS
		font.setPointSize( 13 );
#else
		font.setPointSize( 10 );
#endif
		return font;
	}

	QFont Theme::monospace_font() const{
		QFont font = QFontDatabase::systemFont( QFontDatabase::FixedFont );
		font.setStyleHint( QFont::Monospace );
		font.setPointSize( ui_font().pointSize() );
		return font;
	}

	QHash<QString, QString> Theme::tokens() const{
		return {
			{ QStringLiteral( "canvas" ), color_token( canvas ) },
			{ QStringLiteral( "sidebar" ), color_token( sidebar ) },
			{ QStringLiteral( "surface" ), color_token( surface ) },
			{ QStringLiteral( "elevated" ), color_token( elevated ) },
			{ QStringLiteral( "terminalBackground" ), color_token( terminal_background ) },
			{ QStringLiteral( "borderSubtle" ), color_token( border_subtle ) },
			{ QStringLiteral( "borderStrong" ), color_token( border_strong ) },
			{ QStringLiteral( "textPrimary" ), color_token( text_primary ) },
			{ QStringLiteral( "textSecondary" ), color_token( text_secondary ) },
			{ QStringLiteral( "textMuted" ), color_token( text_muted ) },
			{ QStringLiteral( "textOnAccent" ), color_token( text_on_accent ) },
			{ QStringLiteral( "accent" ), color_token( accent ) },
			{ QStringLiteral( "accentHover" ), color_token( accent_hover ) },
			{ QStringLiteral( "accentSubtle" ), color_token( accent_subtle ) },
			{ QStringLiteral( "success" ), color_token( success ) },
			{ QStringLiteral( "warning" ), color_token( warning ) },
			{ QStringLiteral( "danger" ), color_token( danger ) },
			// Lengths carry their unit so the QSS can write "border-radius: @radius_small;".
			{ QStringLiteral( "radiusSmall" ), QStringLiteral( "%1px" ).arg( radius_small ) },
			{ QStringLiteral( "radiusMedium" ), QStringLiteral( "%1px" ).arg( radius_medium ) },
			{ QStringLiteral( "radiusLarge" ), QStringLiteral( "%1px" ).arg( radius_large ) },
		};
	}

	QString Theme::stylesheet() const{
		QFile file( QStringLiteral( ":/theme/arterm-dark.qss" ) );
		if( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
			return {};

		QString sheet = QString::fromUtf8( file.readAll() );

		// Longest names first so "@accent_subtle" is not partially replaced by
		// "@accent".
		QStringList names = tokens().keys();
		std::sort( names.begin(), names.end(),
				   []( QString const& a, QString const& b ) { return a.size() > b.size(); } );

		QHash<QString, QString> const values = tokens();
		for( QString const& name : names )
			sheet.replace( QLatin1Char( '@' ) + name, values.value( name ) );

		return sheet;
	}

	void Theme::apply( QApplication& app, Theme const& theme ){
		g_current = theme;

		app.setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );

		QPalette palette;
		palette.setColor( QPalette::Window, theme.canvas );
		palette.setColor( QPalette::WindowText, theme.text_primary );
		palette.setColor( QPalette::Base, theme.surface );
		palette.setColor( QPalette::AlternateBase, theme.elevated );
		palette.setColor( QPalette::Text, theme.text_primary );
		palette.setColor( QPalette::PlaceholderText, theme.text_muted );
		palette.setColor( QPalette::Button, theme.elevated );
		palette.setColor( QPalette::ButtonText, theme.text_primary );
		palette.setColor( QPalette::Highlight, theme.accent );
		palette.setColor( QPalette::HighlightedText, theme.text_on_accent );
		palette.setColor( QPalette::ToolTipBase, theme.elevated );
		palette.setColor( QPalette::ToolTipText, theme.text_primary );
		palette.setColor( QPalette::Link, theme.accent );
		palette.setColor( QPalette::Disabled, QPalette::Text, theme.text_muted );
		palette.setColor( QPalette::Disabled, QPalette::ButtonText, theme.text_muted );
		palette.setColor( QPalette::Disabled, QPalette::WindowText, theme.text_muted );

		app.setPalette( palette );
		app.setFont( theme.ui_font() );

		set_icon_color( theme.text_secondary );
		clear_icon_cache();

		app.setStyleSheet( theme.stylesheet() );
	}

} // namespace arterm::ui
