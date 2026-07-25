#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QString>

class QApplication;

namespace arterm::ui {

/// The application's design tokens.
///
/// Every colour, radius and spacing value used by the stylesheet lives here.
/// `Theme::stylesheet()` loads `:/theme/arterm-dark.qss` and substitutes the
/// `@token` placeholders, so the visual language is defined once and the QSS
/// stays readable.
class Theme {
public:
    enum class Mode { Dark, Light };

    static Theme dark();
    static Theme light();

    /// Applies the palette, the stylesheet and the default fonts to `app`.
    static void apply(QApplication &app, const Theme &theme);

    /// The theme currently applied.
    [[nodiscard]] static const Theme &current();

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }

    // -- Surfaces ---------------------------------------------------------
    QColor canvas;     ///< Window background, the darkest surface.
    QColor sidebar;    ///< Host list.
    QColor surface;    ///< Panels and cards.
    QColor elevated;   ///< Hover states, inputs, menus.
    QColor terminalBackground;

    // -- Lines ------------------------------------------------------------
    QColor borderSubtle;
    QColor borderStrong;

    // -- Text -------------------------------------------------------------
    QColor textPrimary;
    QColor textSecondary;
    QColor textMuted;
    QColor textOnAccent;

    // -- Accent and status ------------------------------------------------
    QColor accent;
    QColor accentHover;
    QColor accentSubtle; ///< Selection fills, badges.
    QColor success;
    QColor warning;
    QColor danger;

    // -- Metrics ----------------------------------------------------------
    int radiusSmall{6};
    int radiusMedium{8};
    int radiusLarge{12};

    /// The UI font; the terminal picks its own monospaced family.
    [[nodiscard]] QFont uiFont() const;
    [[nodiscard]] QFont monospaceFont() const;

    /// The stylesheet with all tokens resolved.
    [[nodiscard]] QString stylesheet() const;

private:
    [[nodiscard]] QHash<QString, QString> tokens() const;

    Mode m_mode{Mode::Dark};
};

} // namespace arterm::ui
