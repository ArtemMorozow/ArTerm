#include "terminal/ColorScheme.hpp"

#include <QStringList>

namespace arterm::term {
namespace {

/// Steps of the 6x6x6 colour cube that occupies palette slots 16-231.
constexpr int kCubeSteps[6] = {0, 95, 135, 175, 215, 255};

} // namespace

ColorScheme::ColorScheme()
{
    // A default-constructed scheme is the dark one; `applyDark` is a plain
    // mutator so this cannot recurse back into a named constructor.
    applyDark(*this);
}

void ColorScheme::fillCube()
{
    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                const int index = 16 + 36 * r + 6 * g + b;
                m_palette[static_cast<std::size_t>(index)] =
                    QColor(kCubeSteps[r], kCubeSteps[g], kCubeSteps[b]);
            }
        }
    }

    // Slots 232-255 are a 24-step greyscale ramp.
    for (int i = 0; i < 24; ++i) {
        const int level = 8 + i * 10;
        m_palette[static_cast<std::size_t>(232 + i)] = QColor(level, level, level);
    }
}

ColorScheme ColorScheme::arTermDark()
{
    ColorScheme scheme;
    applyDark(scheme);
    return scheme;
}

ColorScheme ColorScheme::arTermLight()
{
    ColorScheme scheme;
    applyLight(scheme);
    return scheme;
}

void ColorScheme::applyDark(ColorScheme &scheme)
{
    scheme.m_name = QStringLiteral("ArTerm Dark");

    // Normal colours (0-7), tuned to stay legible on the #12141A background
    // used by the rest of the application chrome.
    scheme.m_palette[0] = QColor(0x1B, 0x1E, 0x26);  // black
    scheme.m_palette[1] = QColor(0xF2, 0x64, 0x6C);  // red
    scheme.m_palette[2] = QColor(0x5C, 0xC9, 0x8A);  // green
    scheme.m_palette[3] = QColor(0xE5, 0xB5, 0x67);  // yellow
    scheme.m_palette[4] = QColor(0x5B, 0xA8, 0xF7);  // blue
    scheme.m_palette[5] = QColor(0xC0, 0x84, 0xF0);  // magenta
    scheme.m_palette[6] = QColor(0x4F, 0xC1, 0xC8);  // cyan
    scheme.m_palette[7] = QColor(0xC5, 0xCA, 0xD6);  // white

    // Bright colours (8-15).
    scheme.m_palette[8] = QColor(0x54, 0x5A, 0x6B);
    scheme.m_palette[9] = QColor(0xFF, 0x83, 0x8A);
    scheme.m_palette[10] = QColor(0x7A, 0xE3, 0xA6);
    scheme.m_palette[11] = QColor(0xFF, 0xCE, 0x7F);
    scheme.m_palette[12] = QColor(0x7F, 0xBE, 0xFF);
    scheme.m_palette[13] = QColor(0xD5, 0xA1, 0xFF);
    scheme.m_palette[14] = QColor(0x6E, 0xDA, 0xE1);
    scheme.m_palette[15] = QColor(0xEE, 0xF1, 0xF7);

    scheme.fillCube();

    scheme.m_background = QColor(0x12, 0x14, 0x1A);
    scheme.m_foreground = QColor(0xC5, 0xCA, 0xD6);
    scheme.m_cursor = QColor(0x5B, 0xA8, 0xF7);
    scheme.m_cursorText = QColor(0x0B, 0x0D, 0x12);
    scheme.m_selection = QColor(0x2C, 0x3A, 0x52);
    scheme.m_selectionText = QColor(0xEE, 0xF1, 0xF7);
}

void ColorScheme::applyLight(ColorScheme &scheme)
{
    scheme.m_name = QStringLiteral("ArTerm Light");

    scheme.m_palette[0] = QColor(0x2B, 0x30, 0x3B);
    scheme.m_palette[1] = QColor(0xC7, 0x35, 0x3F);
    scheme.m_palette[2] = QColor(0x2A, 0x8C, 0x5A);
    scheme.m_palette[3] = QColor(0xA1, 0x71, 0x0F);
    scheme.m_palette[4] = QColor(0x1E, 0x66, 0xC4);
    scheme.m_palette[5] = QColor(0x8B, 0x45, 0xC7);
    scheme.m_palette[6] = QColor(0x11, 0x7D, 0x89);
    scheme.m_palette[7] = QColor(0x5A, 0x61, 0x70);

    scheme.m_palette[8] = QColor(0x8C, 0x93, 0xA3);
    scheme.m_palette[9] = QColor(0xE2, 0x50, 0x5A);
    scheme.m_palette[10] = QColor(0x38, 0xA8, 0x6E);
    scheme.m_palette[11] = QColor(0xC0, 0x8C, 0x22);
    scheme.m_palette[12] = QColor(0x36, 0x81, 0xE0);
    scheme.m_palette[13] = QColor(0xA6, 0x5F, 0xE0);
    scheme.m_palette[14] = QColor(0x1E, 0x99, 0xA6);
    scheme.m_palette[15] = QColor(0x2B, 0x30, 0x3B);

    scheme.fillCube();

    scheme.m_background = QColor(0xFB, 0xFC, 0xFE);
    scheme.m_foreground = QColor(0x2B, 0x30, 0x3B);
    scheme.m_cursor = QColor(0x1E, 0x66, 0xC4);
    scheme.m_cursorText = QColor(0xFF, 0xFF, 0xFF);
    scheme.m_selection = QColor(0xCF, 0xE1, 0xFB);
    scheme.m_selectionText = QColor(0x18, 0x1B, 0x22);
}

QStringList ColorScheme::availableSchemes()
{
    return {QStringLiteral("ArTerm Dark"), QStringLiteral("ArTerm Light")};
}

ColorScheme ColorScheme::byName(const QString &name)
{
    if (name.compare(QLatin1String("ArTerm Light"), Qt::CaseInsensitive) == 0)
        return arTermLight();
    return arTermDark();
}

QColor ColorScheme::resolve(const Color &color, bool isForeground, bool bold) const
{
    switch (color.kind()) {
    case Color::Kind::Default:
        return isForeground ? m_foreground : m_background;

    case Color::Kind::Rgb:
        return QColor(color.red(), color.green(), color.blue());

    case Color::Kind::Indexed: {
        std::uint8_t index = color.index();
        // Bold text picks the bright variant of the eight base colours, which
        // is what applications assume when they emit "SGR 1;31".
        if (bold && isForeground && index < 8)
            index = static_cast<std::uint8_t>(index + 8);
        return m_palette[index];
    }
    }

    return isForeground ? m_foreground : m_background;
}

} // namespace arterm::term
