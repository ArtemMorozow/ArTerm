#import "app/main_window_controller.h"

#include <optional>
#include <utility>

#import "app/host_list_controller.h"
#import "app/terminal_view.h"
#import "app/session_tabs_controller.h"

namespace
{

	constexpr CGFloat INITIAL_WIDTH  = 1100;
	constexpr CGFloat INITIAL_HEIGHT = 720;

} // namespace

@implementation ArTermMainWindowController{
	ArTermHostListController*     _hosts;
	ArTermSessionTabsController*  _sessions;
	NSSplitViewController*        _split;
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

		_sessions = [ArTermSessionTabsController new];

		__weak ArTermMainWindowController* weak_self = self;
		[_hosts setConnectHandler:[weak_self]( arterm::ssh::HostProfile profile ){
			[weak_self openSessionWithProfile:std::move( profile )];
		}];

		// + asks the sidebar which host is selected, so a new tab lands on the
		// host the user is looking at rather than the one last connected.
		__weak ArTermHostListController* weak_hosts = _hosts;
		[_sessions setProfileProvider:[weak_hosts]() -> std::optional<arterm::ssh::HostProfile>{
			ArTermHostListController* hosts = weak_hosts;
			return hosts != nil ? [hosts selectedProfile] : std::nullopt;
		}];

		NSSplitViewController* split = [NSSplitViewController new];

		NSSplitViewItem* sidebar        = [NSSplitViewItem sidebarWithViewController:_hosts];
		sidebar.minimumThickness        = 180;
		sidebar.maximumThickness        = 360;
		sidebar.canCollapse             = YES;

		NSSplitViewItem* content = [NSSplitViewItem splitViewItemWithViewController:_sessions];
		content.minimumThickness = 400;

		[split addSplitViewItem:sidebar];
		[split addSplitViewItem:content];

		_split                       = split;
		window.contentViewController = split;
	}
	return self;
}

// Menu actions arrive here through the responder chain and route to the
// sidebar, which owns the store.
- (void)newHost:(id)sender{
	[_hosts createHost];
}

- (void)importHosts:(id)sender{
	[_hosts importFromSSHConfig];
}

- (void)openSessionWithProfile:(arterm::ssh::HostProfile)profile{
	[_sessions openTerminalForProfile:std::move( profile )];
}

- (void)closeTab:(id)sender{
	[_sessions closeCurrentTab];
}

- (void)newFileBrowser:(id)sender{
	if( auto profile = [_hosts selectedProfile] )
		[_sessions openFilesForProfile:std::move( *profile )];
}

@end
