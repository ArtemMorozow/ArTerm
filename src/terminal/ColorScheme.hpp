#pragma once

#include "terminal/Cell.hpp"

#include <QColor>
#include <QString>

#include <array>

namespace arterm::term {

/// The 256-entry ANSI palette plus the handful of colours a terminal needs
/// beyond it (background, foreground, cursor, selection).
///
/// ArTerm ships one scheme by default - the same low-contrast dark palette the
/// rest of the UI uses - but the type is a value so alternative schemes can be
/// swapped in without touching the renderer.
class ColorScheme {
public:
    ColorScheme();

    static ColorScheme arTermDark();
    static ColorScheme arTermLight();

    /// Named schemes available in the settings UI.
    [[nodiscard]] static QStringList availableSchemes();
    [[nodiscard]] static ColorScheme byName(const QString &name);

    [[nodiscard]] const QString &name() const noexcept { return m_name; }

    [[nodiscard]] QColor background() const noexcept { return m_background; }
    [[nodiscard]] QColor foreground() const noexcept { return m_foreground; }
    [[nodiscard]] QColor cursor() const noexcept { return m_cursor; }
    [[nodiscard]] QColor cursorText() const noexcept { return m_cursorText; }
    [[nodiscard]] QColor selection() const noexcept { return m_selection; }
    [[nodiscard]] QColor selectionText() const noexcept { return m_selectionText; }

    void setBackground(const QColor &color) { m_background = color; }
    void setForeground(const QColor &color) { m_foreground = color; }

    /// Palette slot 0-255.
    [[nodiscard]] QColor indexed(std::uint8_t index) const { return m_palette[index]; }
    void setIndexed(std::uint8_t index, const QColor &color) { m_palette[index] = color; }

    /// Resolve a cell colour, applying the bold-brightens-colours convention
    /// that every terminal inherits from the original xterm.
    [[nodiscard]] QColor resolve(const Color &color, bool isForeground, bool bold) const;

private:
    void fillCube();

    // These populate an existing instance. The named constructors and the
    // default constructor both route through them, so no constructor ever calls
    // another named constructor - that recursed until the stack ran out.
    static void applyDark(ColorScheme &scheme);
    static void applyLight(ColorScheme &scheme);

    QString m_name;
    std::array<QColor, 256> m_palette;
    QColor m_background;
    QColor m_foreground;
    QColor m_cursor;
    QColor m_cursorText;
    QColor m_selection;
    QColor m_selectionText;
};

} // namespace arterm::term
