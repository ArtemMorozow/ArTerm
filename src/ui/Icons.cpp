#include "ui/icons.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace arterm::ui
{
	namespace
	{

		QColor                g_iconColor{ 0x9A, 0xA3, 0xB2 };
		QHash<QString, QIcon> g_cache;

		constexpr int RENDER_SIZE = 64; ///< Rendered once at 64px and scaled down.

		QIcon render_icon( QString const& name, QColor const& color ){
			QString const path = QStringLiteral( ":/icons/%1.svg" ).arg( name );

			QFile file( path );
			if( !file.open( QIODevice::ReadOnly ) )
				return {};

			QSvgRenderer renderer( file.readAll() );
			if( !renderer.isValid() )
				return {};

			QPixmap pixmap( RENDER_SIZE, RENDER_SIZE );
			pixmap.fill( Qt::transparent );

			QPainter painter( &pixmap );
			painter.setRenderHint( QPainter::Antialiasing, true );
			renderer.render( &painter );

			// Tint by compositing the colour through the rendered alpha mask, which
			// keeps a single set of source SVGs usable in every theme.
			painter.setCompositionMode( QPainter::CompositionMode_SourceIn );
			painter.fillRect( pixmap.rect(), color );
			painter.end();

			return QIcon( pixmap );
		}

		/// Maps a file extension onto one of the bundled type icons.
		QString icon_name_for_extension( QString const& suffix ){
			static QHash<QString, QString> const mapping = {
				{ QStringLiteral( "cpp" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "hpp" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "h" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "c" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "py" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "js" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "ts" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "go" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "rs" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "rb" ), QStringLiteral( "file-code" ) },
				{ QStringLiteral( "sh" ), QStringLiteral( "file-terminal" ) },
				{ QStringLiteral( "bash" ), QStringLiteral( "file-terminal" ) },
				{ QStringLiteral( "zsh" ), QStringLiteral( "file-terminal" ) },
				{ QStringLiteral( "json" ), QStringLiteral( "file-config" ) },
				{ QStringLiteral( "yaml" ), QStringLiteral( "file-config" ) },
				{ QStringLiteral( "yml" ), QStringLiteral( "file-config" ) },
				{ QStringLiteral( "toml" ), QStringLiteral( "file-config" ) },
				{ QStringLiteral( "ini" ), QStringLiteral( "file-config" ) },
				{ QStringLiteral( "conf" ), QStringLiteral( "file-config" ) },
				{ QStringLiteral( "png" ), QStringLiteral( "file-image" ) },
				{ QStringLiteral( "jpg" ), QStringLiteral( "file-image" ) },
				{ QStringLiteral( "jpeg" ), QStringLiteral( "file-image" ) },
				{ QStringLiteral( "gif" ), QStringLiteral( "file-image" ) },
				{ QStringLiteral( "svg" ), QStringLiteral( "file-image" ) },
				{ QStringLiteral( "webp" ), QStringLiteral( "file-image" ) },
				{ QStringLiteral( "zip" ), QStringLiteral( "file-archive" ) },
				{ QStringLiteral( "gz" ), QStringLiteral( "file-archive" ) },
				{ QStringLiteral( "tar" ), QStringLiteral( "file-archive" ) },
				{ QStringLiteral( "bz2" ), QStringLiteral( "file-archive" ) },
				{ QStringLiteral( "xz" ), QStringLiteral( "file-archive" ) },
				{ QStringLiteral( "7z" ), QStringLiteral( "file-archive" ) },
				{ QStringLiteral( "log" ), QStringLiteral( "file-text" ) },
				{ QStringLiteral( "txt" ), QStringLiteral( "file-text" ) },
				{ QStringLiteral( "md" ), QStringLiteral( "file-text" ) },
			};

			return mapping.value( suffix.toLower(), QStringLiteral( "file" ) );
		}

	} // namespace

	void set_icon_color( QColor const& color ){
		if( g_iconColor == color )
			return;
		g_iconColor = color;
		g_cache.clear();
	}

	QColor icon_color(){
		return g_iconColor;
	}

	void clear_icon_cache(){
		g_cache.clear();
	}

	QIcon icon( QString const& name ){
		return icon( name, g_iconColor );
	}

	QIcon icon( QString const& name, QColor const& color ){
		QString const key = name + QLatin1Char( '#' ) + color.name( QColor::HexArgb );

		auto const cached = g_cache.constFind( key );
		if( cached != g_cache.constEnd() )
			return *cached;

		QIcon rendered = render_icon( name, color );
		g_cache.insert( key, rendered );
		return rendered;
	}

	QIcon file_icon( QString const& file_name, bool is_directory, bool is_symlink ){
		if( is_directory )
			return icon( is_symlink ? QStringLiteral( "folder-link" ) : QStringLiteral( "folder" ) );

		QFileInfo const info( file_name );
		return icon( icon_name_for_extension( info.suffix() ) );
	}

} // namespace arterm::ui
