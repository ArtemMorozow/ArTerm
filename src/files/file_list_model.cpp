#include "files/file_list_model.hpp"

#include <QLocale>

#include <cmath>

namespace arterm::files
{

	QString format_file_size( quint64 bytes ){
		if( bytes < 1024 )
			return QObject::tr( "%1 bytes" ).arg( bytes );

		static char const* const units[] = { "KB", "MB", "GB", "TB", "PB" };

		double value = static_cast<double>( bytes ) / 1024.0;
		int    unit  = 0;
		while( value >= 1024.0 && unit < 4 ){
			value /= 1024.0;
			++unit;
		}

		// One decimal below 10 units keeps the column narrow without losing
		// meaningful precision.
		int const precision = value < 10.0 ? 1 : 0;
		return QStringLiteral( "%1 %2" ).arg( QLocale::system().toString( value, 'f', precision ),
											  QLatin1String( units[unit] ) );
	}

	QString format_timestamp( QDateTime const& timestamp ){
		if( !timestamp.isValid() )
			return {};

		QDate const   today  = QDate::currentDate();
		QDate const   date   = timestamp.date();
		QLocale const locale = QLocale::system();

		if( date == today )
			return QObject::tr( "Today %1" ).arg( locale.toString( timestamp.time(), QLocale::ShortFormat ) );
		if( date == today.addDays( -1 ) )
			return QObject::tr( "Yesterday %1" ).arg( locale.toString( timestamp.time(), QLocale::ShortFormat ) );
		if( date.year() == today.year() )
			return locale.toString( timestamp, QStringLiteral( "d MMM HH:mm" ) );

		return locale.toString( timestamp, QStringLiteral( "d MMM yyyy" ) );
	}

} // namespace arterm::files
