#include "files/file_mime_data.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QUrl>

namespace arterm::files
{

	void set_remote_payload( QMimeData* mime, RemoteDragPayload const& payload ){
		if( mime == nullptr || !payload.is_valid() )
			return;

		QJsonArray paths;
		for( QString const& path : payload.paths )
			paths.append( path );

		QJsonObject root;
		root.insert( QStringLiteral( "session" ), payload.session_id );
		root.insert( QStringLiteral( "paths" ), paths );

		mime->setData( QLatin1String( REMOTE_FILES_MIME_TYPE ),
					   QJsonDocument( root ).toJson( QJsonDocument::Compact ) );

		// A readable text fallback so dropping onto the terminal or a text editor
		// yields the paths rather than nothing.
		mime->setText( payload.paths.join( QLatin1Char( ' ' ) ) );
	}

	RemoteDragPayload remote_payload( QMimeData const* mime ){
		RemoteDragPayload payload;
		if( mime == nullptr || !mime->hasFormat( QLatin1String( REMOTE_FILES_MIME_TYPE ) ) )
			return payload;

		QJsonDocument const document = QJsonDocument::fromJson( mime->data( QLatin1String( REMOTE_FILES_MIME_TYPE ) ) );
		if( !document.isObject() )
			return payload;

		QJsonObject const root = document.object();
		payload.session_id     = root.value( QStringLiteral( "session" ) ).toString();

		QJsonArray const paths = root.value( QStringLiteral( "paths" ) ).toArray();
		payload.paths.reserve( paths.size() );
		for( QJsonValue const& value : paths ){
			QString const path = value.toString();
			if( !path.isEmpty() )
				payload.paths << path;
		}

		return payload;
	}

	QStringList local_paths( QMimeData const* mime ){
		QStringList paths;
		if( mime == nullptr || !mime->hasUrls() )
			return paths;

		for( QUrl const& url : mime->urls() ){
			if( url.isLocalFile() )
				paths << url.toLocalFile();
		}

		return paths;
	}

} // namespace arterm::files
