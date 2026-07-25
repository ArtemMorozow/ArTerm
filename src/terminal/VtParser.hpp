#pragma once

#include <QByteArray>

#include <array>
#include <cstdint>
#include <span>

namespace arterm::term {

/// A parsed CSI sequence: `ESC [ <private> <params> <intermediates> <final>`.
struct CsiSequence {
    static constexpr int kMaxParameters = 32;

    std::array<int, kMaxParameters> parameters{};
    /// Sub-parameters use ':' as separator (SGR 38:2:...); this records how many
    /// colon-joined values belong to the parameter at the same index.
    std::array<int, kMaxParameters> subParameterCount{};
    int parameterCount{0};

    char privateMarker{'\0'}; ///< '?', '>', '<' or '='.
    char intermediate{'\0'};  ///< ' ', '!', '"', '$', '\'' ...
    char final{'\0'};

    /// Parameter `index`, or `fallback` when it was omitted or zero-length.
    [[nodiscard]] int parameter(int index, int fallback = 0) const
    {
        if (index < 0 || index >= parameterCount)
            return fallback;
        const int value = parameters[static_cast<std::size_t>(index)];
        return value < 0 ? fallback : value;
    }

    /// Same as `parameter`, but treats an explicit 0 as "use the default", which
    /// is what most cursor-movement sequences want.
    [[nodiscard]] int positiveParameter(int index, int fallback = 1) const
    {
        const int value = parameter(index, fallback);
        return value <= 0 ? fallback : value;
    }
};

/// `ESC <intermediates> <final>`.
struct EscSequence {
    char intermediate{'\0'};
    char final{'\0'};
};

/// Receives the events produced by `VtParser`.
class VtHandler {
public:
    virtual ~VtHandler() = default;

    /// A printable code point.
    virtual void print(char32_t codePoint) = 0;

    /// A C0 or C1 control character (BEL, BS, HT, LF, CR, ...).
    virtual void execute(std::uint8_t control) = 0;

    virtual void csiDispatch(const CsiSequence &sequence) = 0;
    virtual void escDispatch(const EscSequence &sequence) = 0;

    /// Operating System Command payload, without the introducer or terminator.
    virtual void oscDispatch(const QByteArray &payload) = 0;

    /// Device Control String. The default implementations ignore DCS, which is
    /// correct for everything ArTerm supports today.
    virtual void dcsHook(const CsiSequence &) {}
    virtual void dcsPut(std::uint8_t) {}
    virtual void dcsUnhook() {}
};

/// Byte-oriented VT500 parser following the state machine documented by
/// Paul Williams, extended with UTF-8 decoding in the ground state.
///
/// The parser is deliberately free of any screen knowledge: it turns a byte
/// stream into events and nothing more, which keeps it unit testable without a
/// GUI.
class VtParser {
public:
    explicit VtParser(VtHandler &handler);

    void parse(std::span<const char> data);
    void parse(const QByteArray &data);

    /// Return to the ground state, e.g. after a terminal reset.
    void reset();

private:
    enum class State : std::uint8_t {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiEntry,
        CsiParam,
        CsiIntermediate,
        CsiIgnore,
        DcsEntry,
        DcsParam,
        DcsIntermediate,
        DcsPassthrough,
        DcsIgnore,
        OscString,
        SosPmApcString,
    };

    void advance(std::uint8_t byte);
    void enter(State state);

    void clearSequence();
    void collectParameter(std::uint8_t byte);
    void collectIntermediate(std::uint8_t byte);

    /// Feeds one byte into the UTF-8 decoder; emits a code point once complete.
    void decodeUtf8(std::uint8_t byte);

    VtHandler &m_handler;
    State m_state{State::Ground};

    CsiSequence m_sequence;
    QByteArray m_oscPayload;

    /// Set when an ESC arrived while collecting an OSC, so the following '\'
    /// is recognised as the string terminator.
    bool m_oscTerminatorPending{false};

    /// True while the current parameter has received at least one digit, so
    /// "CSI ;5H" can distinguish an omitted first parameter from a zero.
    bool m_parameterStarted{false};

    // UTF-8 decoder state.
    char32_t m_utf8CodePoint{0};
    int m_utf8Remaining{0};
    char32_t m_utf8Minimum{0};
};

} // namespace arterm::term
