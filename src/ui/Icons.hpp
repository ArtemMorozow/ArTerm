#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace arterm::ui {

/// Loads a monochrome SVG from `:/icons/<name>.svg` and tints it with the
/// current theme's icon colour.
///
/// Results are cached, so calling this from a widget constructor for every
/// button costs one rasterisation per name and colour.
[[nodiscard]] QIcon icon(const QString &name);

/// Same, but with an explicit colour - used for state-dependent icons such as
/// the connection indicator.
[[nodiscard]] QIcon icon(const QString &name, const QColor &color);

/// Icon matching a file's type, used in the browser's Name column.
[[nodiscard]] QIcon fileIcon(const QString &fileName, bool isDirectory, bool isSymlink);

/// Sets the colour later `icon(name)` calls tint with. Called by the theme.
void setIconColor(const QColor &color);
[[nodiscard]] QColor iconColor();

/// Drops the cache; call after a theme change.
void clearIconCache();

} // namespace arterm::ui
