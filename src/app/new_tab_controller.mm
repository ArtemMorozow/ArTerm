#import "app/new_tab_controller.h"

#include "ssh/endpoint.hpp"

#include <cstdlib>
#include <string>
#include <utility>

using namespace arterm;

@interface ArTermNewTabController () <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@end

@implementation ArTermNewTabController{
	std::vector<ssh::HostProfile>                        _hosts;
	std::function<void( ssh::HostProfile, bool )>        _open;

	NSTextField*     _address;
	NSTableView*     _table;
	NSSegmentedControl* _kind;
	NSTextField*     _hint;
}

- (void)setHosts:(std::vector<arterm::ssh::HostProfile>)hosts{
	_hosts = std::move( hosts );
	[_table reloadData];
}

- (void)setOpenHandler:(std::function<void( arterm::ssh::HostProfile, bool )>)handler{
	_open = std::move( handler );
}

- (void)focusAddressField{
	[self.view.window makeFirstResponder:_address];
}

- (void)loadView{
	NSTextField* heading = [NSTextField labelWithString:@"Connect to"];
	heading.font         = [NSFont systemFontOfSize:22 weight:NSFontWeightSemibold];

	_address                     = [NSTextField textFieldWithString:@""];
	_address.placeholderString   = @"user@host  or  host:2222";
	_address.font                = [NSFont monospacedSystemFontOfSize:14 weight:NSFontWeightRegular];
	_address.target              = self;
	_address.action              = @selector( addressEntered: );
	_address.delegate            = self;
	[_address.heightAnchor constraintEqualToConstant:28].active = YES;
	// The field takes whatever the segmented control and button leave, instead
	// of being squeezed to its intrinsic width.
	[_address setContentHuggingPriority:NSLayoutPriorityDefaultLow
						 forOrientation:NSLayoutConstraintOrientationHorizontal];
	[_address setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
									   forOrientation:NSLayoutConstraintOrientationHorizontal];
	[_address.widthAnchor constraintGreaterThanOrEqualToConstant:240].active = YES;

	_kind = [NSSegmentedControl segmentedControlWithLabels:@[ @"Terminal", @"Files" ]
											  trackingMode:NSSegmentSwitchTrackingSelectOne
													target:nil
													action:nil];
	[_kind setSelectedSegment:0];

	NSButton* connect = [NSButton buttonWithTitle:@"Connect" target:self action:@selector( addressEntered: )];
	connect.keyEquivalent = @"\r";

	NSStackView* row = [NSStackView stackViewWithViews:@[ _address, _kind, connect ]];
	row.orientation  = NSUserInterfaceLayoutOrientationHorizontal;
	row.spacing      = 8;
	// Fill, so the low-hugging address field absorbs the row's spare width
	// rather than everything sitting at its intrinsic size.
	row.distribution = NSStackViewDistributionFill;

	_hint            = [NSTextField labelWithString:@"A host typed here is not saved; use the sidebar to keep one."];
	_hint.font       = [NSFont systemFontOfSize:11];
	_hint.textColor  = NSColor.secondaryLabelColor;

	NSTextField* saved = [NSTextField labelWithString:@"Saved hosts"];
	saved.font         = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
	saved.textColor    = NSColor.secondaryLabelColor;

	_table                                    = [NSTableView new];
	_table.dataSource                         = self;
	_table.delegate                           = self;
	_table.headerView                         = nil;
	_table.rowHeight                          = 30;
	_table.target                             = self;
	_table.doubleAction                       = @selector( hostChosen: );
	_table.usesAlternatingRowBackgroundColors = NO;
	[_table addTableColumn:[[NSTableColumn alloc] initWithIdentifier:@"host"]];

	NSScrollView* scroll       = [NSScrollView new];
	scroll.documentView        = _table;
	scroll.hasVerticalScroller = YES;
	scroll.drawsBackground     = NO;

	NSStackView* column = [NSStackView stackViewWithViews:@[ heading, row, _hint, saved, scroll ]];
	column.orientation  = NSUserInterfaceLayoutOrientationVertical;
	column.alignment    = NSLayoutAttributeLeading;
	column.spacing      = 10;
	[column setCustomSpacing:20 afterView:_hint];

	NSView* container = [NSView new];
	[container addSubview:column];

	column.translatesAutoresizingMaskIntoConstraints = NO;
	scroll.translatesAutoresizingMaskIntoConstraints = NO;
	row.translatesAutoresizingMaskIntoConstraints    = NO;

	NSLayoutConstraint* preferred_width = [column.widthAnchor constraintEqualToConstant:560];
	// Breakable, so a narrow window shrinks the page instead of overflowing it.
	preferred_width.priority = NSLayoutPriorityDefaultHigh;

	[NSLayoutConstraint activateConstraints:@[
		// Centred with a sensible maximum width, so the page does not stretch
		// across a wide window the way a raw stack view would.
		[column.centerXAnchor constraintEqualToAnchor:container.centerXAnchor],
		[column.topAnchor constraintEqualToAnchor:container.topAnchor constant:60],
		[column.bottomAnchor constraintLessThanOrEqualToAnchor:container.bottomAnchor constant:-40],
		// A definite width, not one derived from the content: the row below
		// distributes its slack to the address field, and that cannot work while
		// the column's own width still depends on what the row asks for.
		preferred_width,
		[column.widthAnchor constraintLessThanOrEqualToAnchor:container.widthAnchor constant:-80],

		[row.widthAnchor constraintEqualToAnchor:column.widthAnchor],
		[scroll.widthAnchor constraintEqualToAnchor:column.widthAnchor],
		[scroll.heightAnchor constraintGreaterThanOrEqualToConstant:120],
	]];

	self.view = container;
}

- (void)viewDidAppear{
	[super viewDidAppear];
	[self focusAddressField];
}

// -- Opening ----------------------------------------------------------------

- (BOOL)wantsFiles{
	return _kind.selectedSegment == 1;
}

- (void)addressEntered:(id)sender{
	char const* text = _address.stringValue.UTF8String;
	auto const  parsed = ssh::parse_endpoint( text != nullptr ? text : "" );

	if( !parsed ){
		NSAlert* alert        = [NSAlert new];
		alert.messageText     = @"That is not a destination";
		alert.informativeText = @"Enter a host like example.com, user@example.com, or user@host:2222.";
		[alert runModal];
		return;
	}

	ssh::HostProfile profile;
	profile.hostname = parsed->hostname;
	profile.port     = parsed->port;
	profile.username = parsed->username;

	// An address typed here has no saved profile, so fall back to the login name
	// the way ssh does.
	if( profile.username.empty() ){
		if( char const* user = std::getenv( "USER" ); user != nullptr )
			profile.username = user;
	}

	if( _open )
		_open( std::move( profile ), [self wantsFiles] );
}

- (void)hostChosen:(id)sender{
	NSInteger const row = _table.clickedRow >= 0 ? _table.clickedRow : _table.selectedRow;
	if( row < 0 || row >= static_cast<NSInteger>( _hosts.size() ) )
		return;

	if( _open )
		_open( _hosts[static_cast<std::size_t>( row )], [self wantsFiles] );
}

// -- NSTableView ------------------------------------------------------------

- (NSInteger)numberOfRowsInTableView:(NSTableView*)table{
	return static_cast<NSInteger>( _hosts.size() );
}

- (NSView*)tableView:(NSTableView*)table viewForTableColumn:(NSTableColumn*)column row:(NSInteger)row{
	if( row < 0 || row >= static_cast<NSInteger>( _hosts.size() ) )
		return nil;

	auto const& profile = _hosts[static_cast<std::size_t>( row )];

	NSTextField* title = [NSTextField labelWithString:@( profile.display_name().c_str() )];
	title.font         = [NSFont systemFontOfSize:13];

	NSString* endpoint = @( ( profile.username.empty() ? profile.endpoint()
													   : profile.username + "@" + profile.endpoint() )
								.c_str() );
	NSTextField* detail = [NSTextField labelWithString:endpoint];
	detail.font         = [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular];
	detail.textColor    = NSColor.secondaryLabelColor;

	NSStackView* stack = [NSStackView stackViewWithViews:@[ title, detail ]];
	stack.orientation  = NSUserInterfaceLayoutOrientationVertical;
	stack.alignment    = NSLayoutAttributeLeading;
	stack.spacing      = 0;

	NSTableCellView* cell = [NSTableCellView new];
	[cell addSubview:stack];
	cell.textField = title;

	stack.translatesAutoresizingMaskIntoConstraints = NO;
	[NSLayoutConstraint activateConstraints:@[
		[stack.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:6],
		[stack.trailingAnchor constraintLessThanOrEqualToAnchor:cell.trailingAnchor],
		[stack.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
	]];
	return cell;
}

@end
