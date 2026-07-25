#pragma once

#include <cstdint>

namespace arterm::term {

/// A colour as it appears in an SGR sequence: either "whatever the theme says",
/// one of the 256 palette slots, or a literal RGB triple.
class Color {
public:
    enum class Kind : std::uint8_t { Default, Indexed, Rgb };

    constexpr Color() = default;

    static constexpr Color defaultColor() { return Color{}; }

    static constexpr Color indexed(std::uint8_t index)
    {
        Color color;
        color.m_kind = Kind::Indexed;
        color.m_index = index;
        return color;
    }

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        Color color;
        color.m_kind = Kind::Rgb;
        color.m_r = r;
        color.m_g = g;
        color.m_b = b;
        return color;
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return m_kind; }
    [[nodiscard]] constexpr std::uint8_t index() const noexcept { return m_index; }
    [[nodiscard]] constexpr std::uint8_t red() const noexcept { return m_r; }
    [[nodiscard]] constexpr std::uint8_t green() const noexcept { return m_g; }
    [[nodiscard]] constexpr std::uint8_t blue() const noexcept { return m_b; }
    [[nodiscard]] constexpr bool isDefault() const noexcept { return m_kind == Kind::Default; }

    friend constexpr bool operator==(const Color &, const Color &) = default;

private:
    Kind m_kind{Kind::Default};
    std::uint8_t m_index{0};
    std::uint8_t m_r{0};
    std::uint8_t m_g{0};
    std::uint8_t m_b{0};
};

/// Rendition bits carried by every cell.
enum class CellFlag : std::uint16_t {
    None = 0,
    Bold = 1u << 0,
    Faint = 1u << 1,
    Italic = 1u << 2,
    Underline = 1u << 3,
    Blink = 1u << 4,
    Inverse = 1u << 5,
    Hidden = 1u << 6,
    Strikeout = 1u << 7,
    DoubleUnderline = 1u << 8,
    /// First column of a double-width glyph (CJK, some emoji).
    WideLead = 1u << 9,
    /// The placeholder column that follows a `WideLead` cell.
    WideTrail = 1u << 10,
};

constexpr CellFlag operator|(CellFlag a, CellFlag b)
{
    return static_cast<CellFlag>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

constexpr CellFlag operator&(CellFlag a, CellFlag b)
{
    return static_cast<CellFlag>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

constexpr CellFlag operator~(CellFlag a)
{
    return static_cast<CellFlag>(~static_cast<std::uint16_t>(a));
}

constexpr CellFlag &operator|=(CellFlag &a, CellFlag b)
{
    a = a | b;
    return a;
}

constexpr CellFlag &operator&=(CellFlag &a, CellFlag b)
{
    a = a & b;
    return a;
}

constexpr bool hasFlag(CellFlag value, CellFlag flag)
{
    return (static_cast<std::uint16_t>(value) & static_cast<std::uint16_t>(flag)) != 0;
}

/// The graphic rendition currently in effect, i.e. the state SGR mutates.
struct Attributes {
    Color foreground;
    Color background;
    Color underlineColor;
    CellFlag flags{CellFlag::None};

    friend constexpr bool operator==(const Attributes &, const Attributes &) = default;

    /// SGR 0.
    constexpr void reset() { *this = Attributes{}; }
};

/// One character cell of the screen grid.
struct Cell {
    char32_t character{U' '};
    Attributes attributes;

    [[nodiscard]] constexpr bool isBlank() const
    {
        return character == U' ' || character == U'\0';
    }

    /// True when the cell holds nothing that needs painting beyond its
    /// background - lets the renderer skip glyph shaping entirely.
    [[nodiscard]] constexpr bool isEmpty() const
    {
        return isBlank() && !hasFlag(attributes.flags, CellFlag::Underline)
               && !hasFlag(attributes.flags, CellFlag::Strikeout)
               && !hasFlag(attributes.flags, CellFlag::DoubleUnderline);
    }

    friend constexpr bool operator==(const Cell &, const Cell &) = default;
};

} // namespace arterm::term
