#import "app/main_window_controller.h"

#include <utility>

#import "app/host_list_controller.h"
#import "app/session_view_controller.h"

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
	NSSplitViewController*    _split;
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

		__weak ArTermMainWindowController* weak_self = self;
		[_hosts setConnectHandler:[weak_self]( arterm::ssh::HostProfile profile ){
			[weak_self openSessionWithProfile:std::move( profile )];
		}];

		NSSplitViewController* split = [NSSplitViewController new];

		NSSplitViewItem* sidebar        = [NSSplitViewItem sidebarWithViewController:_hosts];
		sidebar.minimumThickness        = 180;
		sidebar.maximumThickness        = 360;
		sidebar.canCollapse             = YES;

		NSSplitViewItem* content = [NSSplitViewItem splitViewItemWithViewController:make_placeholder_controller()];
		content.minimumThickness = 400;

		[split addSplitViewItem:sidebar];
		[split addSplitViewItem:content];

		_split                       = split;
		window.contentViewController = split;
	}
	return self;
}

- (void)openSessionWithProfile:(arterm::ssh::HostProfile)profile{
	NSString* const title = @( profile.display_name().c_str() );

	// One session at a time for now; replacing the item deallocates the previous
	// controller, which shuts its session down.
	ArTermSessionViewController* session =
		[[ArTermSessionViewController alloc] initWithProfile:std::move( profile )];

	NSSplitViewItem* content = [NSSplitViewItem splitViewItemWithViewController:session];
	content.minimumThickness = 400;

	[_split removeSplitViewItem:_split.splitViewItems.lastObject];
	[_split addSplitViewItem:content];

	self.window.title = title;
}

@end
