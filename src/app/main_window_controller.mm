#import "app/main_window_controller.h"

#import "app/host_list_controller.h"

namespace
{

	constexpr CGFloat INITIAL_WIDTH  = 1100;
	constexpr CGFloat INITIAL_HEIGHT = 720;

	NSViewController* make_placeholder_controller(){
		NSTextField* label   = [NSTextField labelWithString:@"Select a host to connect"];
		label.textColor      = NSColor.secondaryLabelColor;
		label.font           = [NSFont systemFontOfSize:15];
		label.alignment      = NSTextAlignmentCenter;

		NSView* container = [NSView new];
		[container addSubview:label];
		label.translatesAutoresizingMaskIntoConstraints = NO;
		[NSLayoutConstraint activateConstraints:@[
			[label.centerXAnchor constraintEqualToAnchor:container.centerXAnchor],
			[label.centerYAnchor constraintEqualToAnchor:container.centerYAnchor],
		]];

		NSViewController* controller = [NSViewController new];
		controller.view              = container;
		return controller;
	}

} // namespace

@implementation ArTermMainWindowController{
	ArTermHostListController* _hosts;
}

- (instancetype)init{
	NSWindow* window = [[NSWindow alloc]
		initWithContentRect:NSMakeRect( 0, 0, INITIAL_WIDTH, INITIAL_HEIGHT )
				  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
							NSWindowStyleMaskResizable
					backing:NSBackingStoreBuffered
					  defer:NO];
	window.title                  = @"ArTerm";
	window.minSize                = NSMakeSize( 700, 480 );
	window.titlebarAppearsTransparent = NO;
	window.frameAutosaveName      = @"main-window";
	[window center];

	if( ( self = [super initWithWindow:window] ) ){
		_hosts = [ArTermHostListController new];

		NSSplitViewController* split = [NSSplitViewController new];

		NSSplitViewItem* sidebar        = [NSSplitViewItem sidebarWithViewController:_hosts];
		sidebar.minimumThickness        = 180;
		sidebar.maximumThickness        = 360;
		sidebar.canCollapse             = YES;

		NSSplitViewItem* content = [NSSplitViewItem splitViewItemWithViewController:make_placeholder_controller()];
		content.minimumThickness = 400;

		[split addSplitViewItem:sidebar];
		[split addSplitViewItem:content];

		window.contentViewController = split;
	}
	return self;
}

@end
