#include "terminal/key_encoder.hpp"

#include <algorithm>
#include <format>
#include <string>

namespace arterm::term
{
	namespace
	{

		constexpr char ESC = '\033';

		constexpr bool has_control( KeyModifier modifiers ){
			return has_modifier( modifiers, KeyModifier::CONTROL );
		}

		constexpr bool has_meta_alt( KeyModifier modifiers, KeyEncoder::Options const& options ){
			return has_modifier( modifiers, KeyModifier::ALT ) && options.option_is_meta;
		}

	} // namespace

	int KeyEncoder::modifier_parameter( KeyModifier modifiers ){
		int value = 0;
		if( has_modifier( modifiers, KeyModifier::SHIFT ) )
			value |= 1;
		if( has_modifier( modifiers, KeyModifier::ALT ) )
			value |= 2;
		if( has_control( modifiers ) )
			value |= 4;
		if( has_modifier( modifiers, KeyModifier::COMMAND ) )
			value |= 8;
		return value == 0 ? 0 : value + 1;
	}

	std::string KeyEncoder::tilde_sequence( int number, int modifier ){
		if( modifier == 0 )
			return std::format( "\033[{}~", number );
		return std::format( "\033[{};{}~", number, modifier );
	}

	std::string KeyEncoder::cursor_sequence( char final, int modifier, bool application_mode ){
		if( modifier != 0 ){
			// The modified form is always CSI, even in application mode.
			return std::format( "\033[1;{}{}", modifier, final );
		}
		return std::string{ ESC } + ( application_mode ? 'O' : '[' ) + final;
	}

	std::string KeyEncoder::encode( KeyEvent const& event, Options const& options ){
		KeyModifier const modifiers = event.modifiers;
		int const         modifier  = modifier_parameter( modifiers );

		// Cmd is reserved for menu shortcuts unless it is part of a larger chord.
		if( has_modifier( modifiers, KeyModifier::COMMAND ) && !has_modifier( modifiers, KeyModifier::CONTROL ) &&
			!has_modifier( modifiers, KeyModifier::ALT ) ){
			return {};
		}

		switch( event.key ){
			case Key::UP:
				return cursor_sequence( 'A', modifier, options.application_cursor_keys );
			case Key::DOWN:
				return cursor_sequence( 'B', modifier, options.application_cursor_keys );
			case Key::RIGHT:
				return cursor_sequence( 'C', modifier, options.application_cursor_keys );
			case Key::LEFT:
				return cursor_sequence( 'D', modifier, options.application_cursor_keys );
			case Key::HOME:
				return cursor_sequence( 'H', modifier, options.application_cursor_keys );
			case Key::END:
				return cursor_sequence( 'F', modifier, options.application_cursor_keys );

			case Key::INSERT:
				return tilde_sequence( 2, modifier );
			case Key::DELETE_FORWARD:
				return tilde_sequence( 3, modifier );
			case Key::PAGE_UP:
				return tilde_sequence( 5, modifier );
			case Key::PAGE_DOWN:
				return tilde_sequence( 6, modifier );

			case Key::F1:
				return modifier == 0 ? std::string( "\033OP" ) : cursor_sequence( 'P', modifier, true );
			case Key::F2:
				return modifier == 0 ? std::string( "\033OQ" ) : cursor_sequence( 'Q', modifier, true );
			case Key::F3:
				return modifier == 0 ? std::string( "\033OR" ) : cursor_sequence( 'R', modifier, true );
			case Key::F4:
				return modifier == 0 ? std::string( "\033OS" ) : cursor_sequence( 'S', modifier, true );
			case Key::F5:
				return tilde_sequence( 15, modifier );
			case Key::F6:
				return tilde_sequence( 17, modifier );
			case Key::F7:
				return tilde_sequence( 18, modifier );
			case Key::F8:
				return tilde_sequence( 19, modifier );
			case Key::F9:
				return tilde_sequence( 20, modifier );
			case Key::F10:
				return tilde_sequence( 21, modifier );
			case Key::F11:
				return tilde_sequence( 23, modifier );
			case Key::F12:
				return tilde_sequence( 24, modifier );

			case Key::RETURN:
			case Key::ENTER:
				return options.new_line_mode ? std::string( "\r\n" ) : std::string( "\r" );

			case Key::BACKSPACE:{
				std::string result;
				if( has_meta_alt( modifiers, options ) )
					result.push_back( ESC );
				// Ctrl inverts the choice, which is the convention every terminal uses
				// to let the user reach whichever byte the host actually wants.
				bool const send_delete = options.backspace_sends_delete != has_control( modifiers );
				result.push_back( send_delete ? '\x7F' : '\x08' );
				return result;
			}

			case Key::TAB:
				return "\t";
			case Key::BACKTAB:
				return "\033[Z";

			case Key::ESCAPE:
				return std::string{ ESC };

			case Key::MODIFIER:
				return {};

			case Key::CHARACTER:
				break;
		}

		if( has_control( modifiers ) ){
			char32_t const base = event.base_character;

			// Map Ctrl+<letter> onto the matching C0 control code.
			if( base >= U'a' && base <= U'z' ){
				std::string result;
				if( has_meta_alt( modifiers, options ) )
					result.push_back( ESC );
				result.push_back( static_cast<char>( base - U'a' + 1 ) );
				return result;
			}

			switch( base ){
				case U' ':
				case U'2':
				case U'@':
					return std::string( 1, '\0' );
				case U'[':
					return "\x1B";
				case U'\\':
					return "\x1C";
				case U']':
					return "\x1D";
				case U'^':
				case U'6':
					return "\x1E";
				case U'_':
				case U'-':
					return "\x1F";
				case U'?':
					return "\x7F";
				default:
					break;
			}
		}

		if( event.text.empty() )
			return {};

		std::string result;

		// Meta/Alt prefixes the sequence with ESC, which is how bash's M-x bindings
		// and vim's Alt mappings are reached.
		if( has_meta_alt( modifiers, options ) )
			result.push_back( ESC );

		result.append( event.text );
		return result;
	}

	std::string KeyEncoder::encode_paste( std::string_view text, bool bracketed ){
		// Normalise line endings; a stray CRLF makes shells see a blank command.
		// Everything here is byte-wise, which is safe on UTF-8: continuation bytes
		// are all >= 0x80 and can never be mistaken for one of these ASCII ones.
		std::string sanitised;
		sanitised.reserve( text.size() );
		for( std::size_t i = 0; i < text.size(); ++i ){
			if( text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n' )
				continue;
			sanitised.push_back( text[i] == '\n' ? '\r' : text[i] );
		}

		if( bracketed ){
			// The guard sequence itself must not appear inside the payload,
			// otherwise a crafted clipboard could end the paste early and have the
			// rest executed as typed input.
			constexpr std::string_view GUARD = "\033[201~";
			for( std::size_t at = sanitised.find( GUARD ); at != std::string::npos; at = sanitised.find( GUARD, at ) )
				sanitised.erase( at, GUARD.size() );

			return "\033[200~" + sanitised + "\033[201~";
		}

		// Without bracketed paste the shell cannot tell pasted text from typing, so
		// drop the control characters that would run something unintended.
		std::erase_if( sanitised, []( char character ){
			auto const byte = static_cast<unsigned char>( character );
			return byte < 0x20 && character != '\r' && character != '\t';
		} );

		return sanitised;
	}

} // namespace arterm::term
