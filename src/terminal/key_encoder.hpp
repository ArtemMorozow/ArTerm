#pragma once

#include "terminal/key_event.hpp"

#include <string>
#include <string_view>

namespace arterm::term
{

	/// Turns key presses into the byte sequences an xterm-compatible host expects.
	///
	/// The two mode flags come from the emulator: DECCKM changes the cursor keys
	/// between the CSI and SS3 forms, and DECKPAM does the same for the keypad.
	class KeyEncoder
	{
	public:
		struct Options
		{
			bool application_cursor_keys{ false };
			bool application_keypad{ false };
			bool new_line_mode{ false };
			/// macOS: when true, the Option key acts as Meta and prefixes ESC;
			/// when false it composes characters (é, ø, ...) as the system does.
			bool option_is_meta{ true };
			/// Send DEL (0x7F) rather than BS (0x08) for the Backspace key.
			bool backspace_sends_delete{ true };
		};

		/// Returns the bytes to send, or an empty string when the key should be
		/// handled by the view instead (shortcuts, modifiers on their own).
		[[nodiscard]] static std::string encode( KeyEvent const& event, Options const& options );

		/// Wrap pasted text for bracketed paste mode and strip anything that could
		/// be mistaken for a control sequence.
		[[nodiscard]] static std::string encode_paste( std::string_view text, bool bracketed );

	private:
		/// xterm's modifier parameter: 1 + shift(1) + alt(2) + ctrl(4) + meta(8).
		[[nodiscard]] static int modifier_parameter( KeyModifier modifiers );

		/// Build "ESC [ <number> ; <mod> ~" or the unmodified "ESC [ <number> ~".
		[[nodiscard]] static std::string tilde_sequence( int number, int modifier );

		/// Build "ESC [ 1 ; <mod> <final>", or the CSI/SS3 form when unmodified.
		[[nodiscard]] static std::string cursor_sequence( char final, int modifier, bool application_mode );
	};

} // namespace arterm::term
