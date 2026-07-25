#include "terminal/VtParser.hpp"

namespace arterm::term {
namespace {

constexpr std::uint8_t kEsc = 0x1B;
constexpr std::uint8_t kCan = 0x18;
constexpr std::uint8_t kSub = 0x1A;
constexpr std::uint8_t kBel = 0x07;
constexpr std::uint8_t kSt = 0x9C;

constexpr char32_t kReplacementCharacter = 0xFFFD;

constexpr bool isC0(std::uint8_t byte)
{
    return byte <= 0x17 || byte == 0x19 || (byte >= 0x1C && byte <= 0x1F);
}

constexpr bool isIntermediate(std::uint8_t byte)
{
    return byte >= 0x20 && byte <= 0x2F;
}

constexpr bool isParameter(std::uint8_t byte)
{
    return byte >= 0x30 && byte <= 0x3F;
}

constexpr bool isFinal(std::uint8_t byte)
{
    return byte >= 0x40 && byte <= 0x7E;
}

/// Escape sequences accept a wider final range than CSI does: `ESC 7` (DECSC),
/// `ESC =` (DECKPAM) and `ESC ( 0` (line drawing) all end below 0x40.
constexpr bool isEscapeFinal(std::uint8_t byte)
{
    return byte >= 0x30 && byte <= 0x7E;
}

} // namespace

VtParser::VtParser(VtHandler &handler)
    : m_handler(handler)
{
    m_oscPayload.reserve(256);
}

void VtParser::parse(const QByteArray &data)
{
    parse(std::span<const char>(data.constData(), static_cast<std::size_t>(data.size())));
}

void VtParser::parse(std::span<const char> data)
{
    for (const char byte : data)
        advance(static_cast<std::uint8_t>(byte));
}

void VtParser::reset()
{
    m_state = State::Ground;
    clearSequence();
    m_oscPayload.clear();
    m_oscTerminatorPending = false;
    m_utf8Remaining = 0;
    m_utf8CodePoint = 0;
}

void VtParser::clearSequence()
{
    m_sequence = CsiSequence{};
    m_parameterStarted = false;
}

void VtParser::enter(State state)
{
    m_state = state;
}

void VtParser::collectParameter(std::uint8_t byte)
{
    auto &sequence = m_sequence;

    if (byte == ';' || byte == ':') {
        // A separator only ends the current parameter. The slot itself is
        // allocated by the first digit, so incrementing here as well would
        // insert a phantom parameter between every pair of real ones.
        if (!m_parameterStarted && sequence.parameterCount < CsiSequence::kMaxParameters) {
            // An omitted parameter: record -1 so the consumer can apply its own
            // default rather than guessing from a zero.
            sequence.parameters[static_cast<std::size_t>(sequence.parameterCount)] = -1;
            ++sequence.parameterCount;
        }

        if (byte == ':' && sequence.parameterCount > 0)
            ++sequence.subParameterCount[static_cast<std::size_t>(sequence.parameterCount - 1)];

        m_parameterStarted = false;
        return;
    }

    if (byte < '0' || byte > '9')
        return;

    if (sequence.parameterCount >= CsiSequence::kMaxParameters)
        return;

    const auto slot = static_cast<std::size_t>(sequence.parameterCount);
    if (!m_parameterStarted) {
        sequence.parameters[slot] = 0;
        ++sequence.parameterCount;
        m_parameterStarted = true;
    }

    const auto current = static_cast<std::size_t>(sequence.parameterCount - 1);
    int &value = sequence.parameters[current];
    if (value < 0)
        value = 0;

    // Clamp instead of overflowing: a hostile stream can send arbitrarily many
    // digits and the result is only ever used as a row/column/colour.
    if (value < 100'000)
        value = value * 10 + (byte - '0');
}

void VtParser::collectIntermediate(std::uint8_t byte)
{
    if (m_sequence.intermediate == '\0')
        m_sequence.intermediate = static_cast<char>(byte);
}

void VtParser::decodeUtf8(std::uint8_t byte)
{
    if (m_utf8Remaining == 0) {
        if (byte < 0x80) {
            m_handler.print(static_cast<char32_t>(byte));
        } else if ((byte & 0xE0) == 0xC0) {
            m_utf8CodePoint = byte & 0x1Fu;
            m_utf8Remaining = 1;
            m_utf8Minimum = 0x80;
        } else if ((byte & 0xF0) == 0xE0) {
            m_utf8CodePoint = byte & 0x0Fu;
            m_utf8Remaining = 2;
            m_utf8Minimum = 0x800;
        } else if ((byte & 0xF8) == 0xF0) {
            m_utf8CodePoint = byte & 0x07u;
            m_utf8Remaining = 3;
            m_utf8Minimum = 0x10000;
        } else {
            // Stray continuation byte or an invalid lead byte.
            m_handler.print(kReplacementCharacter);
        }
        return;
    }

    if ((byte & 0xC0) != 0x80) {
        // Truncated sequence: report it and reprocess this byte from scratch.
        m_utf8Remaining = 0;
        m_handler.print(kReplacementCharacter);
        decodeUtf8(byte);
        return;
    }

    m_utf8CodePoint = (m_utf8CodePoint << 6) | (byte & 0x3Fu);
    if (--m_utf8Remaining > 0)
        return;

    const char32_t codePoint = m_utf8CodePoint;
    m_utf8CodePoint = 0;

    const bool overlong = codePoint < m_utf8Minimum;
    const bool surrogate = codePoint >= 0xD800 && codePoint <= 0xDFFF;
    const bool tooLarge = codePoint > 0x10FFFF;

    m_handler.print((overlong || surrogate || tooLarge) ? kReplacementCharacter : codePoint);
}

void VtParser::advance(std::uint8_t byte)
{
    // These three are handled identically in every state.
    if (byte == kEsc) {
        clearSequence();
        if (m_state == State::DcsPassthrough)
            m_handler.dcsUnhook();
        // "ESC \" is the standard string terminator; keep the payload so the
        // backslash can flush it instead of discarding a valid OSC.
        m_oscTerminatorPending = (m_state == State::OscString);
        if (!m_oscTerminatorPending)
            m_oscPayload.clear();
        enter(State::Escape);
        return;
    }
    if (byte == kCan || byte == kSub) {
        if (m_state == State::DcsPassthrough)
            m_handler.dcsUnhook();
        if (byte == kSub)
            m_handler.print(kReplacementCharacter);
        m_oscTerminatorPending = false;
        m_oscPayload.clear();
        enter(State::Ground);
        return;
    }

    if (m_oscTerminatorPending) {
        m_oscTerminatorPending = false;
        if (byte == '\\') {
            m_handler.oscDispatch(m_oscPayload);
            m_oscPayload.clear();
            enter(State::Ground);
            return;
        }
        // Anything else means the OSC was abandoned mid-sequence.
        m_oscPayload.clear();
    }

    switch (m_state) {
    case State::Ground:
        if (isC0(byte))
            m_handler.execute(byte);
        else
            decodeUtf8(byte);
        return;

    case State::Escape:
        if (isC0(byte)) {
            m_handler.execute(byte);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
            enter(State::EscapeIntermediate);
        } else if (byte == '[') {
            clearSequence();
            enter(State::CsiEntry);
        } else if (byte == ']') {
            m_oscPayload.clear();
            enter(State::OscString);
        } else if (byte == 'P') {
            clearSequence();
            enter(State::DcsEntry);
        } else if (byte == 'X' || byte == '^' || byte == '_') {
            enter(State::SosPmApcString);
        } else if (isEscapeFinal(byte)) {
            m_handler.escDispatch(EscSequence{'\0', static_cast<char>(byte)});
            enter(State::Ground);
        }
        return;

    case State::EscapeIntermediate:
        if (isC0(byte)) {
            m_handler.execute(byte);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
        } else if (isEscapeFinal(byte)) {
            m_handler.escDispatch(EscSequence{m_sequence.intermediate, static_cast<char>(byte)});
            enter(State::Ground);
        }
        return;

    case State::CsiEntry:
        if (isC0(byte)) {
            m_handler.execute(byte);
        } else if (byte >= 0x3C && byte <= 0x3F) {
            m_sequence.privateMarker = static_cast<char>(byte);
            enter(State::CsiParam);
        } else if (isParameter(byte)) {
            collectParameter(byte);
            enter(State::CsiParam);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
            enter(State::CsiIntermediate);
        } else if (isFinal(byte)) {
            m_sequence.final = static_cast<char>(byte);
            m_handler.csiDispatch(m_sequence);
            enter(State::Ground);
        } else {
            enter(State::CsiIgnore);
        }
        return;

    case State::CsiParam:
        if (isC0(byte)) {
            m_handler.execute(byte);
        } else if (isParameter(byte)) {
            if (byte >= 0x3C && byte <= 0x3F)
                enter(State::CsiIgnore); // A private marker after parameters is invalid.
            else
                collectParameter(byte);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
            enter(State::CsiIntermediate);
        } else if (isFinal(byte)) {
            m_sequence.final = static_cast<char>(byte);
            m_handler.csiDispatch(m_sequence);
            enter(State::Ground);
        } else {
            enter(State::CsiIgnore);
        }
        return;

    case State::CsiIntermediate:
        if (isC0(byte)) {
            m_handler.execute(byte);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
        } else if (isParameter(byte)) {
            enter(State::CsiIgnore);
        } else if (isFinal(byte)) {
            m_sequence.final = static_cast<char>(byte);
            m_handler.csiDispatch(m_sequence);
            enter(State::Ground);
        }
        return;

    case State::CsiIgnore:
        if (isC0(byte))
            m_handler.execute(byte);
        else if (isFinal(byte))
            enter(State::Ground);
        return;

    case State::DcsEntry:
        if (byte >= 0x3C && byte <= 0x3F) {
            m_sequence.privateMarker = static_cast<char>(byte);
            enter(State::DcsParam);
        } else if (isParameter(byte)) {
            collectParameter(byte);
            enter(State::DcsParam);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
            enter(State::DcsIntermediate);
        } else if (isFinal(byte)) {
            m_sequence.final = static_cast<char>(byte);
            m_handler.dcsHook(m_sequence);
            enter(State::DcsPassthrough);
        } else {
            enter(State::DcsIgnore);
        }
        return;

    case State::DcsParam:
        if (isParameter(byte) && !(byte >= 0x3C && byte <= 0x3F)) {
            collectParameter(byte);
        } else if (isIntermediate(byte)) {
            collectIntermediate(byte);
            enter(State::DcsIntermediate);
        } else if (isFinal(byte)) {
            m_sequence.final = static_cast<char>(byte);
            m_handler.dcsHook(m_sequence);
            enter(State::DcsPassthrough);
        } else {
            enter(State::DcsIgnore);
        }
        return;

    case State::DcsIntermediate:
        if (isIntermediate(byte)) {
            collectIntermediate(byte);
        } else if (isFinal(byte)) {
            m_sequence.final = static_cast<char>(byte);
            m_handler.dcsHook(m_sequence);
            enter(State::DcsPassthrough);
        } else {
            enter(State::DcsIgnore);
        }
        return;

    case State::DcsPassthrough:
        if (byte == kSt) {
            m_handler.dcsUnhook();
            enter(State::Ground);
        } else if (byte != 0x7F) {
            m_handler.dcsPut(byte);
        }
        return;

    case State::DcsIgnore:
    case State::SosPmApcString:
        if (byte == kSt)
            enter(State::Ground);
        return;

    case State::OscString:
        // OSC ends on BEL (xterm) or ST (the standard 0x9C / "ESC \"). The
        // ESC form is handled above: it re-enters Escape and the following '\'
        // dispatches through escDispatch, so flush the payload here as well.
        if (byte == kBel || byte == kSt) {
            m_handler.oscDispatch(m_oscPayload);
            m_oscPayload.clear();
            enter(State::Ground);
        } else if (byte >= 0x20 || byte == 0x09) {
            if (m_oscPayload.size() < 1 << 20) // Cap a runaway sequence.
                m_oscPayload.append(static_cast<char>(byte));
        }
        return;
    }
}

} // namespace arterm::term
