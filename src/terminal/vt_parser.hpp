#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace arterm::term
{

	/// A parsed CSI sequence: `ESC [ <private> <params> <intermediates> <final>`.
	struct CsiSequence
	{
		static constexpr int MAX_PARAMETERS = 32;

		std::array<int, MAX_PARAMETERS> parameters{};
		/// Sub-parameters use ':' as separator (SGR 38:2:...); this records how many
		/// colon-joined values belong to the parameter at the same index.
		std::array<int, MAX_PARAMETERS> sub_parameter_count{};
		int                             parameter_count{ 0 };

		char private_marker{ '\0' }; ///< '?', '>', '<' or '='.
		char intermediate{ '\0' };   ///< ' ', '!', '"', '$', '\'' ...
		char final{ '\0' };

		/// Parameter `index`, or `fallback` when it was omitted or zero-length.
		[[nodiscard]] int parameter( int index, int fallback = 0 ) const{
			if( index < 0 || index >= parameter_count )
				return fallback;
			int const value = parameters[static_cast<std::size_t>( index )];
			return value < 0 ? fallback : value;
		}

		/// Same as `parameter`, but treats an explicit 0 as "use the default", which
		/// is what most cursor-movement sequences want.
		[[nodiscard]] int positive_parameter( int index, int fallback = 1 ) const{
			int const value = parameter( index, fallback );
			return value <= 0 ? fallback : value;
		}
	};

	/// `ESC <intermediates> <final>`.
	struct EscSequence
	{
		char intermediate{ '\0' };
		char final{ '\0' };
	};

	/// Receives the events produced by `VtParser`.
	class VtHandler
	{
	public:
		virtual ~VtHandler() = default;

		/// A printable code point.
		virtual void print( char32_t code_point ) = 0;

		/// A C0 or C1 control character (BEL, BS, HT, LF, CR, ...).
		virtual void execute( std::uint8_t control ) = 0;

		virtual void csi_dispatch( CsiSequence const& sequence ) = 0;
		virtual void esc_dispatch( EscSequence const& sequence ) = 0;

		/// Operating System Command payload, without the introducer or terminator.
		virtual void osc_dispatch( std::string const& payload ) = 0;

		/// Device Control String. The default implementations ignore DCS, which is
		/// correct for everything ArTerm supports today.
		virtual void dcs_hook( CsiSequence const& ) {}
		virtual void dcs_put( std::uint8_t ) {}
		virtual void dcs_unhook() {}
	};

	/// Byte-oriented VT500 parser following the state machine documented by
	/// Paul Williams, extended with UTF-8 decoding in the ground state.
	///
	/// The parser is deliberately free of any screen knowledge: it turns a byte
	/// stream into events and nothing more, which keeps it unit testable without a
	/// GUI.
	class VtParser
	{
	public:
		explicit VtParser( VtHandler& handler );

		void parse( std::span<char const> data );
		void parse( std::string const& data );

		/// Return to the ground state, e.g. after a terminal reset.
		void reset();

	private:
		enum class State : std::uint8_t{
			GROUND,
			ESCAPE,
			ESCAPE_INTERMEDIATE,
			CSI_ENTRY,
			CSI_PARAM,
			CSI_INTERMEDIATE,
			CSI_IGNORE,
			DCS_ENTRY,
			DCS_PARAM,
			DCS_INTERMEDIATE,
			DCS_PASSTHROUGH,
			DCS_IGNORE,
			OSC_STRING,
			SOS_PM_APC_STRING,
		};

		void advance( std::uint8_t byte );
		void enter( State state );

		void clear_sequence();
		void collect_parameter( std::uint8_t byte );
		void collect_intermediate( std::uint8_t byte );

		/// Feeds one byte into the UTF-8 decoder; emits a code point once complete.
		void decode_utf8( std::uint8_t byte );

		VtHandler& _handler;
		State      _state{ State::GROUND };

		CsiSequence _sequence;
		std::string _osc_payload;

		/// Set when an ESC arrived while collecting an OSC, so the following '\'
		/// is recognised as the string terminator.
		bool _osc_terminator_pending{ false };

		/// True while the current parameter has received at least one digit, so
		/// "CSI ;5H" can distinguish an omitted first parameter from a zero.
		bool _parameter_started{ false };

		// UTF-8 decoder state.
		char32_t _utf8_code_point{ 0 };
		int      _utf8_remaining{ 0 };
		char32_t _utf8_minimum{ 0 };
	};

} // namespace arterm::term
