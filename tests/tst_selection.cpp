#include "terminal/selection.hpp"

#include "terminal/terminal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace arterm::term;

namespace
{

	/// A terminal fed `content`, so selections run against a screen the emulator
	/// actually produced rather than one assembled by hand.
	class Fixture
	{
	public:
		Fixture( int columns, int rows, std::string const& content )
			: _terminal( columns, rows )
		{
			_terminal.receive( content );
		}

		[[nodiscard]] Screen const& screen() const { return _terminal.screen(); }

	private:
		Terminal _terminal;
	};

} // namespace

TEST_CASE( "word characters cover what a path or URL needs", "[selection]" ){
	CHECK( is_word_character( U'a' ) );
	CHECK( is_word_character( U'Z' ) );
	CHECK( is_word_character( U'7' ) );
	CHECK( is_word_character( U'_' ) );
	CHECK( is_word_character( U'/' ) );
	CHECK( is_word_character( U'.' ) );
	CHECK( is_word_character( U'-' ) );
	CHECK( is_word_character( U':' ) );
	CHECK( is_word_character( U'~' ) );
	// Non-ASCII is word content; splitting it would make double-click useless.
	CHECK( is_word_character( U'п' ) ); // Cyrillic.
	CHECK( is_word_character( U'日' ) ); // CJK.

	CHECK_FALSE( is_word_character( U' ' ) );
	CHECK_FALSE( is_word_character( U'\0' ) );
	CHECK_FALSE( is_word_character( U'(' ) );
	CHECK_FALSE( is_word_character( U'"' ) );
	CHECK_FALSE( is_word_character( U',' ) );
}

TEST_CASE( "a fresh selection is inactive", "[selection]" ){
	Fixture   fixture( 20, 5, "hello" );
	Selection selection;

	CHECK_FALSE( selection.is_active() );
	CHECK_FALSE( selection.contains( 0, 0 ) );
	CHECK( selection.text( fixture.screen() ).empty() );
}

TEST_CASE( "a character selection covers exactly the dragged range", "[selection]" ){
	Fixture   fixture( 20, 5, "hello world" );
	Selection selection;

	selection.begin( { 0, 0 }, SelectionUnit::CHARACTER, fixture.screen() );
	selection.extend_to( { 0, 4 }, fixture.screen() );

	CHECK( selection.is_active() );
	CHECK( selection.text( fixture.screen() ) == "hello" );
	CHECK( selection.contains( 0, 0 ) );
	CHECK( selection.contains( 0, 4 ) );
	CHECK_FALSE( selection.contains( 0, 5 ) );
	CHECK_FALSE( selection.contains( 1, 0 ) );
}

TEST_CASE( "dragging backwards selects the same range", "[selection]" ){
	Fixture   fixture( 20, 5, "hello world" );
	Selection forwards;
	Selection backwards;

	forwards.begin( { 0, 6 }, SelectionUnit::CHARACTER, fixture.screen() );
	forwards.extend_to( { 0, 10 }, fixture.screen() );

	backwards.begin( { 0, 10 }, SelectionUnit::CHARACTER, fixture.screen() );
	backwards.extend_to( { 0, 6 }, fixture.screen() );

	CHECK( forwards.text( fixture.screen() ) == "world" );
	CHECK( backwards.text( fixture.screen() ) == "world" );
	CHECK( backwards.first() == forwards.first() );
	CHECK( backwards.last() == forwards.last() );
}

TEST_CASE( "a word selection snaps to word bounds", "[selection]" ){
	Fixture   fixture( 40, 5, "run /usr/local/bin/tool --flag now" );
	Selection selection;

	// Anywhere inside the path selects the whole path, separators included.
	selection.begin( { 0, 8 }, SelectionUnit::WORD, fixture.screen() );
	CHECK( selection.text( fixture.screen() ) == "/usr/local/bin/tool" );

	// And inside the first word selects just it.
	Selection other;
	other.begin( { 0, 1 }, SelectionUnit::WORD, fixture.screen() );
	CHECK( other.text( fixture.screen() ) == "run" );
}

TEST_CASE( "a word selection on a space stays put", "[selection]" ){
	Fixture   fixture( 20, 5, "ab cd" );
	Selection selection;

	selection.begin( { 0, 2 }, SelectionUnit::WORD, fixture.screen() );
	CHECK( selection.text( fixture.screen() ) == " " );
}

TEST_CASE( "extending a word selection keeps whole words", "[selection]" ){
	Fixture   fixture( 40, 5, "alpha beta gamma" );
	Selection selection;

	selection.begin( { 0, 7 }, SelectionUnit::WORD, fixture.screen() ); // Inside "beta".
	CHECK( selection.text( fixture.screen() ) == "beta" );

	// Dragging into "gamma" must take all of it, not stop mid-word.
	selection.extend_to( { 0, 12 }, fixture.screen() );
	CHECK( selection.text( fixture.screen() ) == "beta gamma" );

	// Dragging back before the anchor takes all of "alpha".
	selection.extend_to( { 0, 2 }, fixture.screen() );
	CHECK( selection.text( fixture.screen() ) == "alpha beta" );
}

TEST_CASE( "a line selection takes the whole row", "[selection]" ){
	Fixture   fixture( 20, 5, "first\r\nsecond" );
	Selection selection;

	selection.begin( { 0, 3 }, SelectionUnit::LINE, fixture.screen() );
	CHECK( selection.text( fixture.screen() ) == "first" );
	CHECK( selection.contains( 0, 0 ) );
	CHECK( selection.contains( 0, 19 ) );
}

TEST_CASE( "trailing blanks are not part of the text", "[selection]" ){
	Fixture   fixture( 40, 5, "short" );
	Selection selection;

	// Select the whole 40-column row; only the typed characters come back.
	selection.begin( { 0, 0 }, SelectionUnit::LINE, fixture.screen() );
	CHECK( selection.text( fixture.screen() ) == "short" );
}

TEST_CASE( "a multi-row selection joins rows with newlines", "[selection]" ){
	Fixture   fixture( 20, 5, "one\r\ntwo\r\nthree" );
	Selection selection;

	selection.begin( { 0, 0 }, SelectionUnit::CHARACTER, fixture.screen() );
	selection.extend_to( { 2, 4 }, fixture.screen() );

	CHECK( selection.text( fixture.screen() ) == "one\ntwo\nthree" );
}

TEST_CASE( "a wrapped row is copied back as one line", "[selection]" ){
	// 10 columns, so this command wraps rather than ending at a newline; a
	// paste of it has to reproduce the single line that was typed.
	Fixture   fixture( 10, 5, "abcdefghijKLMNO" );
	Selection selection;

	REQUIRE( fixture.screen().is_line_wrapped( 0 ) );

	selection.begin( { 0, 0 }, SelectionUnit::CHARACTER, fixture.screen() );
	selection.extend_to( { 1, 4 }, fixture.screen() );

	CHECK( selection.text( fixture.screen() ) == "abcdefghijKLMNO" );
}

TEST_CASE( "a wrap flag survives the row scrolling into history", "[selection]" ){
	// Five rows of output push the wrapped row off the top; selecting it out of
	// the scrollback must still join it to its continuation.
	Fixture fixture( 10, 3, "abcdefghijKLMNO\r\np\r\nq\r\nr\r\ns" );

	Screen const& screen = fixture.screen();
	REQUIRE( screen.scrollback_size() > 0 );

	int const first = -screen.scrollback_size();
	REQUIRE( screen.history_line( first ) != nullptr );
	CHECK( screen.is_history_line_wrapped( first ) );

	Selection selection;
	selection.begin( { first, 0 }, SelectionUnit::CHARACTER, screen );
	selection.extend_to( { first + 1, 4 }, screen );

	CHECK( selection.text( screen ) == "abcdefghijKLMNO" );
}

TEST_CASE( "a selection reaching into the scrollback reads history rows", "[selection]" ){
	Fixture fixture( 20, 3, "one\r\ntwo\r\nthree\r\nfour\r\nfive" );

	Screen const& screen = fixture.screen();
	REQUIRE( screen.scrollback_size() >= 2 );

	Selection selection;
	selection.begin( { -2, 0 }, SelectionUnit::CHARACTER, screen );
	selection.extend_to( { -1, 2 }, screen );

	CHECK( selection.text( screen ) == "one\ntwo" );
}

TEST_CASE( "a wide glyph contributes one character, not two", "[selection]" ){
	Fixture   fixture( 20, 5, "\xe6\x97\xa5\xe6\x9c\xac" );
	Selection selection;

	selection.begin( { 0, 0 }, SelectionUnit::CHARACTER, fixture.screen() );
	selection.extend_to( { 0, 3 }, fixture.screen() );

	CHECK( selection.text( fixture.screen() ) == "\xe6\x97\xa5\xe6\x9c\xac" );
}

TEST_CASE( "clearing deactivates the selection", "[selection]" ){
	Fixture   fixture( 20, 5, "hello" );
	Selection selection;

	selection.begin( { 0, 0 }, SelectionUnit::LINE, fixture.screen() );
	REQUIRE( selection.is_active() );

	selection.clear();
	CHECK_FALSE( selection.is_active() );
	CHECK_FALSE( selection.contains( 0, 0 ) );
}
