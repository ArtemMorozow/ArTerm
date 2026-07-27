#import "app/tab_bar_view.h"

#include <algorithm>

namespace
{

	constexpr CGFloat BAR_HEIGHT   = 32;
	constexpr CGFloat TAB_MIN      = 120;
	constexpr CGFloat TAB_MAX      = 220;
	constexpr CGFloat CLOSE_SIZE   = 16;
	constexpr CGFloat ADD_WIDTH    = 30;

} // namespace

@implementation ArTermTabDescriptor
@end

/// One tab. A view rather than an NSButton so the title and the close box can
/// be hit separately - a click near the × must close, not switch.
@interface ArTermTabItemView : NSView
@property( nonatomic ) NSInteger index;
@property( nonatomic ) BOOL      selected;
@property( nonatomic, weak ) ArTermTabBarView* bar;
@end

@implementation ArTermTabItemView{
	NSTextField*         _label;
	NSButton*            _close;
	NSImageView*         _icon;
	NSProgressIndicator* _spinner;
	NSTrackingArea*      _tracking;
	BOOL                 _hovered;
}

- (instancetype)initWithDescriptor:(ArTermTabDescriptor*)descriptor{
	if( ( self = [super initWithFrame:NSZeroRect] ) ){
		self.wantsLayer            = YES;
		self.layer.cornerRadius    = 6;

		_icon                = [NSImageView new];
		_icon.image          = [NSImage imageWithSystemSymbolName:descriptor.symbol accessibilityDescription:nil];
		_icon.contentTintColor = NSColor.secondaryLabelColor;

		_spinner                      = [NSProgressIndicator new];
		_spinner.style                = NSProgressIndicatorStyleSpinning;
		_spinner.controlSize          = NSControlSizeSmall;
		_spinner.displayedWhenStopped = NO;

		_label                = [NSTextField labelWithString:descriptor.title];
		_label.font           = [NSFont systemFontOfSize:12];
		_label.lineBreakMode  = NSLineBreakByTruncatingTail;
		[_label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
										 forOrientation:NSLayoutConstraintOrientationHorizontal];

		_close        = [NSButton buttonWithTitle:@"" target:self action:@selector( closeClicked: )];
		_close.image  = [NSImage imageWithSystemSymbolName:@"xmark" accessibilityDescription:@"Close tab"];
		_close.bezelStyle = NSBezelStyleShadowlessSquare;
		_close.bordered   = NO;
		_close.toolTip    = @"Close tab";
		// The close box only appears on hover or on the active tab, the way every
		// browser does it, so an inactive strip stays quiet.
		_close.alphaValue = 0;

		for( NSView* view in @[ _icon, _spinner, _label, _close ] ){
			view.translatesAutoresizingMaskIntoConstraints = NO;
			[self addSubview:view];
		}

		[NSLayoutConstraint activateConstraints:@[
			[_icon.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
			[_icon.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
			[_icon.widthAnchor constraintEqualToConstant:14],
			[_icon.heightAnchor constraintEqualToConstant:14],

			[_spinner.centerXAnchor constraintEqualToAnchor:_icon.centerXAnchor],
			[_spinner.centerYAnchor constraintEqualToAnchor:_icon.centerYAnchor],
			[_spinner.widthAnchor constraintEqualToConstant:14],
			[_spinner.heightAnchor constraintEqualToConstant:14],

			[_label.leadingAnchor constraintEqualToAnchor:_icon.trailingAnchor constant:6],
			[_label.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
			[_label.trailingAnchor constraintEqualToAnchor:_close.leadingAnchor constant:-4],

			[_close.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-6],
			[_close.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
			[_close.widthAnchor constraintEqualToConstant:CLOSE_SIZE],
			[_close.heightAnchor constraintEqualToConstant:CLOSE_SIZE],
		]];

		if( descriptor.busy ){
			_icon.hidden = YES;
			[_spinner startAnimation:nil];
		}
	}
	return self;
}

- (void)setSelected:(BOOL)selected{
	_selected = selected;
	[self updateAppearance];
}

- (void)updateAppearance{
	self.layer.backgroundColor =
		_selected ? NSColor.controlBackgroundColor.CGColor
				  : ( _hovered ? [NSColor.controlBackgroundColor colorWithAlphaComponent:0.5].CGColor
							   : NSColor.clearColor.CGColor );

	_label.textColor  = _selected ? NSColor.labelColor : NSColor.secondaryLabelColor;
	_close.alphaValue = ( _selected || _hovered ) ? 1 : 0;
}

- (void)updateTrackingAreas{
	[super updateTrackingAreas];
	if( _tracking != nil )
		[self removeTrackingArea:_tracking];

	_tracking = [[NSTrackingArea alloc]
		initWithRect:self.bounds
			 options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
			   owner:self
			userInfo:nil];
	[self addTrackingArea:_tracking];
}

- (void)mouseEntered:(NSEvent*)event{
	_hovered = YES;
	[self updateAppearance];
}

- (void)mouseExited:(NSEvent*)event{
	_hovered = NO;
	[self updateAppearance];
}

- (void)mouseDown:(NSEvent*)event{
	[self.bar.delegate tabBarDidSelectIndex:self.index];
}

- (void)closeClicked:(id)sender{
	[self.bar.delegate tabBarDidCloseIndex:self.index];
}

/// Middle-click closes, as in a browser.
- (void)otherMouseUp:(NSEvent*)event{
	if( event.buttonNumber == 2 )
		[self.bar.delegate tabBarDidCloseIndex:self.index];
}

@end

@implementation ArTermTabBarView{
	NSScrollView* _scroll;
	NSView*       _strip;
	NSButton*     _add;

	NSArray<ArTermTabDescriptor*>* _tabs;
	NSInteger                      _selected;
}

- (instancetype)initWithFrame:(NSRect)frame{
	if( ( self = [super initWithFrame:frame] ) ){
		self.wantsLayer            = YES;
		self.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;

		_strip = [NSView new];

		_scroll                       = [NSScrollView new];
		_scroll.documentView          = _strip;
		_scroll.hasHorizontalScroller = NO;
		_scroll.hasVerticalScroller   = NO;
		_scroll.drawsBackground       = NO;

		_add            = [NSButton buttonWithTitle:@"" target:self action:@selector( addClicked: )];
		_add.image      = [NSImage imageWithSystemSymbolName:@"plus" accessibilityDescription:@"New tab"];
		_add.bezelStyle = NSBezelStyleShadowlessSquare;
		_add.bordered   = NO;
		_add.toolTip    = @"New tab";

		_scroll.translatesAutoresizingMaskIntoConstraints = NO;
		_add.translatesAutoresizingMaskIntoConstraints    = NO;
		[self addSubview:_scroll];
		[self addSubview:_add];

		[NSLayoutConstraint activateConstraints:@[
			[self.heightAnchor constraintEqualToConstant:BAR_HEIGHT],

			[_scroll.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:6],
			[_scroll.topAnchor constraintEqualToAnchor:self.topAnchor constant:2],
			[_scroll.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-2],
			[_scroll.trailingAnchor constraintEqualToAnchor:_add.leadingAnchor constant:-4],

			[_add.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8],
			[_add.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
			[_add.widthAnchor constraintEqualToConstant:ADD_WIDTH],
			[_add.heightAnchor constraintEqualToConstant:22],
		]];
	}
	return self;
}

- (void)setTabs:(NSArray<ArTermTabDescriptor*>*)tabs selectedIndex:(NSInteger)selected{
	_tabs     = [tabs copy];
	_selected = selected;

	// The items are positioned by hand from the scroll view's bounds, which are
	// only meaningful once the constraints have run - so the work happens in
	// -layout rather than here, where the bounds can still be zero.
	self.needsLayout = YES;
}

- (void)layout{
	[super layout];

	for( NSView* view in [_strip.subviews copy] )
		[view removeFromSuperview];

	CGFloat const height    = NSHeight( _scroll.bounds );
	CGFloat const available = NSWidth( _scroll.bounds );
	if( height <= 0 )
		return;

	// Tabs share the available width down to a floor, past which the strip
	// scrolls rather than shrinking them into illegibility.
	CGFloat const width =
		_tabs.count == 0 ? TAB_MIN : std::clamp( available / static_cast<CGFloat>( _tabs.count ), TAB_MIN, TAB_MAX );

	CGFloat x = 0;
	for( NSUInteger i = 0; i < _tabs.count; ++i ){
		ArTermTabItemView* item = [[ArTermTabItemView alloc] initWithDescriptor:_tabs[i]];
		item.index              = static_cast<NSInteger>( i );
		item.bar                = self;
		item.frame              = NSMakeRect( x, 0, width - 2, height );
		item.selected           = static_cast<NSInteger>( i ) == _selected;
		[_strip addSubview:item];
		x += width;
	}

	_strip.frame = NSMakeRect( 0, 0, std::max( x, available ), height );
}

- (void)addClicked:(id)sender{
	[self.delegate tabBarDidRequestNewTab];
}

@end
