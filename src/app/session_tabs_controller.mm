#import "app/session_tabs_controller.h"

#import "app/files_view_controller.h"
#import "app/new_tab_controller.h"
#import "app/session_view_controller.h"
#import "app/tab_bar_view.h"

#include <utility>

using namespace arterm;

@interface ArTermSessionTabsController () <ArTermTabBarDelegate>
@end

@implementation ArTermSessionTabsController{
	ArTermTabBarView* _bar;
	NSView*           _content;

	/// One entry per tab, parallel to the bar's descriptors.
	NSMutableArray<NSViewController*>*     _controllers;
	NSMutableArray<ArTermTabDescriptor*>*  _descriptors;
	NSInteger                              _selected;

	std::function<std::vector<ssh::HostProfile>()> _hosts_provider;
}

- (instancetype)init{
	if( ( self = [super initWithNibName:nil bundle:nil] ) ){
		_controllers = [NSMutableArray new];
		_descriptors = [NSMutableArray new];
		_selected    = -1;
	}
	return self;
}

- (void)setHostsProvider:(std::function<std::vector<arterm::ssh::HostProfile>()>)provider{
	_hosts_provider = std::move( provider );
}

- (BOOL)isEmpty{
	return _controllers.count == 0;
}

- (void)loadView{
	_bar          = [[ArTermTabBarView alloc] initWithFrame:NSZeroRect];
	_bar.delegate = self;

	// The controller's own view is nothing but the session area. The tab strip
	// goes into the window's titlebar instead - see -titlebarAccessory.
	_content = [NSView new];
	self.view = _content;
}

/// The tab strip, wrapped for installation under the window's title bar.
///
/// It lives there rather than as a sibling of the session area because a strip
/// sitting beside the layer-backed terminal was ordered behind it by AppKit:
/// the bar kept its frame and went on taking clicks, but never drew. The
/// titlebar is also where a browser-style strip belongs on macOS.
- (NSTitlebarAccessoryViewController*)titlebarAccessory{
	NSTitlebarAccessoryViewController* accessory = [NSTitlebarAccessoryViewController new];
	accessory.view            = _bar;
	accessory.layoutAttribute = NSLayoutAttributeBottom;
	accessory.fullScreenMinHeight = NSHeight( _bar.frame );
	return accessory;
}

- (void)viewDidLoad{
	[super viewDidLoad];

	// The start page is created here rather than in viewDidAppear: appearance
	// callbacks do not fire for a window that is never ordered front, and the
	// tab area must never be empty regardless.
	if( _controllers.count == 0 )
		[self openNewTabPage];
}

// -- Tabs -------------------------------------------------------------------

- (void)addController:(NSViewController*)controller
				title:(NSString*)title
			   symbol:(NSString*)symbol
				 busy:(BOOL)busy{
	ArTermTabDescriptor* descriptor = [ArTermTabDescriptor new];
	descriptor.title                = title;
	descriptor.symbol               = symbol;
	descriptor.busy                 = busy;

	[_controllers addObject:controller];
	[_descriptors addObject:descriptor];

	[self selectIndex:static_cast<NSInteger>( _controllers.count ) - 1];
	[self refreshBar];
}

- (void)openNewTabPage{
	ArTermNewTabController* page = [ArTermNewTabController new];

	if( _hosts_provider )
		[page setHosts:_hosts_provider()];

	__weak ArTermSessionTabsController* weak_self = self;
	__weak ArTermNewTabController*      weak_page = page;

	[page setOpenHandler:[weak_self, weak_page]( ssh::HostProfile profile, bool files ){
		ArTermSessionTabsController* strong_self = weak_self;
		if( strong_self == nil )
			return;

		// The start page turns into the session it was pointed at, the way a
		// browser's new tab becomes the page you navigate to.
		NSInteger const index = [strong_self indexOfController:weak_page];
		if( files )
			[strong_self replaceIndex:index withFilesFor:std::move( profile )];
		else
			[strong_self replaceIndex:index withTerminalFor:std::move( profile )];
	}];

	[self addController:page title:@"New Tab" symbol:@"plus.square" busy:NO];
}

- (NSInteger)indexOfController:(NSViewController*)controller{
	if( controller == nil )
		return -1;
	NSUInteger const index = [_controllers indexOfObject:controller];
	return index == NSNotFound ? -1 : static_cast<NSInteger>( index );
}

- (void)replaceIndex:(NSInteger)index withTerminalFor:(arterm::ssh::HostProfile)profile{
	NSString* const title = @( profile.display_name().c_str() );

	ArTermSessionViewController* session =
		[[ArTermSessionViewController alloc] initWithProfile:std::move( profile )];

	[self replaceIndex:index with:session title:title symbol:@"terminal"];
}

- (void)replaceIndex:(NSInteger)index withFilesFor:(arterm::ssh::HostProfile)profile{
	NSString* const title = @( profile.display_name().c_str() );

	ArTermFilesViewController* files = [[ArTermFilesViewController alloc] initWithProfile:std::move( profile )];

	[self replaceIndex:index with:files title:title symbol:@"folder"];
}

- (void)replaceIndex:(NSInteger)index
				with:(NSViewController*)controller
			   title:(NSString*)title
			  symbol:(NSString*)symbol{
	if( index < 0 || index >= static_cast<NSInteger>( _controllers.count ) ){
		[self addController:controller title:title symbol:symbol busy:NO];
		return;
	}

	_controllers[static_cast<NSUInteger>( index )] = controller;

	ArTermTabDescriptor* descriptor = _descriptors[static_cast<NSUInteger>( index )];
	descriptor.title                = title;
	descriptor.symbol               = symbol;

	[self selectIndex:index];
	[self refreshBar];
}

- (void)openTerminalForProfile:(arterm::ssh::HostProfile)profile{
	NSString* const title = @( profile.display_name().c_str() );

	ArTermSessionViewController* session =
		[[ArTermSessionViewController alloc] initWithProfile:std::move( profile )];

	[self addController:session title:title symbol:@"terminal" busy:NO];
}

- (void)openFilesForProfile:(arterm::ssh::HostProfile)profile{
	NSString* const title = @( profile.display_name().c_str() );

	ArTermFilesViewController* files = [[ArTermFilesViewController alloc] initWithProfile:std::move( profile )];

	[self addController:files title:title symbol:@"folder" busy:NO];
}

- (void)selectIndex:(NSInteger)index{
	if( index < 0 || index >= static_cast<NSInteger>( _controllers.count ) )
		return;

	_selected = index;

	for( NSView* view in [_content.subviews copy] )
		[view removeFromSuperview];

	NSViewController* controller = _controllers[static_cast<NSUInteger>( index )];
	NSView*           view       = controller.view;

	view.translatesAutoresizingMaskIntoConstraints = NO;
	[_content addSubview:view];
	[NSLayoutConstraint activateConstraints:@[
		[view.topAnchor constraintEqualToAnchor:_content.topAnchor],
		[view.leadingAnchor constraintEqualToAnchor:_content.leadingAnchor],
		[view.trailingAnchor constraintEqualToAnchor:_content.trailingAnchor],
		[view.bottomAnchor constraintEqualToAnchor:_content.bottomAnchor],
	]];

	// Parenting the controller is what gives it viewDidAppear and keeps the
	// responder chain intact for the menu actions.
	if( ![self.childViewControllers containsObject:controller] )
		[self addChildViewController:controller];

	if( self.view.window != nil )
		self.view.window.title = _descriptors[static_cast<NSUInteger>( index )].title;
}

- (void)refreshBar{
	[_bar setTabs:_descriptors selectedIndex:_selected];
}

- (void)closeCurrentTab{
	[self tabBarDidCloseIndex:_selected];
}

- (void)newTab{
	[self openNewTabPage];
}

// -- ArTermTabBarDelegate ---------------------------------------------------

- (void)tabBarDidSelectIndex:(NSInteger)index{
	[self selectIndex:index];
	[self refreshBar];
}

- (void)tabBarDidCloseIndex:(NSInteger)index{
	if( index < 0 || index >= static_cast<NSInteger>( _controllers.count ) )
		return;

	NSViewController* controller = _controllers[static_cast<NSUInteger>( index )];
	[controller.view removeFromSuperview];
	[controller removeFromParentViewController];

	[_controllers removeObjectAtIndex:static_cast<NSUInteger>( index )];
	[_descriptors removeObjectAtIndex:static_cast<NSUInteger>( index )];
	// Dropping the last reference runs the controller's dealloc, which shuts its
	// session down.

	if( _controllers.count == 0 ){
		_selected = -1;
		[self openNewTabPage];
		return;
	}

	[self selectIndex:std::min( index, static_cast<NSInteger>( _controllers.count ) - 1 )];
	[self refreshBar];
}

- (void)tabBarDidRequestNewTab{
	[self openNewTabPage];
}

@end
