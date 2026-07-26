#include "terminal/terminal.hpp"

#include "core/base64.hpp"
#include "terminal/char_width.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace arterm::term
{
	namespace
	{

		/// DEC Special Graphics: maps ASCII 0x5F-0x7E onto the box-drawing glyphs that
		/// ncurses relies on for borders.
		constexpr char32_t DEC_SPECIAL_GRAPHICS[] = {
			0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B,
			0x2518, 0x2510, 0x250C, 0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C,
			0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
		};

		constexpr Screen::EraseMode erase_mode_for( int parameter ){
			switch( parameter ){
				case 1:
					return Screen::EraseMode::TO_START;
				case 2:
				case 3:
					return Screen::EraseMode::ALL;
				default:
					return Screen::EraseMode::TO_END;
			}
		}

	} // namespace

	Terminal::Terminal( int columns, int rows )
		: _columns( std::max( 1, columns ) )
		, _rows( std::max( 1, rows ) )
		, _parser( *this )
	{
		_normal    = std::make_unique<Screen>( _columns, _rows, _scrollback_limit );
		_alternate = std::make_unique<Screen>( _columns, _rows, 0 );
		_active    = _normal.get();
	}

	Terminal::~Terminal() = default;

	void Terminal::receive( std::string_view data ){
		if( data.empty() )
			return;

		_dirty = false;
		_parser.parse( data );

		// One repaint per chunk instead of one per glyph: a `cat` of a large file
		// would otherwise spend all its time in the renderer.
		screen_changed();
	}

	void Terminal::resize( int columns, int rows ){
		columns = std::max( 1, columns );
		rows    = std::max( 1, rows );
		if( columns == _columns && rows == _rows )
			return;

		_columns = columns;
		_rows    = rows;
		_normal->resize( columns, rows );
		_alternate->resize( columns, rows );

		screen_changed();
	}

	void Terminal::set_scrollback_limit( int lines ){
		_scrollback_limit = std::max( 0, lines );
		_normal->set_scrollback_limit( _scrollback_limit );
		screen_changed();
	}

	void Terminal::reset(){
		_attributes.reset();
		_modes                  = TerminalModes{};
		_charsets               = { 0, 0 };
		_active_charset         = 0;
		_saved_cursor           = CursorState{};
		_saved_alternate_cursor = CursorState{};

		_parser.reset();
		_normal->reset( _attributes );
		_normal->clear_scrollback();
		_alternate->reset( _attributes );
		_active = _normal.get();

		_title.clear();

		alternate_screen_changed( false );
		mouse_tracking_changed( MouseTracking::OFF );
		bracketed_paste_changed( false );
		screen_changed();
	}

	void Terminal::soft_reset(){
		_attributes.reset();
		_modes.origin_mode    = false;
		_modes.insert_mode    = false;
		_modes.auto_wrap      = true;
		_modes.cursor_visible = true;
		_charsets             = { 0, 0 };
		_active_charset       = 0;
		_active->reset_scroll_region();
		_active->move_cursor( 0, 0 );
	}

	char32_t Terminal::translate( char32_t code_point ) const{
		if( _charsets[static_cast<std::size_t>( _active_charset )] != 1 )
			return code_point;
		if( code_point < 0x5F || code_point > 0x7E )
			return code_point;
		return DEC_SPECIAL_GRAPHICS[code_point - 0x5F];
	}

	// ---------------------------------------------------------------------------
	// VtHandler
	// ---------------------------------------------------------------------------

	void Terminal::print( char32_t code_point ){
		char32_t const glyph = translate( code_point );
		int const      width = character_width( glyph );

		if( width == 0 ){
			// A combining mark attaches to the cell to the left of the cursor.
			int const column = _active->cursor().pending_wrap ? _active->cursor().column : _active->cursor().column - 1;
			if( column >= 0 ){
				Line& target = _active->line( _active->cursor().row );
				Cell& cell   = target[static_cast<std::size_t>( column )];
				// Only variation selectors and the like are dropped; a real mark
				// would need a per-cell string, which the grid deliberately avoids.
				if( glyph >= 0xFE00 && glyph <= 0xFE0F )
					return;
				if( cell.character == U' ' )
					cell.character = glyph;
			}
			return;
		}

		_active->write_character( glyph, width, _attributes, _modes.insert_mode, _modes.auto_wrap );
		_dirty = true;
	}

	void Terminal::execute( std::uint8_t control ){
		Screen& screen = *_active;

		switch( control ){
			case 0x07: // BEL
				bell_rang();
				break;
			case 0x08: // BS
				if( screen.cursor().pending_wrap )
					screen.cursor().pending_wrap = false;
				else if( screen.cursor().column > 0 )
					screen.move_cursor_relative( 0, -1 );
				break;
			case 0x09: // HT
				screen.set_column( screen.next_tab_stop( screen.cursor().column ) );
				break;
			case 0x0A: // LF
			case 0x0B: // VT
			case 0x0C: // FF
				screen.index( _attributes );
				if( _modes.new_line_mode )
					screen.carriage_return();
				break;
			case 0x0D: // CR
				screen.carriage_return();
				break;
			case 0x0E: // SO - select G1
				_active_charset = 1;
				break;
			case 0x0F: // SI - select G0
				_active_charset = 0;
				break;
			default:
				break;
		}

		_dirty = true;
	}

	void Terminal::esc_dispatch( EscSequence const& sequence ){
		Screen& screen = *_active;

		// Charset designation: ESC ( <set> for G0, ESC ) <set> for G1.
		if( sequence.intermediate == '(' || sequence.intermediate == ')' ){
			int const slot                              = ( sequence.intermediate == '(' ) ? 0 : 1;
			_charsets[static_cast<std::size_t>( slot )] = ( sequence.final == '0' ) ? 1 : 0;
			return;
		}

		switch( sequence.final ){
			case 'D': // IND
				screen.index( _attributes );
				break;
			case 'E': // NEL
				screen.next_line( _attributes );
				break;
			case 'H': // HTS
				screen.set_tab_stop( screen.cursor().column );
				break;
			case 'M': // RI
				screen.reverse_index( _attributes );
				break;
			case '7': // DECSC
				save_cursor();
				break;
			case '8':
				if( sequence.intermediate == '#' ){
					// DECALN: fill the screen with 'E', used by test suites.
					screen.fill_with( U'E', _attributes );
				}
				else{
					restore_cursor();
				}
				break;
			case '=': // DECKPAM
				_modes.application_keypad = true;
				break;
			case '>': // DECKPNM
				_modes.application_keypad = false;
				break;
			case 'c': // RIS
				reset();
				break;
			default:
				break;
		}

		_dirty = true;
	}

	void Terminal::csi_dispatch( CsiSequence const& sequence ){
		Screen&    screen     = *_active;
		bool const is_private = sequence.private_marker == '?';

		switch( sequence.final ){
			case '@': // ICH
				screen.insert_characters( sequence.positive_parameter( 0 ), _attributes );
				break;

			case 'A': // CUU
				screen.move_cursor_relative( -sequence.positive_parameter( 0 ), 0 );
				break;
			case 'B': // CUD
			case 'e': // VPR
				screen.move_cursor_relative( sequence.positive_parameter( 0 ), 0 );
				break;
			case 'C': // CUF
			case 'a': // HPR
				screen.move_cursor_relative( 0, sequence.positive_parameter( 0 ) );
				break;
			case 'D': // CUB
				screen.move_cursor_relative( 0, -sequence.positive_parameter( 0 ) );
				break;

			case 'E': // CNL
				screen.set_row( screen.cursor().row + sequence.positive_parameter( 0 ) );
				screen.carriage_return();
				break;
			case 'F': // CPL
				screen.set_row( screen.cursor().row - sequence.positive_parameter( 0 ) );
				screen.carriage_return();
				break;

			case 'G': // CHA
			case '`': // HPA
				screen.set_column( sequence.positive_parameter( 0 ) - 1 );
				break;
			case 'd': // VPA
				screen.set_row( sequence.positive_parameter( 0 ) - 1 );
				break;

			case 'H':   // CUP
			case 'f': { // HVP
				int       row    = sequence.positive_parameter( 0 ) - 1;
				int const column = sequence.positive_parameter( 1 ) - 1;
				if( _modes.origin_mode )
					row += screen.scroll_top();
				screen.move_cursor( row, column );
				break;
			}

			case 'I': // CHT
				for( int i = 0; i < sequence.positive_parameter( 0 ); ++i )
					screen.set_column( screen.next_tab_stop( screen.cursor().column ) );
				break;
			case 'Z': // CBT
				for( int i = 0; i < sequence.positive_parameter( 0 ); ++i )
					screen.set_column( screen.previous_tab_stop( screen.cursor().column ) );
				break;

			case 'J': // ED
				screen.erase_in_display( erase_mode_for( sequence.parameter( 0, 0 ) ), _attributes );
				if( sequence.parameter( 0, 0 ) == 3 )
					screen.clear_scrollback();
				break;
			case 'K': // EL
				screen.erase_in_line( erase_mode_for( sequence.parameter( 0, 0 ) ), _attributes );
				break;

			case 'L': // IL
				screen.insert_lines( sequence.positive_parameter( 0 ), _attributes );
				break;
			case 'M': // DL
				screen.delete_lines( sequence.positive_parameter( 0 ), _attributes );
				break;
			case 'P': // DCH
				screen.delete_characters( sequence.positive_parameter( 0 ), _attributes );
				break;
			case 'X': // ECH
				screen.erase_characters( sequence.positive_parameter( 0 ), _attributes );
				break;

			case 'S': // SU
				screen.scroll_up( sequence.positive_parameter( 0 ), _attributes );
				break;
			case 'T': // SD
				screen.scroll_down( sequence.positive_parameter( 0 ), _attributes );
				break;

			case 'c': // DA
				if( !is_private ){
					// "VT220 with 132 columns, selective erase and colour".
					reply( "\033[?62;1;6;9;15;22c" );
				}
				break;

			case 'g': // TBC
				if( sequence.parameter( 0, 0 ) == 3 )
					screen.clear_all_tab_stops();
				else
					screen.clear_tab_stop( screen.cursor().column );
				break;

			case 'h': // SM / DECSET
				set_mode( sequence, true );
				break;
			case 'l': // RM / DECRST
				set_mode( sequence, false );
				break;

			case 'm': // SGR
				apply_sgr( sequence );
				break;

			case 'n': // DSR
				report_device_status( sequence );
				break;

			case 'p':
				if( sequence.intermediate == '!' ) // DECSTR - soft reset.
					soft_reset();
				break;

			case 'q':
				// DECSCUSR: cursor shape. The widget always draws a block, so the
				// request is accepted and ignored rather than echoed back.
				break;

			case 'r': // DECSTBM
				if( is_private )
					break;
				if( sequence.parameter_count == 0 ){
					screen.reset_scroll_region();
				}
				else{
					screen.set_scroll_region( sequence.positive_parameter( 0 ) - 1,
											  sequence.positive_parameter( 1, _rows ) - 1 );
				}
				screen.move_cursor( _modes.origin_mode ? screen.scroll_top() : 0, 0 );
				break;

			case 's': // Save cursor (ANSI.SYS style).
				save_cursor();
				break;
			case 'u':
				restore_cursor();
				break;

			case 't':
				// Window manipulation. Only the size report is answered; resizing the
				// window from the remote side is deliberately not honoured.
				if( sequence.parameter( 0, 0 ) == 18 ){
					reply( std::format( "\033[8;{};{}t", _rows, _columns ) );
				}
				break;

			default:
				break;
		}

		_dirty = true;
	}

	void Terminal::apply_sgr( CsiSequence const& sequence ){
		if( sequence.parameter_count == 0 ){
			_attributes.reset();
			return;
		}

		for( int i = 0; i < sequence.parameter_count; ++i ){
			int const code = sequence.parameter( i, 0 );

			switch( code ){
				case 0:
					_attributes.reset();
					break;
				case 1:
					_attributes.flags |= CellFlag::BOLD;
					break;
				case 2:
					_attributes.flags |= CellFlag::FAINT;
					break;
				case 3:
					_attributes.flags |= CellFlag::ITALIC;
					break;
				case 4:
					_attributes.flags |= CellFlag::UNDERLINE;
					break;
				case 5:
				case 6:
					_attributes.flags |= CellFlag::BLINK;
					break;
				case 7:
					_attributes.flags |= CellFlag::INVERSE;
					break;
				case 8:
					_attributes.flags |= CellFlag::HIDDEN;
					break;
				case 9:
					_attributes.flags |= CellFlag::STRIKEOUT;
					break;
				case 21:
					_attributes.flags |= CellFlag::DOUBLE_UNDERLINE;
					break;
				case 22:
					_attributes.flags &= ~( CellFlag::BOLD | CellFlag::FAINT );
					break;
				case 23:
					_attributes.flags &= ~CellFlag::ITALIC;
					break;
				case 24:
					_attributes.flags &= ~( CellFlag::UNDERLINE | CellFlag::DOUBLE_UNDERLINE );
					break;
				case 25:
					_attributes.flags &= ~CellFlag::BLINK;
					break;
				case 27:
					_attributes.flags &= ~CellFlag::INVERSE;
					break;
				case 28:
					_attributes.flags &= ~CellFlag::HIDDEN;
					break;
				case 29:
					_attributes.flags &= ~CellFlag::STRIKEOUT;
					break;

				case 39:
					_attributes.foreground = Color::default_color();
					break;
				case 49:
					_attributes.background = Color::default_color();
					break;
				case 59:
					_attributes.underline_color = Color::default_color();
					break;

				case 38:
				case 48:
				case 58:{
					// Extended colour: 5;<index> or 2;<r>;<g>;<b>. Both the ';' and the
					// ':' separated forms arrive as flat parameters here.
					Color     parsed;
					int const selector = sequence.parameter( i + 1, -1 );

					if( selector == 5 && i + 2 < sequence.parameter_count ){
						parsed = Color::indexed(
							static_cast<std::uint8_t>( std::clamp( sequence.parameter( i + 2, 0 ), 0, 255 ) ) );
						i += 2;
					}
					else if( selector == 2 && i + 4 < sequence.parameter_count ){
						parsed = Color::rgb(
							static_cast<std::uint8_t>( std::clamp( sequence.parameter( i + 2, 0 ), 0, 255 ) ),
							static_cast<std::uint8_t>( std::clamp( sequence.parameter( i + 3, 0 ), 0, 255 ) ),
							static_cast<std::uint8_t>( std::clamp( sequence.parameter( i + 4, 0 ), 0, 255 ) ) );
						i += 4;
					}
					else{
						// Malformed: skip the selector and carry on rather than
						// misreading the rest of the sequence as attributes.
						i += 1;
						break;
					}

					if( code == 38 )
						_attributes.foreground = parsed;
					else if( code == 48 )
						_attributes.background = parsed;
					else
						_attributes.underline_color = parsed;
					break;
				}

				default:
					if( code >= 30 && code <= 37 )
						_attributes.foreground = Color::indexed( static_cast<std::uint8_t>( code - 30 ) );
					else if( code >= 40 && code <= 47 )
						_attributes.background = Color::indexed( static_cast<std::uint8_t>( code - 40 ) );
					else if( code >= 90 && code <= 97 )
						_attributes.foreground = Color::indexed( static_cast<std::uint8_t>( code - 90 + 8 ) );
					else if( code >= 100 && code <= 107 )
						_attributes.background = Color::indexed( static_cast<std::uint8_t>( code - 100 + 8 ) );
					break;
			}
		}
	}

	void Terminal::set_mode( CsiSequence const& sequence, bool enabled ){
		if( sequence.private_marker == '?' ){
			for( int i = 0; i < sequence.parameter_count; ++i )
				set_private_mode( sequence.parameter( i, 0 ), enabled );
			return;
		}

		for( int i = 0; i < sequence.parameter_count; ++i ){
			switch( sequence.parameter( i, 0 ) ){
				case 4: // IRM
					_modes.insert_mode = enabled;
					break;
				case 20: // LNM
					_modes.new_line_mode = enabled;
					break;
				default:
					break;
			}
		}
	}

	void Terminal::set_private_mode( int mode, bool enabled ){
		switch( mode ){
			case 1: // DECCKM
				_modes.application_cursor_keys = enabled;
				break;
			case 3: // DECCOLM - clears the screen as a side effect.
				_active->erase_in_display( Screen::EraseMode::ALL, _attributes );
				_active->move_cursor( 0, 0 );
				break;
			case 6: // DECOM
				_modes.origin_mode = enabled;
				_active->move_cursor( enabled ? _active->scroll_top() : 0, 0 );
				break;
			case 7: // DECAWM
				_modes.auto_wrap = enabled;
				break;
			case 12: // Cursor blink.
				break;
			case 25: // DECTCEM
				_modes.cursor_visible = enabled;
				break;

			case 5: // DECSCNM
				_modes.reverse_video = enabled;
				break;

			case 9:
				_modes.mouse_tracking = enabled ? MouseTracking::X10 : MouseTracking::OFF;
				mouse_tracking_changed( _modes.mouse_tracking );
				break;
			case 1000:
				_modes.mouse_tracking = enabled ? MouseTracking::NORMAL : MouseTracking::OFF;
				mouse_tracking_changed( _modes.mouse_tracking );
				break;
			case 1002:
				_modes.mouse_tracking = enabled ? MouseTracking::BUTTON_EVENT : MouseTracking::OFF;
				mouse_tracking_changed( _modes.mouse_tracking );
				break;
			case 1003:
				_modes.mouse_tracking = enabled ? MouseTracking::ANY_EVENT : MouseTracking::OFF;
				mouse_tracking_changed( _modes.mouse_tracking );
				break;

			case 1004:
				_modes.focus_reporting = enabled;
				break;

			case 1005:
				_modes.mouse_encoding = enabled ? MouseEncoding::UTF8 : MouseEncoding::DEFAULT;
				break;
			case 1006:
				_modes.mouse_encoding = enabled ? MouseEncoding::SGR : MouseEncoding::DEFAULT;
				break;
			case 1015:
				_modes.mouse_encoding = enabled ? MouseEncoding::URXVT : MouseEncoding::DEFAULT;
				break;

			case 47:
			case 1047:
				use_alternate_screen( enabled, /*clear_on_enter=*/false );
				break;
			case 1048:
				if( enabled )
					save_cursor();
				else
					restore_cursor();
				break;
			case 1049:
				// The combined form: save the cursor, switch, and clear.
				if( enabled )
					save_cursor();
				use_alternate_screen( enabled, /*clear_on_enter=*/true );
				if( !enabled )
					restore_cursor();
				break;

			case 2004:
				_modes.bracketed_paste = enabled;
				bracketed_paste_changed( enabled );
				break;

			default:
				break;
		}
	}

	void Terminal::use_alternate_screen( bool enabled, bool clear_on_enter ){
		if( _modes.alternate_screen == enabled )
			return;

		// Keep revision() monotonic so the widget never mistakes a buffer switch
		// for "nothing changed".
		_revision_base += _active->revision() + 1;

		_modes.alternate_screen = enabled;
		_active                 = enabled ? _alternate.get() : _normal.get();

		if( enabled && clear_on_enter ){
			_alternate->reset( _attributes );
			_alternate->move_cursor( 0, 0 );
		}

		alternate_screen_changed( enabled );
	}

	void Terminal::report_device_status( CsiSequence const& sequence ){
		switch( sequence.parameter( 0, 0 ) ){
			case 5: // Terminal status: always "OK".
				reply( "\033[0n" );
				break;
			case 6: { // CPR
				CursorState const& cursor = _active->cursor();
				int                row    = cursor.row + 1;
				if( _modes.origin_mode )
					row -= _active->scroll_top();
				reply( std::format( "\033[{};{}R", row, cursor.column + 1 ) );
				break;
			}
			default:
				break;
		}
	}

	void Terminal::save_cursor(){
		CursorState state = _active->cursor();
		state.attributes  = _attributes;
		state.origin_mode = _modes.origin_mode;
		state.charset     = _active_charset;

		if( _modes.alternate_screen )
			_saved_alternate_cursor = state;
		else
			_saved_cursor = state;
	}

	void Terminal::restore_cursor(){
		CursorState const& state = _modes.alternate_screen ? _saved_alternate_cursor : _saved_cursor;

		_attributes        = state.attributes;
		_modes.origin_mode = state.origin_mode;
		_active_charset    = state.charset;
		_active->move_cursor( state.row, state.column );
	}

	void Terminal::osc_dispatch( std::string const& payload ){
		std::size_t const separator = payload.find( ';' );
		if( separator == std::string::npos )
			return;

		int        command  = 0;
		auto const number   = std::string_view( payload ).substr( 0, separator );
		auto const [end, e] = std::from_chars( number.data(), number.data() + number.size(), command );
		if( e != std::errc{} || end != number.data() + number.size() )
			return;

		std::string const argument = payload.substr( separator + 1 );

		switch( command ){
			case 0:   // Icon name and window title.
			case 1:   // Icon name.
			case 2: { // Window title.
				// Remote titles are untrusted text: strip controls so a hostile host
				// cannot smuggle escape sequences into the tab bar.
				std::string title;
				title.reserve( argument.size() );
				for( char const character : argument ){
					auto const byte = static_cast<unsigned char>( character );
					if( byte >= 0x20 && byte != 0x7F )
						title.push_back( character );
				}
				if( title.size() > 256 )
					title.resize( 256 );

				if( command != 1 && title != _title ){
					_title = title;
					title_changed( _title );
				}
				break;
			}

			case 52: { // Clipboard access.
				std::size_t const comma = argument.find( ';' );
				if( comma == std::string::npos )
					break;
				std::string const encoded = argument.substr( comma + 1 );
				if( encoded == "?" )
					break; // Reading the local clipboard from the remote side is refused.

				if( auto const decoded = base64_decode( encoded ) )
					clipboard_write_requested( *decoded );
				break;
			}

			default:
				break;
		}

		_dirty = true;
	}

} // namespace arterm::term
