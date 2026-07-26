#pragma once

#include <cstdint>
#include <string>

namespace arterm::term
{

	/// The keys that do not produce text and therefore need a name of their own.
	/// Anything else arrives as `CHARACTER` with `text` filled in.
	enum class Key{
		CHARACTER,
		UP,
		DOWN,
		LEFT,
		RIGHT,
		HOME,
		END,
		INSERT,
		DELETE_FORWARD,
		PAGE_UP,
		PAGE_DOWN,
		F1,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,
		RETURN,
		ENTER,
		BACKSPACE,
		TAB,
		BACKTAB,
		ESCAPE,
		/// Shift, Control, Option, Command, Caps Lock and friends pressed alone.
		MODIFIER,
	};

	enum class KeyModifier : std::uint8_t{
		NONE    = 0,
		SHIFT   = 1u << 0,
		ALT     = 1u << 1, ///< Option on macOS.
		CONTROL = 1u << 2,
		COMMAND = 1u << 3, ///< Reported as xterm's "meta".
	};

	constexpr KeyModifier operator|( KeyModifier a, KeyModifier b ){
		return static_cast<KeyModifier>( static_cast<std::uint8_t>( a ) | static_cast<std::uint8_t>( b ) );
	}

	constexpr KeyModifier& operator|=( KeyModifier& a, KeyModifier b ){
		a = a | b;
		return a;
	}

	constexpr bool has_modifier( KeyModifier value, KeyModifier flag ){
		return ( static_cast<std::uint8_t>( value ) & static_cast<std::uint8_t>( flag ) ) != 0;
	}

	/// One key press, in terms the emulator understands.
	///
	/// The AppKit view builds this from an NSEvent: `text` is `-characters`, which
	/// is what the input source composed, and `base_character` is
	/// `-charactersIgnoringModifiers`, which is what Ctrl+<key> has to be resolved
	/// against - Ctrl+Shift+2 must still reach NUL.
	struct KeyEvent
	{
		Key         key{ Key::CHARACTER };
		KeyModifier modifiers{ KeyModifier::NONE };

		/// UTF-8 text the key produced; empty for keys that produce none.
		std::string text;

		/// The character the key carries with modifiers ignored, lowercased for
		/// ASCII letters. Zero when the key has none.
		char32_t base_character{ 0 };
	};

} // namespace arterm::term
