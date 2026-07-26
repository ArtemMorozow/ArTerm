#import "app/terminal_view.h"

#include "core/rgb.hpp"
#include "terminal/color_scheme.hpp"
#include "terminal/key_encoder.hpp"
#include "terminal/key_event.hpp"

#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

using namespace arterm;
using namespace arterm::term;

namespace
{

	constexpr CGFloat FONT_SIZE = 13.0;

	NSColor* ns_color( Rgb rgb ){
		return [NSColor colorWithSRGBRed:rgb.red / 255.0
								   green:rgb.green / 255.0
									blue:rgb.blue / 255.0
								   alpha:rgb.alpha / 255.0];
	}

	void append_utf16( NSMutableString* out, char32_t code_point ){
		if( code_point < 0x10000 ){
			unichar const unit = static_cast<unichar>( code_point );
			[out appendString:[NSString stringWithCharacters:&unit length:1]];
		}
		else{
			char32_t const bias  = code_point - 0x10000;
			unichar const  units[2] = { static_cast<unichar>( 0xD800 + ( bias >> 10 ) ),
										static_cast<unichar>( 0xDC00 + ( bias & 0x3FF ) ) };
			[out appendString:[NSString stringWithCharacters:units length:2]];
		}
	}

	/// Maps an AppKit function-key character (0xF7xx) or control character onto
	/// the emulator's key names.
	Key key_from_event( NSEvent* event, unichar character ){
		switch( character ){
			case NSUpArrowFunctionKey:
				return Key::UP;
			case NSDownArrowFunctionKey:
				return Key::DOWN;
			case NSLeftArrowFunctionKey:
				return Key::LEFT;
			case NSRightArrowFunctionKey:
				return Key::RIGHT;
			case NSHomeFunctionKey:
				return Key::HOME;
			case NSEndFunctionKey:
				return Key::END;
			case NSInsertFunctionKey:
				return Key::INSERT;
			case NSDeleteFunctionKey:
				return Key::DELETE_FORWARD;
			case NSPageUpFunctionKey:
				return Key::PAGE_UP;
			case NSPageDownFunctionKey:
				return Key::PAGE_DOWN;
			case '\r':
			case '\x03':
				return event.keyCode == 76 ? Key::ENTER : Key::RETURN;
			case '\x7F':
				return Key::BACKSPACE;
			case '\t':
				return Key::TAB;
			case 0x19: // NSBackTabCharacter.
				return Key::BACKTAB;
			case '\x1B':
				return Key::ESCAPE;
			default:
				break;
		}

		if( character >= NSF1FunctionKey && character <= NSF12FunctionKey )
			return static_cast<Key>( static_cast<int>( Key::F1 ) + ( character - NSF1FunctionKey ) );

		return Key::CHARACTER;
	}

	KeyModifier modifiers_from_event( NSEvent* event ){
		KeyModifier modifiers = KeyModifier::NONE;
		if( event.modifierFlags & NSEventModifierFlagShift )
			modifiers |= KeyModifier::SHIFT;
		if( event.modifierFlags & NSEventModifierFlagOption )
			modifiers |= KeyModifier::ALT;
		if( event.modifierFlags & NSEventModifierFlagControl )
			modifiers |= KeyModifier::CONTROL;
		if( event.modifierFlags & NSEventModifierFlagCommand )
			modifiers |= KeyModifier::COMMAND;
		return modifiers;
	}

} // namespace

@implementation ArTermTerminalView{
	// unique_ptr rather than a direct ivar: Terminal has no default constructor,
	// which Objective-C++ ivar construction would require.
	std::unique_ptr<Terminal> _terminal;
	ColorScheme               _scheme;

	std::function<void( std::string )>                _on_input;
	std::function<void( int, int, int, int )>         _on_resize;

	NSFont* _font;
	NSFont* _bold_font;
	CGFloat _cell_width;
	CGFloat _cell_height;
	CGFloat _baseline; ///< Distance from the cell top to the text baseline.
}

- (instancetype)initWithFrame:(NSRect)frame{
	if( ( self = [super initWithFrame:frame] ) ){
		_terminal = std::make_unique<Terminal>( 80, 24 );

		_font      = [NSFont monospacedSystemFontOfSize:FONT_SIZE weight:NSFontWeightRegular];
		_bold_font = [NSFont monospacedSystemFontOfSize:FONT_SIZE weight:NSFontWeightBold];

		// The advancement of a representative glyph is the cell width; a fraction
		// of leading keeps adjacent rows from touching.
		_cell_width  = std::ceil( [@"M" sizeWithAttributes:@{ NSFontAttributeName : _font }].width );
		_cell_height = std::ceil( _font.ascender - _font.descender + _font.leading ) + 1;
		_baseline    = std::ceil( _font.ascender );

		self.wantsLayer = YES;
	}
	return self;
}

- (arterm::term::Terminal&)terminal{
	return *_terminal;
}

- (void)setInputHandler:(std::function<void( std::string )>)handler{
	_on_input = std::move( handler );
}

- (void)setResizeHandler:(std::function<void( int, int, int, int )>)handler{
	_on_resize = std::move( handler );
}

- (void)feed:(std::string const&)data{
	_terminal->receive( data );
	self.needsDisplay = YES;
}

// -- Geometry ---------------------------------------------------------------

- (BOOL)isFlipped{
	return YES; // Row 0 at the top, like every terminal.
}

- (void)setFrameSize:(NSSize)size{
	[super setFrameSize:size];

	int const columns = std::max( 2, static_cast<int>( size.width / _cell_width ) );
	int const rows    = std::max( 2, static_cast<int>( size.height / _cell_height ) );

	if( columns == _terminal->columns() && rows == _terminal->rows() )
		return;

	_terminal->resize( columns, rows );
	if( _on_resize )
		_on_resize( columns, rows, static_cast<int>( size.width ), static_cast<int>( size.height ) );
	self.needsDisplay = YES;
}

// -- Rendering --------------------------------------------------------------

- (void)drawRect:(NSRect)dirty{
	[ns_color( _scheme.background() ) setFill];
	NSRectFill( dirty );

	Screen const& screen = _terminal->screen();

	int const first_row = std::clamp( static_cast<int>( NSMinY( dirty ) / _cell_height ), 0, screen.rows() - 1 );
	int const last_row  = std::clamp( static_cast<int>( NSMaxY( dirty ) / _cell_height ), 0, screen.rows() - 1 );

	for( int row = first_row; row <= last_row; ++row )
		[self drawRow:row];

	[self drawCursor];
}

- (void)drawRow:(int)row{
	Screen const& screen = _terminal->screen();
	Line const&   line   = screen.line( row );
	CGFloat const top    = row * _cell_height;

	bool const reverse_video = _terminal->modes().reverse_video;

	// Pass 1: background runs, merged so a full row of one colour is one fill.
	int column = 0;
	while( column < screen.columns() && column < static_cast<int>( line.size() ) ){
		Cell const& cell = line[static_cast<std::size_t>( column )];

		bool const inverse = has_flag( cell.attributes.flags, CellFlag::INVERSE ) != reverse_video;
		Rgb const  background = inverse ? _scheme.resolve( cell.attributes.foreground, true,
														   has_flag( cell.attributes.flags, CellFlag::BOLD ) )
										: _scheme.resolve( cell.attributes.background, false, false );

		int run_end = column + 1;
		while( run_end < screen.columns() && run_end < static_cast<int>( line.size() ) ){
			Cell const& next          = line[static_cast<std::size_t>( run_end )];
			bool const  next_inverse  = has_flag( next.attributes.flags, CellFlag::INVERSE ) != reverse_video;
			Rgb const   next_background = next_inverse
											  ? _scheme.resolve( next.attributes.foreground, true,
																 has_flag( next.attributes.flags, CellFlag::BOLD ) )
											  : _scheme.resolve( next.attributes.background, false, false );
			if( next_background != background )
				break;
			++run_end;
		}

		if( background != _scheme.background() ){
			[ns_color( background ) setFill];
			NSRectFill( NSMakeRect( column * _cell_width, top, ( run_end - column ) * _cell_width, _cell_height ) );
		}
		column = run_end;
	}

	// Pass 2: the glyphs, one attributed line per row.
	NSMutableAttributedString* text = [NSMutableAttributedString new];

	for( int i = 0; i < screen.columns() && i < static_cast<int>( line.size() ); ++i ){
		Cell const& cell = line[static_cast<std::size_t>( i )];
		if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
			continue;

		bool const bold    = has_flag( cell.attributes.flags, CellFlag::BOLD );
		bool const inverse = has_flag( cell.attributes.flags, CellFlag::INVERSE ) != reverse_video;

		Rgb const foreground = inverse ? _scheme.resolve( cell.attributes.background, false, false )
									   : _scheme.resolve( cell.attributes.foreground, true, bold );

		NSMutableString* character = [NSMutableString new];
		append_utf16( character, cell.character == U'\0' ? U' ' : cell.character );

		NSMutableDictionary* attributes = [NSMutableDictionary new];
		attributes[NSFontAttributeName]            = bold ? _bold_font : _font;
		attributes[NSForegroundColorAttributeName] = ns_color( foreground );
		if( has_flag( cell.attributes.flags, CellFlag::UNDERLINE ) ||
			has_flag( cell.attributes.flags, CellFlag::DOUBLE_UNDERLINE ) ){
			attributes[NSUnderlineStyleAttributeName] = @( NSUnderlineStyleSingle );
		}
		if( has_flag( cell.attributes.flags, CellFlag::STRIKEOUT ) )
			attributes[NSStrikethroughStyleAttributeName] = @( NSUnderlineStyleSingle );

		[text appendAttributedString:[[NSAttributedString alloc] initWithString:character attributes:attributes]];
	}

	CGContextRef context = NSGraphicsContext.currentContext.CGContext;
	CGContextSaveGState( context );

	// CoreText draws in an unflipped space; flip back around this row's baseline.
	CGContextSetTextMatrix( context, CGAffineTransformIdentity );
	CGContextTranslateCTM( context, 0, top + _baseline );
	CGContextScaleCTM( context, 1, -1 );

	CTLineRef ct_line = CTLineCreateWithAttributedString( (__bridge CFAttributedStringRef)text );
	CGContextSetTextPosition( context, 0, 0 );
	CTLineDraw( ct_line, context );
	CFRelease( ct_line );

	CGContextRestoreGState( context );
}

- (void)drawCursor{
	if( !_terminal->modes().cursor_visible )
		return;

	CursorState const& cursor = _terminal->screen().cursor();
	NSRect const       rect   = NSMakeRect( cursor.column * _cell_width, cursor.row * _cell_height, _cell_width,
											_cell_height );

	if( self.window.firstResponder == self ){
		[ns_color( _scheme.cursor() ) setFill];
		NSRectFill( rect );

		// Redraw the covered character in the cursor-text colour.
		Line const& line = _terminal->screen().line( cursor.row );
		if( cursor.column < static_cast<int>( line.size() ) ){
			Cell const& cell = line[static_cast<std::size_t>( cursor.column )];
			if( cell.character != U'\0' && cell.character != U' ' ){
				NSMutableString* character = [NSMutableString new];
				append_utf16( character, cell.character );
				[character drawAtPoint:NSMakePoint( NSMinX( rect ), NSMinY( rect ) )
						withAttributes:@{
							NSFontAttributeName : _font,
							NSForegroundColorAttributeName : ns_color( _scheme.cursor_text() ),
						}];
			}
		}
	}
	else{
		[ns_color( _scheme.cursor() ) setStroke];
		NSFrameRect( NSInsetRect( rect, 0.5, 0.5 ) );
	}
}

// -- Input ------------------------------------------------------------------

- (BOOL)acceptsFirstResponder{
	return YES;
}

- (BOOL)becomeFirstResponder{
	self.needsDisplay = YES;
	return [super becomeFirstResponder];
}

- (BOOL)resignFirstResponder{
	self.needsDisplay = YES;
	return [super resignFirstResponder];
}

- (void)keyDown:(NSEvent*)event{
	NSString* raw  = event.characters;
	NSString* base = event.charactersIgnoringModifiers;

	KeyEvent key;
	key.modifiers = modifiers_from_event( event );

	unichar const probe = base.length > 0 ? [base characterAtIndex:0] : 0;
	key.key             = key_from_event( event, probe );

	if( key.key == Key::CHARACTER ){
		// With Option acting as Meta the composed character (é, ø) is wrong; the
		// host wants ESC plus the base character.
		bool const option_as_meta = has_modifier( key.modifiers, KeyModifier::ALT );
		NSString*  text           = option_as_meta ? base : raw;
		key.text                  = text.UTF8String != nullptr ? text.UTF8String : "";

		if( base.length > 0 ){
			unichar unit = [base characterAtIndex:0];
			if( unit >= 'A' && unit <= 'Z' )
				unit = static_cast<unichar>( unit - 'A' + 'a' );
			key.base_character = unit;
		}
	}

	std::string encoded = KeyEncoder::encode( key, [self encoderOptions] );
	if( encoded.empty() ){
		[super keyDown:event];
		return;
	}

	if( _on_input )
		_on_input( std::move( encoded ) );
}

- (arterm::term::KeyEncoder::Options)encoderOptions{
	KeyEncoder::Options options;
	options.application_cursor_keys = _terminal->modes().application_cursor_keys;
	options.application_keypad      = _terminal->modes().application_keypad;
	options.new_line_mode           = _terminal->modes().new_line_mode;
	return options;
}

- (void)paste:(id)sender{
	NSString* text = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
	if( text.length == 0 || text.UTF8String == nullptr )
		return;

	std::string encoded = KeyEncoder::encode_paste( text.UTF8String, _terminal->modes().bracketed_paste );
	if( _on_input && !encoded.empty() )
		_on_input( std::move( encoded ) );
}

@end
