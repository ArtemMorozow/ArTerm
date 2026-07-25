#include "ui/Theme.hpp"

#include "ui/Icons.hpp"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

namespace arterm::ui {
namespace {

Theme g_current = Theme::dark();

QString colorToken(const QColor &color)
{
    if (color.alpha() == 255)
        return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

} // namespace

Theme Theme::dark()
{
    Theme theme;
    theme.m_mode = Mode::Dark;

    theme.canvas = QColor(0x0E, 0x10, 0x16);
    theme.sidebar = QColor(0x12, 0x14, 0x1A);
    theme.surface = QColor(0x17, 0x1A, 0x21);
    theme.elevated = QColor(0x1D, 0x21, 0x2A);
    theme.terminalBackground = QColor(0x12, 0x14, 0x1A);

    theme.borderSubtle = QColor(0x23, 0x27, 0x2F);
    theme.borderStrong = QColor(0x2E, 0x34, 0x40);

    theme.textPrimary = QColor(0xE6, 0xE9, 0xEF);
    theme.textSecondary = QColor(0x9A, 0xA3, 0xB2);
    theme.textMuted = QColor(0x6B, 0x74, 0x82);
    theme.textOnAccent = QColor(0xFF, 0xFF, 0xFF);

    theme.accent = QColor(0x4C, 0x8D, 0xFF);
    theme.accentHover = QColor(0x6B, 0xA1, 0xFF);
    theme.accentSubtle = QColor(0x4C, 0x8D, 0xFF, 36);

    theme.success = QColor(0x3F, 0xBF, 0x7F);
    theme.warning = QColor(0xE5, 0xB5, 0x67);
    theme.danger = QColor(0xF2, 0x64, 0x6C);

    return theme;
}

Theme Theme::light()
{
    Theme theme;
    theme.m_mode = Mode::Light;

    theme.canvas = QColor(0xF4, 0xF6, 0xFA);
    theme.sidebar = QColor(0xEE, 0xF1, 0xF7);
    theme.surface = QColor(0xFF, 0xFF, 0xFF);
    theme.elevated = QColor(0xF7, 0xF9, 0xFC);
    theme.terminalBackground = QColor(0xFB, 0xFC, 0xFE);

    theme.borderSubtle = QColor(0xE0, 0xE4, 0xEC);
    theme.borderStrong = QColor(0xCB, 0xD2, 0xDE);

    theme.textPrimary = QColor(0x1B, 0x1F, 0x27);
    theme.textSecondary = QColor(0x55, 0x5D, 0x6C);
    theme.textMuted = QColor(0x8A, 0x93, 0xA3);
    theme.textOnAccent = QColor(0xFF, 0xFF, 0xFF);

    theme.accent = QColor(0x2C, 0x6E, 0xE0);
    theme.accentHover = QColor(0x1E, 0x5C, 0xC8);
    theme.accentSubtle = QColor(0x2C, 0x6E, 0xE0, 30);

    theme.success = QColor(0x1E, 0x9E, 0x62);
    theme.warning = QColor(0xB4, 0x82, 0x1E);
    theme.danger = QColor(0xD3, 0x3B, 0x45);

    return theme;
}

const Theme &Theme::current()
{
    return g_current;
}

QFont Theme::uiFont() const
{
    // The system UI font is the right default on macOS; Qt maps this to
    // SF Pro Text there and to the platform default elsewhere.
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_MACOS
    font.setPointSize(13);
#else
    font.setPointSize(10);
#endif
    return font;
}

QFont Theme::monospaceFont() const
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(uiFont().pointSize());
    return font;
}

QHash<QString, QString> Theme::tokens() const
{
    return {
        {QStringLiteral("canvas"), colorToken(canvas)},
        {QStringLiteral("sidebar"), colorToken(sidebar)},
        {QStringLiteral("surface"), colorToken(surface)},
        {QStringLiteral("elevated"), colorToken(elevated)},
        {QStringLiteral("terminalBackground"), colorToken(terminalBackground)},
        {QStringLiteral("borderSubtle"), colorToken(borderSubtle)},
        {QStringLiteral("borderStrong"), colorToken(borderStrong)},
        {QStringLiteral("textPrimary"), colorToken(textPrimary)},
        {QStringLiteral("textSecondary"), colorToken(textSecondary)},
        {QStringLiteral("textMuted"), colorToken(textMuted)},
        {QStringLiteral("textOnAccent"), colorToken(textOnAccent)},
        {QStringLiteral("accent"), colorToken(accent)},
        {QStringLiteral("accentHover"), colorToken(accentHover)},
        {QStringLiteral("accentSubtle"), colorToken(accentSubtle)},
        {QStringLiteral("success"), colorToken(success)},
        {QStringLiteral("warning"), colorToken(warning)},
        {QStringLiteral("danger"), colorToken(danger)},
        // Lengths carry their unit so the QSS can write "border-radius: @radiusSmall;".
        {QStringLiteral("radiusSmall"), QStringLiteral("%1px").arg(radiusSmall)},
        {QStringLiteral("radiusMedium"), QStringLiteral("%1px").arg(radiusMedium)},
        {QStringLiteral("radiusLarge"), QStringLiteral("%1px").arg(radiusLarge)},
    };
}

QString Theme::stylesheet() const
{
    QFile file(QStringLiteral(":/theme/arterm-dark.qss"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QString sheet = QString::fromUtf8(file.readAll());

    // Longest names first so "@accentSubtle" is not partially replaced by
    // "@accent".
    QStringList names = tokens().keys();
    std::sort(names.begin(), names.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });

    const QHash<QString, QString> values = tokens();
    for (const QString &name : names)
        sheet.replace(QLatin1Char('@') + name, values.value(name));

    return sheet;
}

void Theme::apply(QApplication &app, const Theme &theme)
{
    g_current = theme;

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, theme.canvas);
    palette.setColor(QPalette::WindowText, theme.textPrimary);
    palette.setColor(QPalette::Base, theme.surface);
    palette.setColor(QPalette::AlternateBase, theme.elevated);
    palette.setColor(QPalette::Text, theme.textPrimary);
    palette.setColor(QPalette::PlaceholderText, theme.textMuted);
    palette.setColor(QPalette::Button, theme.elevated);
    palette.setColor(QPalette::ButtonText, theme.textPrimary);
    palette.setColor(QPalette::Highlight, theme.accent);
    palette.setColor(QPalette::HighlightedText, theme.textOnAccent);
    palette.setColor(QPalette::ToolTipBase, theme.elevated);
    palette.setColor(QPalette::ToolTipText, theme.textPrimary);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, theme.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, theme.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, theme.textMuted);

    app.setPalette(palette);
    app.setFont(theme.uiFont());

    setIconColor(theme.textSecondary);
    clearIconCache();

    app.setStyleSheet(theme.stylesheet());
}

} // namespace arterm::ui
