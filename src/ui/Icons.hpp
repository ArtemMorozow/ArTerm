#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace arterm::ui
{

	/// Loads a monochrome SVG from `:/icons/<name>.svg` and tints it with the
	/// current theme's icon colour.
	///
	/// Results are cached, so calling this from a widget constructor for every
	/// button costs one rasterisation per name and colour.
	[[nodiscard]] QIcon icon( QString const& name );

	/// Same, but with an explicit colour - used for state-dependent icons such as
	/// the connection indicator.
	[[nodiscard]] QIcon icon( QString const& name, QColor const& color );

	/// Icon matching a file's type, used in the browser's Name column.
	[[nodiscard]] QIcon file_icon( QString const& file_name, bool is_directory, bool is_symlink );

	/// Sets the colour later `icon(name)` calls tint with. Called by the theme.
	void                 set_icon_color( QColor const& color );
	[[nodiscard]] QColor icon_color();

	/// Drops the cache; call after a theme change.
	void clear_icon_cache();

} // namespace arterm::ui
