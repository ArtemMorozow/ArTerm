#import "app/app_delegate.h"

#import "app/main_window_controller.h"

namespace
{

	NSMenuItem* item_with_submenu( NSString* title ){
		NSMenuItem* item = [NSMenuItem new];
		item.submenu     = [[NSMenu alloc] initWithTitle:title];
		return item;
	}

	NSMenuItem* action_item( NSString* title, SEL action, NSString* key,
							 NSEventModifierFlags modifiers = NSEventModifierFlagCommand ){
		NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
		item.keyEquivalentModifierMask = modifiers;
		return item;
	}

	/// The menu bar, built in code: ArTerm has no nib files.
	NSMenu* build_main_menu(){
		NSMenu* bar = [NSMenu new];

		NSMenuItem* application = item_with_submenu( @"ArTerm" );
		[application.submenu addItem:action_item( @"About ArTerm", @selector( orderFrontStandardAboutPanel: ), @"" )];
		[application.submenu addItem:NSMenuItem.separatorItem];
		[application.submenu addItem:action_item( @"Hide ArTerm", @selector( hide: ), @"h" )];
		[application.submenu addItem:action_item( @"Hide Others", @selector( hideOtherApplications: ), @"h",
												  NSEventModifierFlagCommand | NSEventModifierFlagOption )];
		[application.submenu addItem:action_item( @"Show All", @selector( unhideAllApplications: ), @"" )];
		[application.submenu addItem:NSMenuItem.separatorItem];
		[application.submenu addItem:action_item( @"Quit ArTerm", @selector( terminate: ), @"q" )];
		[bar addItem:application];

		NSMenuItem* file = item_with_submenu( @"File" );
		[file.submenu addItem:action_item( @"New Host…", @selector( newHost: ), @"n" )];
		[file.submenu addItem:action_item( @"Import Hosts from ~/.ssh/config", @selector( importHosts: ), @"" )];
		[file.submenu addItem:NSMenuItem.separatorItem];
		[file.submenu addItem:action_item( @"Close", @selector( performClose: ), @"w" )];
		[bar addItem:file];

		NSMenuItem* edit = item_with_submenu( @"Edit" );
		[edit.submenu addItem:action_item( @"Undo", @selector( undo: ), @"z" )];
		[edit.submenu addItem:action_item( @"Redo", @selector( redo: ), @"Z" )];
		[edit.submenu addItem:NSMenuItem.separatorItem];
		[edit.submenu addItem:action_item( @"Cut", @selector( cut: ), @"x" )];
		[edit.submenu addItem:action_item( @"Copy", @selector( copy: ), @"c" )];
		[edit.submenu addItem:action_item( @"Paste", @selector( paste: ), @"v" )];
		[edit.submenu addItem:action_item( @"Select All", @selector( selectAll: ), @"a" )];
		[bar addItem:edit];

		NSMenuItem* window        = item_with_submenu( @"Window" );
		[window.submenu addItem:action_item( @"Minimize", @selector( performMiniaturize: ), @"m" )];
		[window.submenu addItem:action_item( @"Zoom", @selector( performZoom: ), @"" )];
		[window.submenu addItem:NSMenuItem.separatorItem];
		[window.submenu addItem:action_item( @"Bring All to Front", @selector( arrangeInFront: ), @"" )];
		NSApp.windowsMenu = window.submenu;
		[bar addItem:window];

		return bar;
	}

} // namespace

@implementation ArTermAppDelegate{
	ArTermMainWindowController* _main_window;
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification{
	NSApp.mainMenu = build_main_menu();
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification{
	_main_window = [ArTermMainWindowController new];
	[_main_window showWindow:nil];
	[NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender{
	return YES;
}

@end
