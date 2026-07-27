#import "app/host_list_controller.h"

#import "app/host_editor_controller.h"

#include "core/log.hpp"
#include "model/host_store.hpp"

#include <optional>
#include <utility>

/// One row of the outline: either a group header or a host.
@interface ArTermHostItem : NSObject
@property( nonatomic ) NSString*                     title;
@property( nonatomic ) NSString*                     detail;    ///< user@host:port, hosts only.
@property( nonatomic, nullable ) NSString*           profileId; ///< nil for groups.
@property( nonatomic ) NSArray<ArTermHostItem*>*     children;
@end

@implementation ArTermHostItem
@end

@interface ArTermHostListController () <NSOutlineViewDataSource, NSOutlineViewDelegate>
@end

@implementation ArTermHostListController{
	arterm::model::HostStore _store;

	std::function<void( arterm::ssh::HostProfile )> _on_connect;

	NSOutlineView*            _outline;
	NSArray<ArTermHostItem*>* _groups;
}

- (void)setConnectHandler:(std::function<void( arterm::ssh::HostProfile )>)handler{
	_on_connect = std::move( handler );
}

- (void)loadView{
	_outline                 = [NSOutlineView new];
	_outline.headerView      = nil;
	_outline.floatsGroupRows = NO;
	_outline.rowSizeStyle    = NSTableViewRowSizeStyleDefault;
	_outline.style           = NSTableViewStyleSourceList;
	_outline.dataSource      = self;
	_outline.delegate        = self;
	_outline.target          = self;
	_outline.doubleAction    = @selector( connectToSelectedHost: );

	NSMenu* context = [NSMenu new];
	[context addItemWithTitle:@"Connect" action:@selector( connectToSelectedHost: ) keyEquivalent:@""].target = self;
	[context addItemWithTitle:@"Edit Host…" action:@selector( editClickedHost: ) keyEquivalent:@""].target = self;
	[context addItem:NSMenuItem.separatorItem];
	[context addItemWithTitle:@"Delete Host" action:@selector( deleteClickedHost: ) keyEquivalent:@""].target = self;
	_outline.menu = context;

	NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"host"];
	column.editable       = NO;
	[_outline addTableColumn:column];
	_outline.outlineTableColumn = column;

	NSScrollView* scroll       = [NSScrollView new];
	scroll.documentView        = _outline;
	scroll.hasVerticalScroller = YES;
	scroll.drawsBackground     = NO;

	self.view = scroll;
}

- (void)viewDidLoad{
	[super viewDidLoad];

	if( !_store.load() )
		arterm::log_warning( "app", "could not load the host list" );

	// The store lives on the main queue, so the signal fires right here in
	// whatever mutator changed it.
	_store.changed.connect( [self]{ [self reloadHosts]; } );

	[self reloadHosts];
}

- (void)reloadHosts{
	NSMutableDictionary<NSString*, NSMutableArray<ArTermHostItem*>*>* by_group = [NSMutableDictionary new];
	NSMutableArray<NSString*>* group_order = [NSMutableArray new];

	for( auto const& profile : _store.profiles() ){
		NSString* group = profile.group.empty() ? @"Hosts" : @( profile.group.c_str() );
		if( by_group[group] == nil ){
			by_group[group] = [NSMutableArray new];
			[group_order addObject:group];
		}

		ArTermHostItem* host = [ArTermHostItem new];
		host.title           = @( profile.display_name().c_str() );
		host.detail          = @( ( profile.username.empty() ? profile.endpoint()
																: profile.username + "@" + profile.endpoint() )
									  .c_str() );
		host.profileId       = @( profile.id.c_str() );
		host.children        = @[];
		[by_group[group] addObject:host];
	}

	NSMutableArray<ArTermHostItem*>* groups = [NSMutableArray new];
	for( NSString* name in
		 [group_order sortedArrayUsingSelector:@selector( localizedCaseInsensitiveCompare: )] ){
		ArTermHostItem* group = [ArTermHostItem new];
		group.title           = name;
		group.children        = by_group[name];
		[groups addObject:group];
	}

	_groups = groups;
	[_outline reloadData];
	[_outline expandItem:nil expandChildren:YES];
}

- (nullable NSString*)clickedProfileId{
	NSInteger const row = _outline.clickedRow >= 0 ? _outline.clickedRow : _outline.selectedRow;
	if( row < 0 )
		return nil;
	return ( (ArTermHostItem*)[_outline itemAtRow:row] ).profileId;
}

- (std::optional<arterm::ssh::HostProfile>)selectedProfile{
	NSInteger const row = _outline.selectedRow;
	if( row < 0 )
		return std::nullopt;

	NSString* profile_id = ( (ArTermHostItem*)[_outline itemAtRow:row] ).profileId;
	if( profile_id == nil )
		return std::nullopt;

	return _store.with_secrets( profile_id.UTF8String );
}

- (void)connectToSelectedHost:(id)sender{
	NSString* profile_id = [self clickedProfileId];
	if( profile_id == nil || !_on_connect )
		return;

	// Pass the profile without secrets: the session resolves those from the
	// keychain on a background thread, keeping keychain access off the main one.
	if( auto profile = _store.profile_by_id( profile_id.UTF8String ) )
		_on_connect( std::move( *profile ) );
}

// -- Editing ----------------------------------------------------------------

- (void)presentEditorFor:(std::optional<arterm::ssh::HostProfile>)profile{
	bool const is_new = !profile.has_value();

	ArTermHostEditorController* editor =
		[[ArTermHostEditorController alloc] initWithProfile:std::move( profile )];

	__weak ArTermHostListController* weak_self = self;
	[editor setCompletionHandler:[weak_self, is_new]( std::optional<arterm::ssh::HostProfile> result ){
		ArTermHostListController* strong_self = weak_self;
		if( strong_self == nil || !result )
			return;

		if( is_new )
			strong_self->_store.add( std::move( *result ) );
		else
			strong_self->_store.update( *result );
	}];

	[self presentViewControllerAsSheet:editor];
}

- (void)createHost{
	[self presentEditorFor:std::nullopt];
}

- (void)editClickedHost:(id)sender{
	NSString* profile_id = [self clickedProfileId];
	if( profile_id == nil )
		return;

	if( auto profile = _store.profile_by_id( profile_id.UTF8String ) )
		[self presentEditorFor:std::move( *profile )];
}

- (void)deleteClickedHost:(id)sender{
	NSString* profile_id = [self clickedProfileId];
	if( profile_id == nil )
		return;

	auto const profile = _store.profile_by_id( profile_id.UTF8String );
	if( !profile )
		return;

	NSAlert* alert        = [NSAlert new];
	alert.messageText     = [NSString stringWithFormat:@"Delete “%s”?", profile->display_name().c_str()];
	alert.informativeText = @"The profile and its keychain secrets are removed. This cannot be undone.";
	alert.alertStyle      = NSAlertStyleWarning;
	[alert addButtonWithTitle:@"Delete"];
	[alert addButtonWithTitle:@"Cancel"];

	if( [alert runModal] == NSAlertFirstButtonReturn )
		_store.remove( profile_id.UTF8String );
}

- (void)importFromSSHConfig{
	int const imported = _store.import_from_ssh_config();

	NSAlert* alert    = [NSAlert new];
	alert.messageText = imported > 0
							? [NSString stringWithFormat:@"Imported %d host%s", imported, imported == 1 ? "" : "s"]
							: @"Nothing to import";
	alert.informativeText = imported > 0 ? @"The imported hosts are in the “Imported” group."
										 : @"Every host in ~/.ssh/config is either already saved or a pattern.";
	[alert runModal];
}

// -- NSOutlineViewDataSource ------------------------------------------------

- (NSInteger)outlineView:(NSOutlineView*)outline numberOfChildrenOfItem:(nullable id)item{
	if( item == nil )
		return static_cast<NSInteger>( _groups.count );
	return static_cast<NSInteger>( ( (ArTermHostItem*)item ).children.count );
}

- (id)outlineView:(NSOutlineView*)outline child:(NSInteger)index ofItem:(nullable id)item{
	if( item == nil )
		return _groups[static_cast<NSUInteger>( index )];
	return ( (ArTermHostItem*)item ).children[static_cast<NSUInteger>( index )];
}

- (BOOL)outlineView:(NSOutlineView*)outline isItemExpandable:(id)item{
	return ( (ArTermHostItem*)item ).children.count > 0;
}

// -- NSOutlineViewDelegate --------------------------------------------------

- (BOOL)outlineView:(NSOutlineView*)outline isGroupItem:(id)item{
	return ( (ArTermHostItem*)item ).profileId == nil;
}

- (BOOL)outlineView:(NSOutlineView*)outline shouldSelectItem:(id)item{
	return ( (ArTermHostItem*)item ).profileId != nil;
}

- (NSView*)outlineView:(NSOutlineView*)outline viewForTableColumn:(NSTableColumn*)column item:(id)item{
	ArTermHostItem* host = item;

	NSTableCellView* cell = [outline makeViewWithIdentifier:@"cell" owner:self];
	if( cell == nil ){
		cell            = [NSTableCellView new];
		cell.identifier = @"cell";

		NSTextField* label = [NSTextField labelWithString:@""];
		label.lineBreakMode = NSLineBreakByTruncatingTail;
		[cell addSubview:label];
		cell.textField = label;

		label.translatesAutoresizingMaskIntoConstraints = NO;
		[NSLayoutConstraint activateConstraints:@[
			[label.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:2],
			[label.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-2],
			[label.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
		]];
	}

	cell.textField.stringValue = host.title;
	cell.textField.font        = host.profileId == nil ? [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold]
													   : [NSFont systemFontOfSize:13];
	cell.textField.textColor   = host.profileId == nil ? NSColor.secondaryLabelColor : NSColor.labelColor;
	cell.toolTip               = host.detail;
	return cell;
}

@end
