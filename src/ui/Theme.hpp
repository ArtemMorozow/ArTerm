#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QString>

class QApplication;

namespace arterm::ui
{

	/// The application's design tokens.
	///
	/// Every colour, radius and spacing value used by the stylesheet lives here.
	/// `Theme::stylesheet()` loads `:/theme/arterm-dark.qss` and substitutes the
	/// `@token` placeholders, so the visual language is defined once and the QSS
	/// stays readable.
	class Theme
	{
	public:
		enum class Mode { DARK, LIGHT };

		static Theme dark();
		static Theme light();

		/// Applies the palette, the stylesheet and the default fonts to `app`.
		static void apply( QApplication& app, Theme const& theme );

		/// The theme currently applied.
		[[nodiscard]] static Theme const& current();

		[[nodiscard]] Mode mode() const noexcept { return _mode; }

		// -- Surfaces ---------------------------------------------------------
		QColor canvas;   ///< Window background, the darkest surface.
		QColor sidebar;  ///< Host list.
		QColor surface;  ///< Panels and cards.
		QColor elevated; ///< Hover states, inputs, menus.
		QColor terminal_background;

		// -- Lines ------------------------------------------------------------
		QColor border_subtle;
		QColor border_strong;

		// -- Text -------------------------------------------------------------
		QColor text_primary;
		QColor text_secondary;
		QColor text_muted;
		QColor text_on_accent;

		// -- Accent and status ------------------------------------------------
		QColor accent;
		QColor accent_hover;
		QColor accent_subtle; ///< Selection fills, badges.
		QColor success;
		QColor warning;
		QColor danger;

		// -- Metrics ----------------------------------------------------------
		int radius_small{ 6 };
		int radius_medium{ 8 };
		int radius_large{ 12 };

		/// The UI font; the terminal picks its own monospaced family.
		[[nodiscard]] QFont ui_font() const;
		[[nodiscard]] QFont monospace_font() const;

		/// The stylesheet with all tokens resolved.
		[[nodiscard]] QString stylesheet() const;

	private:
		[[nodiscard]] QHash<QString, QString> tokens() const;

		Mode _mode{ Mode::DARK };
	};

} // namespace arterm::ui
