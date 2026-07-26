#include "terminal/vt_parser.hpp"

namespace arterm::term
{
	namespace
	{

		constexpr std::uint8_t ESC = 0x1B;
		constexpr std::uint8_t CAN = 0x18;
		constexpr std::uint8_t SUB = 0x1A;
		constexpr std::uint8_t BEL = 0x07;
		constexpr std::uint8_t ST  = 0x9C;

		constexpr char32_t REPLACEMENT_CHARACTER = 0xFFFD;

		constexpr bool is_c0( std::uint8_t byte ){
			return byte <= 0x17 || byte == 0x19 || ( byte >= 0x1C && byte <= 0x1F );
		}

		constexpr bool is_intermediate( std::uint8_t byte ){
			return byte >= 0x20 && byte <= 0x2F;
		}

		constexpr bool is_parameter( std::uint8_t byte ){
			return byte >= 0x30 && byte <= 0x3F;
		}

		constexpr bool is_final( std::uint8_t byte ){
			return byte >= 0x40 && byte <= 0x7E;
		}

		/// Escape sequences accept a wider final range than CSI does: `ESC 7` (DECSC),
		/// `ESC =` (DECKPAM) and `ESC ( 0` (line drawing) all end below 0x40.
		constexpr bool is_escape_final( std::uint8_t byte ){
			return byte >= 0x30 && byte <= 0x7E;
		}

	} // namespace

	VtParser::VtParser( VtHandler& handler )
		: _handler( handler )
	{
		_osc_payload.reserve( 256 );
	}

	void VtParser::parse( std::string const& data ){
		parse( std::span<char const>( data.data(), data.size() ) );
	}

	void VtParser::parse( std::span<char const> data ){
		for( char const byte : data )
			advance( static_cast<std::uint8_t>( byte ) );
	}

	void VtParser::reset(){
		_state = State::GROUND;
		clear_sequence();
		_osc_payload.clear();
		_osc_terminator_pending = false;
		_utf8_remaining         = 0;
		_utf8_code_point        = 0;
	}

	void VtParser::clear_sequence(){
		_sequence          = CsiSequence{};
		_parameter_started = false;
	}

	void VtParser::enter( State state ){
		_state = state;
	}

	void VtParser::collect_parameter( std::uint8_t byte ){
		auto& sequence = _sequence;

		if( byte == ';' || byte == ':' ){
			// A separator only ends the current parameter. The slot itself is
			// allocated by the first digit, so incrementing here as well would
			// insert a phantom parameter between every pair of real ones.
			if( !_parameter_started && sequence.parameter_count < CsiSequence::MAX_PARAMETERS ){
				// An omitted parameter: record -1 so the consumer can apply its own
				// default rather than guessing from a zero.
				sequence.parameters[static_cast<std::size_t>( sequence.parameter_count )] = -1;
				++sequence.parameter_count;
			}

			if( byte == ':' && sequence.parameter_count > 0 )
				++sequence.sub_parameter_count[static_cast<std::size_t>( sequence.parameter_count - 1 )];

			_parameter_started = false;
			return;
		}

		if( byte < '0' || byte > '9' )
			return;

		if( sequence.parameter_count >= CsiSequence::MAX_PARAMETERS )
			return;

		auto const slot = static_cast<std::size_t>( sequence.parameter_count );
		if( !_parameter_started ){
			sequence.parameters[slot] = 0;
			++sequence.parameter_count;
			_parameter_started = true;
		}

		auto const current = static_cast<std::size_t>( sequence.parameter_count - 1 );
		int&       value   = sequence.parameters[current];
		if( value < 0 )
			value = 0;

		// Clamp instead of overflowing: a hostile stream can send arbitrarily many
		// digits and the result is only ever used as a row/column/colour.
		if( value < 100'000 )
			value = value * 10 + ( byte - '0' );
	}

	void VtParser::collect_intermediate( std::uint8_t byte ){
		if( _sequence.intermediate == '\0' )
			_sequence.intermediate = static_cast<char>( byte );
	}

	void VtParser::decode_utf8( std::uint8_t byte ){
		if( _utf8_remaining == 0 ){
			if( byte < 0x80 ){
				_handler.print( static_cast<char32_t>( byte ) );
			}
			else if( ( byte & 0xE0 ) == 0xC0 ){
				_utf8_code_point = byte & 0x1Fu;
				_utf8_remaining  = 1;
				_utf8_minimum    = 0x80;
			}
			else if( ( byte & 0xF0 ) == 0xE0 ){
				_utf8_code_point = byte & 0x0Fu;
				_utf8_remaining  = 2;
				_utf8_minimum    = 0x800;
			}
			else if( ( byte & 0xF8 ) == 0xF0 ){
				_utf8_code_point = byte & 0x07u;
				_utf8_remaining  = 3;
				_utf8_minimum    = 0x10000;
			}
			else{
				// Stray continuation byte or an invalid lead byte.
				_handler.print( REPLACEMENT_CHARACTER );
			}
			return;
		}

		if( ( byte & 0xC0 ) != 0x80 ){
			// Truncated sequence: report it and reprocess this byte from scratch.
			_utf8_remaining = 0;
			_handler.print( REPLACEMENT_CHARACTER );
			decode_utf8( byte );
			return;
		}

		_utf8_code_point = ( _utf8_code_point << 6 ) | ( byte & 0x3Fu );
		if( --_utf8_remaining > 0 )
			return;

		char32_t const code_point = _utf8_code_point;
		_utf8_code_point          = 0;

		bool const overlong  = code_point < _utf8_minimum;
		bool const surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
		bool const too_large = code_point > 0x10FFFF;

		_handler.print( ( overlong || surrogate || too_large ) ? REPLACEMENT_CHARACTER : code_point );
	}

	void VtParser::advance( std::uint8_t byte ){
		// These three are handled identically in every state.
		if( byte == ESC ){
			clear_sequence();
			if( _state == State::DCS_PASSTHROUGH )
				_handler.dcs_unhook();
			// "ESC \" is the standard string terminator; keep the payload so the
			// backslash can flush it instead of discarding a valid OSC.
			_osc_terminator_pending = ( _state == State::OSC_STRING );
			if( !_osc_terminator_pending )
				_osc_payload.clear();
			enter( State::ESCAPE );
			return;
		}
		if( byte == CAN || byte == SUB ){
			if( _state == State::DCS_PASSTHROUGH )
				_handler.dcs_unhook();
			if( byte == SUB )
				_handler.print( REPLACEMENT_CHARACTER );
			_osc_terminator_pending = false;
			_osc_payload.clear();
			enter( State::GROUND );
			return;
		}

		if( _osc_terminator_pending ){
			_osc_terminator_pending = false;
			if( byte == '\\' ){
				_handler.osc_dispatch( _osc_payload );
				_osc_payload.clear();
				enter( State::GROUND );
				return;
			}
			// Anything else means the OSC was abandoned mid-sequence.
			_osc_payload.clear();
		}

		switch( _state ){
			case State::GROUND:
				if( is_c0( byte ) )
					_handler.execute( byte );
				else
					decode_utf8( byte );
				return;

			case State::ESCAPE:
				if( is_c0( byte ) ){
					_handler.execute( byte );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
					enter( State::ESCAPE_INTERMEDIATE );
				}
				else if( byte == '[' ){
					clear_sequence();
					enter( State::CSI_ENTRY );
				}
				else if( byte == ']' ){
					_osc_payload.clear();
					enter( State::OSC_STRING );
				}
				else if( byte == 'P' ){
					clear_sequence();
					enter( State::DCS_ENTRY );
				}
				else if( byte == 'X' || byte == '^' || byte == '_' ){
					enter( State::SOS_PM_APC_STRING );
				}
				else if( is_escape_final( byte ) ){
					_handler.esc_dispatch( EscSequence{ '\0', static_cast<char>( byte ) } );
					enter( State::GROUND );
				}
				return;

			case State::ESCAPE_INTERMEDIATE:
				if( is_c0( byte ) ){
					_handler.execute( byte );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
				}
				else if( is_escape_final( byte ) ){
					_handler.esc_dispatch( EscSequence{ _sequence.intermediate, static_cast<char>( byte ) } );
					enter( State::GROUND );
				}
				return;

			case State::CSI_ENTRY:
				if( is_c0( byte ) ){
					_handler.execute( byte );
				}
				else if( byte >= 0x3C && byte <= 0x3F ){
					_sequence.private_marker = static_cast<char>( byte );
					enter( State::CSI_PARAM );
				}
				else if( is_parameter( byte ) ){
					collect_parameter( byte );
					enter( State::CSI_PARAM );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
					enter( State::CSI_INTERMEDIATE );
				}
				else if( is_final( byte ) ){
					_sequence.final = static_cast<char>( byte );
					_handler.csi_dispatch( _sequence );
					enter( State::GROUND );
				}
				else{
					enter( State::CSI_IGNORE );
				}
				return;

			case State::CSI_PARAM:
				if( is_c0( byte ) ){
					_handler.execute( byte );
				}
				else if( is_parameter( byte ) ){
					if( byte >= 0x3C && byte <= 0x3F )
						enter( State::CSI_IGNORE ); // A private marker after parameters is invalid.
					else
						collect_parameter( byte );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
					enter( State::CSI_INTERMEDIATE );
				}
				else if( is_final( byte ) ){
					_sequence.final = static_cast<char>( byte );
					_handler.csi_dispatch( _sequence );
					enter( State::GROUND );
				}
				else{
					enter( State::CSI_IGNORE );
				}
				return;

			case State::CSI_INTERMEDIATE:
				if( is_c0( byte ) ){
					_handler.execute( byte );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
				}
				else if( is_parameter( byte ) ){
					enter( State::CSI_IGNORE );
				}
				else if( is_final( byte ) ){
					_sequence.final = static_cast<char>( byte );
					_handler.csi_dispatch( _sequence );
					enter( State::GROUND );
				}
				return;

			case State::CSI_IGNORE:
				if( is_c0( byte ) )
					_handler.execute( byte );
				else if( is_final( byte ) )
					enter( State::GROUND );
				return;

			case State::DCS_ENTRY:
				if( byte >= 0x3C && byte <= 0x3F ){
					_sequence.private_marker = static_cast<char>( byte );
					enter( State::DCS_PARAM );
				}
				else if( is_parameter( byte ) ){
					collect_parameter( byte );
					enter( State::DCS_PARAM );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
					enter( State::DCS_INTERMEDIATE );
				}
				else if( is_final( byte ) ){
					_sequence.final = static_cast<char>( byte );
					_handler.dcs_hook( _sequence );
					enter( State::DCS_PASSTHROUGH );
				}
				else{
					enter( State::DCS_IGNORE );
				}
				return;

			case State::DCS_PARAM:
				if( is_parameter( byte ) && !( byte >= 0x3C && byte <= 0x3F ) ){
					collect_parameter( byte );
				}
				else if( is_intermediate( byte ) ){
					collect_intermediate( byte );
					enter( State::DCS_INTERMEDIATE );
				}
				else if( is_final( byte ) ){
					_sequence.final = static_cast<char>( byte );
					_handler.dcs_hook( _sequence );
					enter( State::DCS_PASSTHROUGH );
				}
				else{
					enter( State::DCS_IGNORE );
				}
				return;

			case State::DCS_INTERMEDIATE:
				if( is_intermediate( byte ) ){
					collect_intermediate( byte );
				}
				else if( is_final( byte ) ){
					_sequence.final = static_cast<char>( byte );
					_handler.dcs_hook( _sequence );
					enter( State::DCS_PASSTHROUGH );
				}
				else{
					enter( State::DCS_IGNORE );
				}
				return;

			case State::DCS_PASSTHROUGH:
				if( byte == ST ){
					_handler.dcs_unhook();
					enter( State::GROUND );
				}
				else if( byte != 0x7F ){
					_handler.dcs_put( byte );
				}
				return;

			case State::DCS_IGNORE:
			case State::SOS_PM_APC_STRING:
				if( byte == ST )
					enter( State::GROUND );
				return;

			case State::OSC_STRING:
				// OSC ends on BEL (xterm) or ST (the standard 0x9C / "ESC \"). The
				// ESC form is handled above: it re-enters Escape and the following '\'
				// dispatches through esc_dispatch, so flush the payload here as well.
				if( byte == BEL || byte == ST ){
					_handler.osc_dispatch( _osc_payload );
					_osc_payload.clear();
					enter( State::GROUND );
				}
				else if( byte >= 0x20 || byte == 0x09 ){
					if( _osc_payload.size() < 1 << 20 ) // Cap a runaway sequence.
						_osc_payload.push_back( static_cast<char>( byte ) );
				}
				return;
		}
	}

} // namespace arterm::term
