#import "app/terminal_view.h"

#include "core/rgb.hpp"
#include "terminal/box_drawing.hpp"
#include "terminal/color_scheme.hpp"
#include "terminal/key_encoder.hpp"
#include "terminal/key_event.hpp"
#include "terminal/selection.hpp"

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

	/// +scrollerWidth has been deprecated since 10.7 in favour of the
	/// size/style form.
	CGFloat overlay_scroller_width(){
		return [NSScroller scrollerWidthForControlSize:NSControlSizeRegular
										 scrollerStyle:NSScrollerStyleOverlay];
	}

	NSColor* ns_color( Rgb rgb ){
		return [NSColor colorWithSRGBRed:rgb.red / 255.0
								   green:rgb.green / 255.0
									blue:rgb.blue / 255.0
								   alpha:rgb.alpha / 255.0];
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
	CGFloat _pad_x;    ///< Inset from the view edges to the character grid.
	CGFloat _pad_y;
	BOOL    _forces_focused_appearance;

	/// How many rows the view is scrolled back from the live screen. Zero means
	/// the bottom, which is where output keeps it unless the user says otherwise.
	int _scroll_offset;

	Selection _selection;
	BOOL      _dragging;

	NSScroller* _scroller;
}

- (void)setForcesFocusedAppearance:(BOOL)forced{
	_forces_focused_appearance = forced;
	self.needsDisplay          = YES;
}

/// The cursor is a filled block only while the view has the keyboard.
- (BOOL)hasKeyboardFocus{
	return _forces_focused_appearance || self.window.firstResponder == self;
}

- (instancetype)initWithFrame:(NSRect)frame{
	if( ( self = [super initWithFrame:frame] ) ){
		_terminal = std::make_unique<Terminal>( 80, 24 );

		_font      = [NSFont monospacedSystemFontOfSize:FONT_SIZE weight:NSFontWeightRegular];
		_bold_font = [NSFont monospacedSystemFontOfSize:FONT_SIZE weight:NSFontWeightBold];

		// A digit's advance is the cell width for a monospaced font. Line height
		// gets a little extra leading so text breathes instead of looking like a
		// 1980s VT; the glyph baseline is centred in that taller cell.
		_cell_width          = std::ceil( [@"0" sizeWithAttributes:@{ NSFontAttributeName : _font }].width );
		CGFloat const glyph  = _font.ascender - _font.descender;
		_cell_height         = std::ceil( glyph * 1.25 );
		_baseline            = std::round( _font.ascender + ( _cell_height - glyph ) / 2.0 );

		// Breathing room around the grid, the way every modern terminal has it.
		_pad_x = 8;
		_pad_y = 6;

		_scroller = [[NSScroller alloc] initWithFrame:NSMakeRect( 0, 0, overlay_scroller_width(), 100 )];
		_scroller.scrollerStyle       = NSScrollerStyleOverlay;
		_scroller.knobStyle           = NSScrollerKnobStyleLight;
		_scroller.target              = self;
		_scroller.action              = @selector( scrollerMoved: );
		_scroller.autoresizingMask    = NSViewMinXMargin | NSViewHeightSizable;
		_scroller.enabled             = NO;
		[self addSubview:_scroller];

		self.wantsLayer            = YES;
		self.layer.backgroundColor = ns_color( _scheme.background() ).CGColor;
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
	int const before = _terminal->screen().scrollback_size();

	_terminal->receive( data );

	// Output that pushes rows into the history would otherwise drag the view's
	// content upwards; holding the offset keeps what the user is reading still.
	if( _scroll_offset > 0 ){
		int const grew = _terminal->screen().scrollback_size() - before;
		if( grew > 0 )
			_scroll_offset = std::min( _scroll_offset + grew, _terminal->screen().scrollback_size() );
	}

	[self updateScroller];
	self.needsDisplay = YES;
}

// -- Scrollback -------------------------------------------------------------

- (void)scrollToBottom{
	if( _scroll_offset == 0 )
		return;
	_scroll_offset    = 0;
	self.needsDisplay = YES;
	[self updateScroller];
}

- (void)scrollByRows:(int)delta{
	int const history = _terminal->screen().scrollback_size();
	int const wanted  = std::clamp( _scroll_offset + delta, 0, history );
	if( wanted == _scroll_offset )
		return;

	_scroll_offset    = wanted;
	self.needsDisplay = YES;
	[self updateScroller];
}

/// The row shown at the top of the view, in the coordinate space
/// `Screen::history_line` uses: negative reaches into the scrollback.
- (int)topRow{
	return -_scroll_offset;
}

- (void)updateScroller{
	if( _scroller == nil )
		return;

	Screen const& screen  = _terminal->screen();
	int const     history = screen.scrollback_size();
	int const     total   = history + screen.rows();

	_scroller.enabled = history > 0;
	if( total <= 0 )
		return;

	_scroller.knobProportion = static_cast<CGFloat>( screen.rows() ) / total;
	// The scroller runs top-down while the offset counts upwards from the live
	// screen, so a full offset is position zero.
	_scroller.doubleValue = history > 0 ? 1.0 - static_cast<double>( _scroll_offset ) / history : 1.0;
}

- (void)scrollerMoved:(NSScroller*)scroller{
	Screen const& screen  = _terminal->screen();
	int const     history = screen.scrollback_size();
	if( history <= 0 )
		return;

	switch( scroller.hitPart ){
		case NSScrollerKnob:
		case NSScrollerKnobSlot:
			_scroll_offset = static_cast<int>( std::lround( ( 1.0 - scroller.doubleValue ) * history ) );
			break;
		case NSScrollerDecrementPage:
			_scroll_offset += screen.rows();
			break;
		case NSScrollerIncrementPage:
			_scroll_offset -= screen.rows();
			break;
		default:
			return;
	}

	_scroll_offset    = std::clamp( _scroll_offset, 0, history );
	self.needsDisplay = YES;
	[self updateScroller];
}

- (void)scrollWheel:(NSEvent*)event{
	// A trackpad reports fractional lines; accumulate so slow scrolling still
	// moves eventually instead of rounding to nothing every time.
	static CGFloat carry = 0;

	CGFloat const delta = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / _cell_height
														  : event.scrollingDeltaY;
	carry += delta;

	int const rows = static_cast<int>( carry );
	if( rows == 0 )
		return;
	carry -= rows;

	[self scrollByRows:rows];
}

// -- Geometry ---------------------------------------------------------------

- (BOOL)isFlipped{
	return YES; // Row 0 at the top, like every terminal.
}

- (void)setFrameSize:(NSSize)size{
	[super setFrameSize:size];

	CGFloat const scroller_width = overlay_scroller_width();
	_scroller.frame = NSMakeRect( size.width - scroller_width, 0, scroller_width, size.height );

	int const columns =
		std::max( 2, static_cast<int>( ( size.width - 2 * _pad_x - scroller_width ) / _cell_width ) );
	int const rows    = std::max( 2, static_cast<int>( ( size.height - 2 * _pad_y ) / _cell_height ) );

	if( columns == _terminal->columns() && rows == _terminal->rows() )
		return;

	_terminal->resize( columns, rows );
	if( _on_resize )
		_on_resize( columns, rows, static_cast<int>( size.width ), static_cast<int>( size.height ) );
	self.needsDisplay = YES;
}

// -- Rendering --------------------------------------------------------------

- (void)drawRect:(NSRect)dirty{
	CGContextRef context = NSGraphicsContext.currentContext.CGContext;
	CGContextSetShouldAntialias( context, true );
	CGContextSetShouldSmoothFonts( context, true );

	[ns_color( _scheme.background() ) setFill];
	NSRectFill( dirty );

	Screen const& screen = _terminal->screen();

	int const first_row =
		std::clamp( static_cast<int>( ( NSMinY( dirty ) - _pad_y ) / _cell_height ), 0, screen.rows() - 1 );
	int const last_row =
		std::clamp( static_cast<int>( ( NSMaxY( dirty ) - _pad_y ) / _cell_height ), 0, screen.rows() - 1 );

	for( int row = first_row; row <= last_row; ++row )
		[self drawRow:row];

	// While scrolled into the history the cursor belongs to a screen the user is
	// not looking at, so drawing it there would be a lie.
	if( _scroll_offset == 0 )
		[self drawCursor];
}

/// `row` is a screen row; the history row it shows depends on the scroll offset.
- (void)drawRow:(int)row{
	Screen const& screen     = _terminal->screen();
	int const     history_row = [self topRow] + row;

	Line const* source = screen.history_line( history_row );
	if( source == nullptr )
		return;

	Line const&   line = *source;
	CGFloat const top  = _pad_y + row * _cell_height;

	bool const reverse_video = _terminal->modes().reverse_video;

	// The background a cell ends up with, selection included.
	auto const background_of = [&]( int index ) -> Rgb{
		if( _selection.contains( history_row, index ) )
			return _scheme.selection();

		Cell const& cell    = line[static_cast<std::size_t>( index )];
		bool const  inverse = has_flag( cell.attributes.flags, CellFlag::INVERSE ) != reverse_video;
		return inverse ? _scheme.resolve( cell.attributes.foreground, true,
										  has_flag( cell.attributes.flags, CellFlag::BOLD ) )
					   : _scheme.resolve( cell.attributes.background, false, false );
	};

	// Pass 1: background runs, merged so a full row of one colour is one fill.
	int column = 0;
	while( column < screen.columns() && column < static_cast<int>( line.size() ) ){
		Rgb const background = background_of( column );

		int run_end = column + 1;
		while( run_end < screen.columns() && run_end < static_cast<int>( line.size() )
			   && background_of( run_end ) == background ){
			++run_end;
		}

		if( background != _scheme.background() ){
			[ns_color( background ) setFill];
			NSRectFill(
				NSMakeRect( _pad_x + column * _cell_width, top, ( run_end - column ) * _cell_width, _cell_height ) );
		}
		column = run_end;
	}

	// Pass 2: the glyphs, each placed at its exact grid column.
	//
	// Deliberately not one CTLine per row: CoreText would advance by each glyph's
	// natural width, so a wide CJK glyph or any fallback font would shift every
	// following column and the text would drift out from under the cursor, which
	// is positioned on the grid.
	for( int i = 0; i < screen.columns() && i < static_cast<int>( line.size() ); ++i ){
		Cell const& cell = line[static_cast<std::size_t>( i )];
		if( has_flag( cell.attributes.flags, CellFlag::WIDE_TRAIL ) )
			continue;
		if( cell.character == U'\0' || cell.character == U' ' )
			continue;

		bool const bold    = has_flag( cell.attributes.flags, CellFlag::BOLD );
		bool const inverse = has_flag( cell.attributes.flags, CellFlag::INVERSE ) != reverse_video;

		Rgb foreground = inverse ? _scheme.resolve( cell.attributes.background, false, false )
								 : _scheme.resolve( cell.attributes.foreground, true, bold );

		// Selected cells took the selection background, so the text has to take
		// the matching foreground or it would sit on top of its own colour.
		if( _selection.contains( history_row, i ) )
			foreground = _scheme.selection_text();

		[self drawGlyphForCell:cell atColumn:i top:top color:foreground bold:bold];
	}

	// Pass 3: the decorations the glyph pass does not carry.
	for( int i = 0; i < screen.columns() && i < static_cast<int>( line.size() ); ++i ){
		Cell const&    cell  = line[static_cast<std::size_t>( i )];
		CellFlag const flags = cell.attributes.flags;
		if( has_flag( flags, CellFlag::WIDE_TRAIL ) )
			continue;

		bool const underline = has_flag( flags, CellFlag::UNDERLINE ) || has_flag( flags, CellFlag::DOUBLE_UNDERLINE );
		if( !underline && !has_flag( flags, CellFlag::STRIKEOUT ) )
			continue;

		bool const inverse = has_flag( flags, CellFlag::INVERSE ) != reverse_video;
		Rgb const  colour  = inverse ? _scheme.resolve( cell.attributes.background, false, false )
									 : _scheme.resolve( cell.attributes.foreground, true,
														has_flag( flags, CellFlag::BOLD ) );
		[ns_color( colour ) setFill];

		CGFloat const x     = _pad_x + i * _cell_width;
		CGFloat const width = has_flag( flags, CellFlag::WIDE_LEAD ) ? _cell_width * 2 : _cell_width;

		if( underline ){
			NSRectFill( NSMakeRect( x, top + _baseline + 2, width, 1 ) );
			if( has_flag( flags, CellFlag::DOUBLE_UNDERLINE ) )
				NSRectFill( NSMakeRect( x, top + _baseline + 4, width, 1 ) );
		}
		if( has_flag( flags, CellFlag::STRIKEOUT ) )
			NSRectFill( NSMakeRect( x, top + _baseline - _font.xHeight / 2, width, 1 ) );
	}
}

/// Draws the line- or block-drawing character in `cell`, if it is one.
///
/// Arms run from the cell's centre to its edge, so the same arm drawn in the
/// neighbouring cell meets it exactly and a frame reads as continuous.
- (BOOL)drawBoxOrBlockForCell:(Cell const&)cell atColumn:(int)column top:(CGFloat)top color:(Rgb)color{
	CGFloat const left  = _pad_x + column * _cell_width;
	NSRect const  cellRect = NSMakeRect( left, top, _cell_width, _cell_height );

	if( auto const block = block_glyph_for( cell.character ) ){
		NSRect  area  = cellRect;
		CGFloat alpha = 1.0;

		switch( *block ){
			case BlockGlyph::FULL:
				break;
			case BlockGlyph::UPPER_HALF:
				area.size.height = std::round( _cell_height / 2 );
				break;
			case BlockGlyph::LOWER_HALF:
				area.size.height = std::round( _cell_height / 2 );
				area.origin.y += _cell_height - area.size.height;
				break;
			case BlockGlyph::LEFT_HALF:
				area.size.width = std::round( _cell_width / 2 );
				break;
			case BlockGlyph::RIGHT_HALF:
				area.size.width = std::round( _cell_width / 2 );
				area.origin.x += _cell_width - area.size.width;
				break;
			case BlockGlyph::LIGHT_SHADE:
				alpha = 0.25;
				break;
			case BlockGlyph::MEDIUM_SHADE:
				alpha = 0.5;
				break;
			case BlockGlyph::DARK_SHADE:
				alpha = 0.75;
				break;
		}

		[ns_color( color.with_alpha( static_cast<std::uint8_t>( alpha * 255 ) ) ) setFill];
		NSRectFillUsingOperation( area, NSCompositingOperationSourceOver );
		return YES;
	}

	auto const box = box_glyph_for( cell.character );
	if( !box )
		return NO;

	// A light arm is one device pixel at 1x and stays crisp when scaled; heavy
	// and double are derived from it so the whole set looks like one family.
	CGFloat const thin   = std::max( 1.0, std::floor( _cell_height / 14.0 ) );
	CGFloat const thick  = thin * 2;
	CGFloat const gap    = thin;

	CGFloat const mid_x = std::floor( left + _cell_width / 2 );
	CGFloat const mid_y = std::floor( top + _cell_height / 2 );

	[ns_color( color ) setFill];

	// Each arm is drawn as a rect from the centre outwards. Double arms are two
	// parallel rails with a gap, and the centre stays open so joins line up.
	auto const arm = [&]( Stroke stroke, CGFloat dx, CGFloat dy ){
		if( stroke == Stroke::NONE )
			return;

		bool const    horizontal = dx != 0;
		CGFloat const weight     = stroke == Stroke::HEAVY ? thick : thin;

		CGFloat const to_x = dx < 0 ? left : left + _cell_width;
		CGFloat const to_y = dy < 0 ? top : top + _cell_height;

		auto const rail = [&]( CGFloat offset ){
			if( horizontal ){
				CGFloat const x = dx < 0 ? to_x : mid_x;
				NSRectFill( NSMakeRect( x, mid_y - weight / 2 + offset, std::abs( mid_x - to_x ) + weight, weight ) );
			}
			else{
				CGFloat const y = dy < 0 ? to_y : mid_y;
				NSRectFill( NSMakeRect( mid_x - weight / 2 + offset, y, weight, std::abs( mid_y - to_y ) + weight ) );
			}
		};

		if( stroke == Stroke::DOUBLE ){
			rail( -( gap + thin ) / 2 - thin / 2 );
			rail( ( gap + thin ) / 2 + thin / 2 );
		}
		else{
			rail( 0 );
		}
	};

	arm( box->left, -1, 0 );
	arm( box->right, 1, 0 );
	arm( box->up, 0, -1 );
	arm( box->down, 0, 1 );

	return YES;
}

/// Draws one cell's glyph anchored to its grid column.
///
/// Falls back to another font when the monospace face has no glyph, which is
/// what emoji and less common scripts need; the fallback glyph is scaled to fit
/// the cell so the grid still holds.
- (void)drawGlyphForCell:(Cell const&)cell
				atColumn:(int)column
					 top:(CGFloat)top
				   color:(Rgb)color
					bold:(bool)bold{
	// Line and block drawing is done by hand: a font's glyphs for these do not
	// tile, so frames in a TUI come apart at every join.
	if( [self drawBoxOrBlockForCell:cell atColumn:column top:top color:color] )
		return;

	unichar  utf16[2] = { 0, 0 };
	CFIndex  units    = 1;
	char32_t code     = cell.character;

	if( code < 0x10000 ){
		utf16[0] = static_cast<unichar>( code );
	}
	else{
		char32_t const bias = code - 0x10000;
		utf16[0]            = static_cast<unichar>( 0xD800 + ( bias >> 10 ) );
		utf16[1]            = static_cast<unichar>( 0xDC00 + ( bias & 0x3FF ) );
		units               = 2;
	}

	CTFontRef base = (__bridge CTFontRef)( bold ? _bold_font : _font );

	CGGlyph glyphs[2] = { 0, 0 };
	CTFontRef  font    = base;
	CFTypeRef  owned   = nullptr;

	if( !CTFontGetGlyphsForCharacters( base, utf16, glyphs, units ) ){
		// No glyph in the monospace face: ask CoreText which font has one.
		CFStringRef text = CFStringCreateWithCharacters( kCFAllocatorDefault, utf16, units );
		CTFontRef substitute = CTFontCreateForString( base, text, CFRangeMake( 0, units ) );
		CFRelease( text );

		if( substitute == nullptr )
			return;
		if( !CTFontGetGlyphsForCharacters( substitute, utf16, glyphs, units ) ){
			CFRelease( substitute );
			return;
		}
		font  = substitute;
		owned = substitute;
	}

	CGFloat const span = has_flag( cell.attributes.flags, CellFlag::WIDE_LEAD ) ? _cell_width * 2 : _cell_width;

	// Only a substituted face gets centred. The monospace font is designed to sit
	// flush at the cell origin, and centring it would put visible gaps between
	// CJK glyphs that are meant to touch.
	CGFloat offset = 0;
	if( font != base ){
		CGSize advance{};
		CTFontGetAdvancesForGlyphs( font, kCTFontOrientationHorizontal, glyphs, &advance, 1 );
		if( advance.width > 0 && advance.width < span )
			offset = ( span - advance.width ) / 2;
	}

	CGContextRef context = NSGraphicsContext.currentContext.CGContext;
	CGContextSaveGState( context );
	CGContextSetFillColorWithColor( context, ns_color( color ).CGColor );

	// The view is flipped; undo it so glyphs are not drawn upside down.
	CGContextTranslateCTM( context, _pad_x + column * _cell_width + offset, top + _baseline );
	CGContextScaleCTM( context, 1, -1 );
	CGContextSetTextMatrix( context, CGAffineTransformIdentity );

	CGPoint const position = CGPointMake( 0, 0 );
	CTFontDrawGlyphs( font, glyphs, &position, 1, context );

	CGContextRestoreGState( context );

	if( owned != nullptr )
		CFRelease( owned );
}

- (void)drawCursor{
	if( !_terminal->modes().cursor_visible )
		return;

	Screen const&      screen = _terminal->screen();
	CursorState const& cursor = screen.cursor();

	// After the last column the cursor parks one cell past the grid waiting for
	// the wrap; draw it on the last cell instead of outside the view.
	int const row    = std::clamp( cursor.row, 0, screen.rows() - 1 );
	int       column = std::clamp( cursor.column, 0, screen.columns() - 1 );

	Line const& line = screen.line( row );

	// On the trailing half of a wide glyph, back up so the block covers the
	// whole character rather than slicing it down the middle.
	if( column < static_cast<int>( line.size() ) && column > 0
		&& has_flag( line[static_cast<std::size_t>( column )].attributes.flags, CellFlag::WIDE_TRAIL ) ){
		--column;
	}

	bool const wide = column < static_cast<int>( line.size() )
					  && has_flag( line[static_cast<std::size_t>( column )].attributes.flags, CellFlag::WIDE_LEAD );

	NSRect const rect = NSMakeRect( _pad_x + column * _cell_width, _pad_y + row * _cell_height,
									wide ? _cell_width * 2 : _cell_width, _cell_height );

	if( ![self hasKeyboardFocus] ){
		[ns_color( _scheme.cursor() ) setStroke];
		NSFrameRect( NSInsetRect( rect, 0.5, 0.5 ) );
		return;
	}

	[ns_color( _scheme.cursor() ) setFill];
	NSRectFill( rect );

	// Repaint the covered glyph in the cursor-text colour, through the same
	// grid-anchored path the row uses so it cannot land a pixel off.
	if( column < static_cast<int>( line.size() ) ){
		Cell const& cell = line[static_cast<std::size_t>( column )];
		if( cell.character != U'\0' && cell.character != U' ' ){
			[self drawGlyphForCell:cell
						  atColumn:column
							   top:_pad_y + row * _cell_height
							 color:_scheme.cursor_text()
							  bold:has_flag( cell.attributes.flags, CellFlag::BOLD )];
		}
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

// -- Selection --------------------------------------------------------------

/// Turns a point in the view into the cell under it, in history space.
///
/// Clamped rather than rejected: a drag that leaves the view should keep
/// extending the selection to the nearest edge, which is what users expect.
- (Position)positionAtPoint:(NSPoint)point{
	Screen const& screen = _terminal->screen();

	int const column = std::clamp( static_cast<int>( std::floor( ( point.x - _pad_x ) / _cell_width ) ), 0,
								   screen.columns() - 1 );
	int const row    = std::clamp( static_cast<int>( std::floor( ( point.y - _pad_y ) / _cell_height ) ), 0,
								   screen.rows() - 1 );

	return Position{ [self topRow] + row, column };
}

- (void)mouseDown:(NSEvent*)event{
	NSPoint const  point = [self convertPoint:event.locationInWindow fromView:nil];
	Position const start = [self positionAtPoint:point];

	SelectionUnit unit = SelectionUnit::CHARACTER;
	if( event.clickCount == 2 )
		unit = SelectionUnit::WORD;
	else if( event.clickCount >= 3 )
		unit = SelectionUnit::LINE;

	_selection.begin( start, unit, _terminal->screen() );
	_dragging         = YES;
	self.needsDisplay = YES;
}

- (void)mouseDragged:(NSEvent*)event{
	if( !_dragging )
		return;

	NSPoint const point = [self convertPoint:event.locationInWindow fromView:nil];

	// Dragging above or below the view scrolls the history, the way a text view
	// does, so a selection can reach past what is on screen.
	if( point.y < _pad_y )
		[self scrollByRows:1];
	else if( point.y > NSMaxY( self.bounds ) - _pad_y )
		[self scrollByRows:-1];

	_selection.extend_to( [self positionAtPoint:point], _terminal->screen() );
	self.needsDisplay = YES;
}

- (void)mouseUp:(NSEvent*)event{
	_dragging = NO;
}

- (void)copy:(id)sender{
	std::string const text = _selection.text( _terminal->screen() );
	if( text.empty() )
		return;

	NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
	[pasteboard clearContents];
	[pasteboard setString:@( text.c_str() ) forType:NSPasteboardTypeString];
}

- (void)selectAll:(id)sender{
	Screen const& screen = _terminal->screen();

	_selection.begin( Position{ -screen.scrollback_size(), 0 }, SelectionUnit::CHARACTER, screen );
	_selection.extend_to( Position{ screen.rows() - 1, screen.columns() - 1 }, screen );
	self.needsDisplay = YES;
}

- (void)selectFromRow:(int)fromRow column:(int)fromColumn toRow:(int)toRow column:(int)toColumn{
	_selection.begin( Position{ fromRow, fromColumn }, SelectionUnit::CHARACTER, _terminal->screen() );
	_selection.extend_to( Position{ toRow, toColumn }, _terminal->screen() );
	self.needsDisplay = YES;
}

- (void)clearSelection{
	if( !_selection.is_active() )
		return;
	_selection.clear();
	self.needsDisplay = YES;
}

/// Greys out Copy when there is nothing selected.
- (BOOL)validateMenuItem:(NSMenuItem*)item{
	if( item.action == @selector( copy: ) )
		return _selection.is_active() && !_selection.text( _terminal->screen() ).empty();
	return YES;
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

	// Typing means the user is done reading history and done with the selection.
	[self scrollToBottom];
	[self clearSelection];

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
