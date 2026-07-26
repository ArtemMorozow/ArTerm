#pragma once

#include "core/signal.hpp"
#include "terminal/screen.hpp"
#include "terminal/vt_parser.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace arterm::term
{

	/// How the application asked for mouse events to be reported.
	enum class MouseTracking{
		OFF,
		X10,          ///< 9: press only.
		NORMAL,       ///< 1000: press and release.
		BUTTON_EVENT, ///< 1002: press, release and drag.
		ANY_EVENT,    ///< 1003: everything including plain motion.
	};

	enum class MouseEncoding{
		DEFAULT, ///< Legacy 0x20-offset byte encoding.
		UTF8,    ///< 1005.
		SGR,     ///< 1006, the only one that survives past column 223.
		URXVT,   ///< 1015.
	};

	/// The DEC/xterm modes ArTerm honours.
	struct TerminalModes
	{
		bool auto_wrap{ true };                ///< DECAWM (7).
		bool origin_mode{ false };             ///< DECOM (6).
		bool insert_mode{ false };             ///< IRM (4).
		bool cursor_visible{ true };           ///< DECTCEM (25).
		bool application_cursor_keys{ false }; ///< DECCKM (1).
		bool application_keypad{ false };      ///< DECKPAM.
		bool reverse_video{ false };           ///< DECSCNM (5).
		bool bracketed_paste{ false };         ///< 2004.
		bool new_line_mode{ false };           ///< LNM (20).
		bool focus_reporting{ false };         ///< 1004.
		bool alternate_screen{ false };        ///< 1047/1049.

		MouseTracking mouse_tracking{ MouseTracking::OFF };
		MouseEncoding mouse_encoding{ MouseEncoding::DEFAULT };
	};

	/// A complete VT100/xterm emulator: feed it bytes, read a screen back.
	///
	/// The class is GUI-free - the AppKit view renders whatever it exposes - which
	/// is what lets the emulator be tested without an application running.
	///
	/// Signals fire synchronously on the caller's thread. `receive` is driven from
	/// the session queue, so a slot that touches the UI marshals with `on_main`.
	class Terminal : private VtHandler
	{
	public:
		Terminal( int columns, int rows );
		~Terminal() override;

		Terminal( Terminal const& )            = delete;
		Terminal& operator=( Terminal const& ) = delete;

		/// Feed bytes received from the remote shell.
		void receive( std::string_view data );

		void resize( int columns, int rows );
		void reset();

		[[nodiscard]] int columns() const noexcept { return _columns; }
		[[nodiscard]] int rows() const noexcept { return _rows; }

		/// The buffer currently displayed (normal or alternate).
		[[nodiscard]] Screen const& screen() const { return *_active; }
		[[nodiscard]] Screen&       screen() { return *_active; }

		[[nodiscard]] TerminalModes const& modes() const noexcept { return _modes; }
		[[nodiscard]] Attributes const&    current_attributes() const noexcept { return _attributes; }
		[[nodiscard]] std::string const&   title() const noexcept { return _title; }

		void              set_scrollback_limit( int lines );
		[[nodiscard]] int scrollback_limit() const noexcept { return _scrollback_limit; }

		/// Bumped on every change; the widget uses it to skip redundant repaints.
		[[nodiscard]] std::uint64_t revision() const noexcept { return _active->revision() + _revision_base; }

		/// Bytes the emulator wants sent back to the host (DA, DSR, ...).
		Signal<std::string const&> reply;
		Signal<std::string const&> title_changed;
		Signal<>                   bell_rang;
		Signal<>                   screen_changed;
		Signal<bool>               alternate_screen_changed;
		Signal<MouseTracking>      mouse_tracking_changed;
		Signal<bool>               bracketed_paste_changed;
		/// OSC 52: the remote side wants to put `text` on the local clipboard.
		Signal<std::string const&> clipboard_write_requested;

	private:
		// VtHandler.
		void print( char32_t code_point ) override;
		void execute( std::uint8_t control ) override;
		void csi_dispatch( CsiSequence const& sequence ) override;
		void esc_dispatch( EscSequence const& sequence ) override;
		void osc_dispatch( std::string const& payload ) override;

		void apply_sgr( CsiSequence const& sequence );
		void set_mode( CsiSequence const& sequence, bool enabled );
		void set_private_mode( int mode, bool enabled );
		void use_alternate_screen( bool enabled, bool clear_on_enter );
		void report_device_status( CsiSequence const& sequence );
		void save_cursor();
		void restore_cursor();
		void soft_reset();

		/// Translate through the active G0/G1 charset (DEC line drawing).
		[[nodiscard]] char32_t translate( char32_t code_point ) const;

		int _columns;
		int _rows;
		int _scrollback_limit{ 10'000 };

		std::unique_ptr<Screen> _normal;
		std::unique_ptr<Screen> _alternate;
		Screen*                 _active{ nullptr };

		VtParser      _parser;
		TerminalModes _modes;
		Attributes    _attributes;
		std::string   _title;

		CursorState _saved_cursor;
		CursorState _saved_alternate_cursor;

		/// 0 = US ASCII, 1 = DEC special graphics. Index 0/1 are G0/G1.
		std::array<int, 2> _charsets{ 0, 0 };
		int                _active_charset{ 0 };

		/// Offset keeping `revision()` monotonic across a buffer switch.
		std::uint64_t _revision_base{ 0 };

		/// Coalesces the `screen_changed` signal to once per batch of input.
		bool _dirty{ false };
	};

} // namespace arterm::term
