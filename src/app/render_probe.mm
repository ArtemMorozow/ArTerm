#import "app/render_probe.h"

#import "app/terminal_view.h"

#include <string>
#include <vector>

namespace
{

	struct Case
	{
		char const* name;
		char const* description;
		std::string content;
		/// Applied after the content is fed, for state a byte stream cannot set.
		void ( *arrange )( ArTermTerminalView* view ){ nullptr };
	};

	/// Exercises every path the renderer has: grid alignment against a ruler,
	/// wide glyphs, fallback glyphs, the SGR attributes, all three colour models
	/// and the hand-drawn line set.
	std::string general(){
		std::string probe;
		probe += "0123456789012345678901234567890123456789\r\n";
		probe += "|....|....|....|....|....|....|....|....|\r\n";
		probe += "ASCII    the quick brown fox 0123456789\r\n";
		probe += "CJK      \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x83\x86\xe3\x82\xad wide\r\n";
		probe += "Emoji    \xf0\x9f\x98\x80\xf0\x9f\x9a\x80\xf0\x9f\x8e\xaf done\r\n";
		probe += "Accents  \xc3\xa9\xc3\xa8\xc3\xaa\xc3\xab \xc3\xb8\xc3\xa5\xc3\xa6 "
				 "\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\r\n";
		probe += "\033[1mBOLD\033[0m \033[4munderline\033[0m \033[9mstrike\033[0m \033[7minverse\033[0m\r\n";
		probe += "\033[31mred \033[32mgreen \033[33myellow \033[34mblue \033[35mmagenta \033[36mcyan\033[0m\r\n";
		probe += "\033[38;5;208m256-colour\033[0m \033[38;2;120;200;255mtruecolour\033[0m\r\n";
		probe += "\033[41;97m background red \033[0m normal\r\n";
		probe += "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\xac\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90  "
				 "\xe2\x95\x94\xe2\x95\x90\xe2\x95\x90\xe2\x95\xa6\xe2\x95\x90\xe2\x95\x90\xe2\x95\x97  "
				 "\xe2\x94\x8f\xe2\x94\x81\xe2\x94\x81\xe2\x94\xb3\xe2\x94\x81\xe2\x94\x81\xe2\x94\x93  "
				 "\xe2\x96\x88\xe2\x96\x93\xe2\x96\x92\xe2\x96\x91 \xe2\x96\x80\xe2\x96\x84\r\n";
		probe += "\xe2\x94\x82  \xe2\x94\x82  \xe2\x94\x82  "
				 "\xe2\x95\x91  \xe2\x95\x91  \xe2\x95\x91  "
				 "\xe2\x94\x83  \xe2\x94\x83  \xe2\x94\x83\r\n";
		probe += "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80\xe2\x94\xbc\xe2\x94\x80\xe2\x94\x80\xe2\x94\xa4  "
				 "\xe2\x95\xa0\xe2\x95\x90\xe2\x95\x90\xe2\x95\xac\xe2\x95\x90\xe2\x95\x90\xe2\x95\xa3  "
				 "\xe2\x94\xa3\xe2\x94\x81\xe2\x94\x81\xe2\x95\x8b\xe2\x94\x81\xe2\x94\x81\xe2\x94\xab\r\n";
		probe += "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\xb4\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98  "
				 "\xe2\x95\x9a\xe2\x95\x90\xe2\x95\x90\xe2\x95\xa9\xe2\x95\x90\xe2\x95\x90\xe2\x95\x9d  "
				 "\xe2\x94\x97\xe2\x94\x81\xe2\x94\x81\xe2\x94\xbb\xe2\x94\x81\xe2\x94\x81\xe2\x94\x9b\r\n";
		probe += "\xe6\x97\xa5X\xe6\x9c\xacY wide/narrow interleaved\r\n";
		probe += "cursor after the arrow -> ";
		return probe;
	}

	/// The cursor must cover a whole wide glyph rather than half of it, whether
	/// it lands on the leading or the trailing cell.
	std::string cursor_on_wide(){
		std::string probe = "cursor parked on a wide glyph:\r\n\r\n";
		probe += "  \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e  <- next line puts the cursor on the middle one\r\n\r\n";
		probe += "  \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
		probe += "\033[2D"; // Back two cells: onto the trailing half of the middle glyph.
		return probe;
	}

	/// With the cursor one past the last column, waiting for the wrap, it must
	/// still be painted inside the grid.
	std::string cursor_at_end_of_line(){
		std::string probe = "cursor past the last column (pending wrap):\r\n\r\n";
		probe += "\033[4;1H";
		for( int i = 0; i < 200; ++i )
			probe += static_cast<char>( 'a' + ( i % 26 ) );
		return probe;
	}

	/// A hidden cursor must leave no block behind, and inverse text under the
	/// cursor must stay readable.
	std::string cursor_hidden_and_inverse(){
		std::string probe = "\033[7minverse row, cursor hidden below\033[0m\r\n\r\n";
		probe += "\033[?25lthe cursor is off, no block should appear -> ";
		return probe;
	}

	/// Enough rows to push earlier ones into the history.
	std::string numbered_rows(){
		std::string probe;
		for( int i = 1; i <= 60; ++i )
			probe += "row " + std::to_string( i ) + " of sixty\r\n";
		probe += "prompt$ ";
		return probe;
	}

	std::vector<Case> cases(){
		return {
			{ "general", "grid, glyphs, colours, line drawing", general() },
			{ "scrolled-back", "view scrolled into the history", numbered_rows(),
			  []( ArTermTerminalView* view ){ [view scrollByRows:8]; } },
			{ "selection", "a selection spanning several rows", numbered_rows(),
			  []( ArTermTerminalView* view ){ [view selectFromRow:2 column:4 toRow:4 column:9]; } },
			{ "cursor-wide", "cursor over a double-width glyph", cursor_on_wide() },
			{ "cursor-eol", "cursor past the last column", cursor_at_end_of_line() },
			{ "cursor-hidden", "DECTCEM off, inverse text", cursor_hidden_and_inverse() },
		};
	}

	bool write_case( Case const& probe, NSString* directory ){
		NSRect const        frame = NSMakeRect( 0, 0, 760, 420 );
		ArTermTerminalView* view  = [[ArTermTerminalView alloc] initWithFrame:frame];

		// The view only paints a cursor block when it owns focus, and offscreen it
		// never will; the probe forces the focused look so the cursor is visible.
		[view setForcesFocusedAppearance:YES];
		[view feed:probe.content];
		if( probe.arrange != nullptr )
			probe.arrange( view );

		NSBitmapImageRep* bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
		if( bitmap == nil )
			return false;

		[view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];

		NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
		if( png == nil )
			return false;

		NSString* path = [directory stringByAppendingPathComponent:[NSString stringWithFormat:@"%s.png", probe.name]];
		return [png writeToFile:path atomically:YES];
	}

} // namespace

bool arterm_write_render_probe( NSString* directory ){
	NSError* error = nil;
	[NSFileManager.defaultManager createDirectoryAtPath:directory
							withIntermediateDirectories:YES
											 attributes:nil
												  error:&error];

	bool ok = true;
	for( Case const& probe : cases() ){
		if( !write_case( probe, directory ) ){
			NSLog( @"render probe: could not write case %s", probe.name );
			ok = false;
		}
	}
	return ok;
}
