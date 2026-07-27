#import "app/session_tabs_controller.h"

#import "app/files_view_controller.h"
#import "app/session_view_controller.h"

#include <optional>
#include <utility>

using namespace arterm;

namespace
{

	constexpr CGFloat TAB_BAR_HEIGHT = 30;

} // namespace

@interface ArTermSessionTabsController () <NSTabViewDelegate>
@end

@implementation ArTermSessionTabsController{
	NSTabView*   _tabs;
	NSView*      _bar;
	NSPopUpButton* _add;
	NSStackView* _tab_buttons;

	std::function<std::optional<ssh::HostProfile>()> _provider;
}

- (void)setProfileProvider:(std::function<std::optional<arterm::ssh::HostProfile>()>)provider{
	_provider = std::move( provider );
}

- (BOOL)isEmpty{
	return _tabs.numberOfTabViewItems == 0;
}

- (void)loadView{
	_tab_buttons             = [NSStackView new];
	_tab_buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
	_tab_buttons.spacing     = 4;
	_tab_buttons.alignment   = NSLayoutAttributeCenterY;

	// A pull-down NSPopUpButton rather than a plain button plus a hand-rolled
	// popUpMenuPositioningItem: the framework owns the menu placement and
	// tracking, which is what makes the first click reliably open it.
	_add                 = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:YES];
	_add.bezelStyle      = NSBezelStyleTexturedRounded;
	_add.toolTip         = @"Open a new tab for the selected host";
	[_add.widthAnchor constraintEqualToConstant:46].active = YES;

	// Item 0 of a pull-down is the button's own label and is never chosen.
	[_add addItemWithTitle:@"+"];
	[_add.menu addItemWithTitle:@"New SSH Session" action:@selector( newTerminalTab: ) keyEquivalent:@"t"].target =
		self;
	[_add.menu addItemWithTitle:@"New File Browser" action:@selector( newFilesTab: ) keyEquivalent:@"b"].target =
		self;

	_bar = [NSView new];
	[_bar addSubview:_tab_buttons];
	[_bar addSubview:_add];

	_tab_buttons.translatesAutoresizingMaskIntoConstraints = NO;
	_add.translatesAutoresizingMaskIntoConstraints         = NO;
	[NSLayoutConstraint activateConstraints:@[
		[_bar.heightAnchor constraintEqualToConstant:TAB_BAR_HEIGHT],
		[_add.leadingAnchor constraintEqualToAnchor:_bar.leadingAnchor constant:8],
		[_add.centerYAnchor constraintEqualToAnchor:_bar.centerYAnchor],
		[_tab_buttons.leadingAnchor constraintEqualToAnchor:_add.trailingAnchor constant:8],
		[_tab_buttons.trailingAnchor constraintLessThanOrEqualToAnchor:_bar.trailingAnchor constant:-8],
		[_tab_buttons.centerYAnchor constraintEqualToAnchor:_bar.centerYAnchor],
	]];

	// The tab view carries the content only; the strip above is ours, so a tab
	// can show which kind it is and close itself.
	_tabs            = [NSTabView new];
	_tabs.tabViewType = NSNoTabsNoBorder;
	_tabs.delegate    = self;

	NSStackView* column = [NSStackView stackViewWithViews:@[ _bar, _tabs ]];
	column.orientation  = NSUserInterfaceLayoutOrientationVertical;
	column.spacing      = 0;
	column.alignment    = NSLayoutAttributeLeading;
	column.distribution = NSStackViewDistributionFill;

	_bar.translatesAutoresizingMaskIntoConstraints  = NO;
	_tabs.translatesAutoresizingMaskIntoConstraints = NO;
	[NSLayoutConstraint activateConstraints:@[
		[_bar.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
		[_bar.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
		[_tabs.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
		[_tabs.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
	]];

	self.view = column;
}

// -- Opening tabs -----------------------------------------------------------

- (void)openTerminalForProfile:(arterm::ssh::HostProfile)profile{
	NSString* const title = @( profile.display_name().c_str() );

	ArTermSessionViewController* session =
		[[ArTermSessionViewController alloc] initWithProfile:std::move( profile )];

	[self addTabWithController:session title:title symbol:@"terminal"];
}

- (void)openFilesForProfile:(arterm::ssh::HostProfile)profile{
	NSString* const title = [NSString stringWithFormat:@"%s - files", profile.display_name().c_str()];

	ArTermFilesViewController* files = [[ArTermFilesViewController alloc] initWithProfile:std::move( profile )];

	[self addTabWithController:files title:title symbol:@"folder"];
}

- (void)addTabWithController:(NSViewController*)controller title:(NSString*)title symbol:(NSString*)symbol{
	NSTabViewItem* item = [NSTabViewItem tabViewItemWithViewController:controller];
	item.label          = title;
	[_tabs addTabViewItem:item];
	[_tabs selectTabViewItem:item];

	[self rebuildTabButtons];
}

/// The host + acts on, or nothing after telling the user why.
- (std::optional<arterm::ssh::HostProfile>)profileForNewTab{
	auto profile = _provider ? _provider() : std::nullopt;
	if( profile )
		return profile;

	NSAlert* alert        = [NSAlert new];
	alert.messageText     = @"No host selected";
	alert.informativeText = @"Pick a host in the sidebar first, then use + to open a tab for it.";
	[alert runModal];
	return std::nullopt;
}

- (void)newTerminalTab:(id)sender{
	if( auto profile = [self profileForNewTab] )
		[self openTerminalForProfile:std::move( *profile )];
}

- (void)newFilesTab:(id)sender{
	if( auto profile = [self profileForNewTab] )
		[self openFilesForProfile:std::move( *profile )];
}

// -- The tab strip ----------------------------------------------------------

- (void)rebuildTabButtons{
	for( NSView* view in [_tab_buttons.arrangedSubviews copy] )
		[_tab_buttons removeArrangedSubview:view], [view removeFromSuperview];

	NSTabViewItem* current = _tabs.selectedTabViewItem;

	for( NSTabViewItem* item in _tabs.tabViewItems ){
		NSButton* button = [NSButton buttonWithTitle:item.label target:self action:@selector( tabButtonClicked: )];
		button.bezelStyle = NSBezelStyleTexturedRounded;
		button.tag        = static_cast<NSInteger>( [_tabs indexOfTabViewItem:item] );
		button.state      = item == current ? NSControlStateValueOn : NSControlStateValueOff;

		// Right-click closes; a close box on every tab would crowd the strip at
		// the sizes these labels need.
		NSMenu* menu = [NSMenu new];
		[menu addItemWithTitle:@"Close Tab" action:@selector( closeTabFromMenu: ) keyEquivalent:@""].target = self;
		menu.itemArray.firstObject.tag = button.tag;
		button.menu                    = menu;

		[_tab_buttons addArrangedSubview:button];
	}
}

- (void)tabButtonClicked:(NSButton*)sender{
	if( sender.tag >= 0 && sender.tag < _tabs.numberOfTabViewItems ){
		[_tabs selectTabViewItemAtIndex:sender.tag];
		[self rebuildTabButtons];
	}
}

- (void)closeTabFromMenu:(NSMenuItem*)sender{
	[self closeTabAtIndex:sender.tag];
}

- (void)closeCurrentTab{
	if( _tabs.selectedTabViewItem != nil )
		[self closeTabAtIndex:[_tabs indexOfTabViewItem:_tabs.selectedTabViewItem]];
}

- (void)closeTabAtIndex:(NSInteger)index{
	if( index < 0 || index >= _tabs.numberOfTabViewItems )
		return;

	NSTabViewItem* item = [_tabs tabViewItemAtIndex:index];
	[_tabs removeTabViewItem:item];

	// Removing the item drops the last reference to its view controller, whose
	// dealloc shuts the session down.
	[self rebuildTabButtons];
}

- (void)tabView:(NSTabView*)tabView didSelectTabViewItem:(NSTabViewItem*)item{
	[self rebuildTabButtons];

	if( item.viewController != nil && self.view.window != nil )
		self.view.window.title = item.label;
}

@end
