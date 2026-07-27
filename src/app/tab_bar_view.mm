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

	_label.textColor = _selected ? NSColor.labelColor : NSColor.secondaryLabelColor;
	// Always visible: an inactive tab that only reveals its close box on hover
	// leaves no way to shut it for anyone who does not know to hover.
	_close.contentTintColor = ( _selected || _hovered ) ? NSColor.labelColor : NSColor.tertiaryLabelColor;
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
	NSStackView*  _strip;
	NSButton*     _add;
}

- (instancetype)initWithFrame:(NSRect)frame{
	if( ( self = [super initWithFrame:frame] ) ){
		// Off before the height constraint below is added: while the autoresizing
		// mask is still being translated, the zero frame it comes from generates a
		// height-0 constraint that conflicts with ours, and the one Auto Layout
		// breaks to resolve it can be ours - which collapses the bar to nothing.
		self.translatesAutoresizingMaskIntoConstraints = NO;

		// No background of its own: as a titlebar accessory the strip sits on the
		// window's own material, and painting over it would break the blur.

		// A stack view rather than frames computed by hand: the first attempt did
		// the arithmetic in -layout, which ran once while the bounds were still
		// zero, bailed out, and was never asked again - so the strip stayed empty
		// and the bar looked like it had vanished.
		_strip = [NSStackView new];
		// The document view of a scroll view is positioned by constraints only
		// once its autoresizing mask stops being translated; without this the
		// stack keeps the zero frame it was created with.
		_strip.translatesAutoresizingMaskIntoConstraints = NO;
		_strip.orientation = NSUserInterfaceLayoutOrientationHorizontal;
		_strip.spacing     = 2;
		_strip.alignment   = NSLayoutAttributeCenterY;
		_strip.detachesHiddenViews = NO;

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

			// The strip is as tall as the visible area and grows rightwards, which
			// is what makes it scroll once the tabs stop fitting.
			[_strip.leadingAnchor constraintEqualToAnchor:_scroll.contentView.leadingAnchor],
			[_strip.topAnchor constraintEqualToAnchor:_scroll.contentView.topAnchor],
			[_strip.heightAnchor constraintEqualToAnchor:_scroll.contentView.heightAnchor],
		]];
	}
	return self;
}

- (void)setTabs:(NSArray<ArTermTabDescriptor*>*)tabs selectedIndex:(NSInteger)selected{
	for( NSView* view in [_strip.arrangedSubviews copy] ){
		[_strip removeArrangedSubview:view];
		[view removeFromSuperview];
	}

	for( NSUInteger i = 0; i < tabs.count; ++i ){
		ArTermTabItemView* item = [[ArTermTabItemView alloc] initWithDescriptor:tabs[i]];
		item.index              = static_cast<NSInteger>( i );
		item.bar                = self;
		item.selected           = static_cast<NSInteger>( i ) == selected;

		item.translatesAutoresizingMaskIntoConstraints = NO;
		[_strip addArrangedSubview:item];

		// A floor and a ceiling: tabs share the width but never shrink to
		// illegibility - past that the strip scrolls instead.
		[item.widthAnchor constraintGreaterThanOrEqualToConstant:TAB_MIN].active = YES;
		[item.widthAnchor constraintLessThanOrEqualToConstant:TAB_MAX].active    = YES;
		[item.heightAnchor constraintEqualToAnchor:_strip.heightAnchor].active   = YES;
	}
}

- (void)addClicked:(id)sender{
	[self.delegate tabBarDidRequestNewTab];
}

@end
