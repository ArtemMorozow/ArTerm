#include "ui/Icons.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace arterm::ui {
namespace {

QColor g_iconColor{0x9A, 0xA3, 0xB2};
QHash<QString, QIcon> g_cache;

constexpr int kRenderSize = 64; ///< Rendered once at 64px and scaled down.

QIcon renderIcon(const QString &name, const QColor &color)
{
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QSvgRenderer renderer(file.readAll());
    if (!renderer.isValid())
        return {};

    QPixmap pixmap(kRenderSize, kRenderSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter);

    // Tint by compositing the colour through the rendered alpha mask, which
    // keeps a single set of source SVGs usable in every theme.
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();

    return QIcon(pixmap);
}

/// Maps a file extension onto one of the bundled type icons.
QString iconNameForExtension(const QString &suffix)
{
    static const QHash<QString, QString> mapping = {
        {QStringLiteral("cpp"), QStringLiteral("file-code")},
        {QStringLiteral("hpp"), QStringLiteral("file-code")},
        {QStringLiteral("h"), QStringLiteral("file-code")},
        {QStringLiteral("c"), QStringLiteral("file-code")},
        {QStringLiteral("py"), QStringLiteral("file-code")},
        {QStringLiteral("js"), QStringLiteral("file-code")},
        {QStringLiteral("ts"), QStringLiteral("file-code")},
        {QStringLiteral("go"), QStringLiteral("file-code")},
        {QStringLiteral("rs"), QStringLiteral("file-code")},
        {QStringLiteral("rb"), QStringLiteral("file-code")},
        {QStringLiteral("sh"), QStringLiteral("file-terminal")},
        {QStringLiteral("bash"), QStringLiteral("file-terminal")},
        {QStringLiteral("zsh"), QStringLiteral("file-terminal")},
        {QStringLiteral("json"), QStringLiteral("file-config")},
        {QStringLiteral("yaml"), QStringLiteral("file-config")},
        {QStringLiteral("yml"), QStringLiteral("file-config")},
        {QStringLiteral("toml"), QStringLiteral("file-config")},
        {QStringLiteral("ini"), QStringLiteral("file-config")},
        {QStringLiteral("conf"), QStringLiteral("file-config")},
        {QStringLiteral("png"), QStringLiteral("file-image")},
        {QStringLiteral("jpg"), QStringLiteral("file-image")},
        {QStringLiteral("jpeg"), QStringLiteral("file-image")},
        {QStringLiteral("gif"), QStringLiteral("file-image")},
        {QStringLiteral("svg"), QStringLiteral("file-image")},
        {QStringLiteral("webp"), QStringLiteral("file-image")},
        {QStringLiteral("zip"), QStringLiteral("file-archive")},
        {QStringLiteral("gz"), QStringLiteral("file-archive")},
        {QStringLiteral("tar"), QStringLiteral("file-archive")},
        {QStringLiteral("bz2"), QStringLiteral("file-archive")},
        {QStringLiteral("xz"), QStringLiteral("file-archive")},
        {QStringLiteral("7z"), QStringLiteral("file-archive")},
        {QStringLiteral("log"), QStringLiteral("file-text")},
        {QStringLiteral("txt"), QStringLiteral("file-text")},
        {QStringLiteral("md"), QStringLiteral("file-text")},
    };

    return mapping.value(suffix.toLower(), QStringLiteral("file"));
}

} // namespace

void setIconColor(const QColor &color)
{
    if (g_iconColor == color)
        return;
    g_iconColor = color;
    g_cache.clear();
}

QColor iconColor()
{
    return g_iconColor;
}

void clearIconCache()
{
    g_cache.clear();
}

QIcon icon(const QString &name)
{
    return icon(name, g_iconColor);
}

QIcon icon(const QString &name, const QColor &color)
{
    const QString key = name + QLatin1Char('#') + color.name(QColor::HexArgb);

    const auto cached = g_cache.constFind(key);
    if (cached != g_cache.constEnd())
        return *cached;

    QIcon rendered = renderIcon(name, color);
    g_cache.insert(key, rendered);
    return rendered;
}

QIcon fileIcon(const QString &fileName, bool isDirectory, bool isSymlink)
{
    if (isDirectory)
        return icon(isSymlink ? QStringLiteral("folder-link") : QStringLiteral("folder"));

    const QFileInfo info(fileName);
    return icon(iconNameForExtension(info.suffix()));
}

} // namespace arterm::ui
