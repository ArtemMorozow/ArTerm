#import "app/files_view_controller.h"

#include "core/dispatch.hpp"
#include "core/log.hpp"
#include "model/secret_store.hpp"
#include "ssh/session_interaction.hpp"
#include "ssh/sftp_session.hpp"

#include <atomic>
#include <iterator>
#include <format>
#include <memory>
#include <string>
#include <vector>

using namespace arterm;

namespace
{

	/// Request ids only have to be unique within a session; one counter for the
	/// whole process is simpler than one per controller and just as correct.
	std::uint64_t next_request_id(){
		static std::atomic<std::uint64_t> counter{ 1 };
		return counter.fetch_add( 1, std::memory_order_relaxed );
	}

	std::string format_bytes( std::uint64_t bytes ){
		char const* const units[] = { "B", "KB", "MB", "GB", "TB" };

		double      value = static_cast<double>( bytes );
		std::size_t unit  = 0;
		while( value >= 1024.0 && unit + 1 < std::size( units ) ){
			value /= 1024.0;
			++unit;
		}
		return unit == 0 ? std::format( "{} {}", bytes, units[unit] )
						 : std::format( "{:.1f} {}", value, units[unit] );
	}

	NSString* format_size( std::uint64_t bytes, bool is_directory ){
		if( is_directory )
			return @"--";
		return [NSByteCountFormatter stringFromByteCount:static_cast<long long>( bytes )
											  countStyle:NSByteCountFormatterCountStyleFile];
	}

	NSString* format_modified( std::chrono::system_clock::time_point when ){
		if( when == std::chrono::system_clock::time_point{} )
			return @"";

		auto const     seconds = std::chrono::system_clock::to_time_t( when );
		NSDate*        date    = [NSDate dateWithTimeIntervalSince1970:static_cast<NSTimeInterval>( seconds )];
		NSDateFormatter* formatter = [NSDateFormatter new];
		formatter.dateStyle        = NSDateFormatterShortStyle;
		formatter.timeStyle        = NSDateFormatterShortStyle;
		return [formatter stringFromDate:date];
	}

	std::string parent_of( std::string const& path ){
		if( path == "/" || path.empty() )
			return "/";

		auto const slash = path.find_last_of( '/' );
		if( slash == std::string::npos || slash == 0 )
			return "/";
		return path.substr( 0, slash );
	}

} // namespace

@interface ArTermFilesViewController () <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation ArTermFilesViewController{
	ssh::HostProfile                         _profile;
	std::shared_ptr<ssh::SessionInteraction> _interaction;
	std::shared_ptr<ssh::SftpSession>        _session;

	ssh::RemoteListing _entries;
	std::string        _path;

	NSTableView*         _table;
	NSTextField*         _path_field;
	NSProgressIndicator* _spinner;
	NSTextField*         _status;

	NSProgressIndicator* _progress;
	NSButton*            _cancel;
	/// The transfer in flight, so its progress can be matched and cancelled.
	std::uint64_t _active_request;
}

- (instancetype)initWithProfile:(arterm::ssh::HostProfile)profile{
	if( ( self = [super initWithNibName:nil bundle:nil] ) ){
		_profile = std::move( profile );
	}
	return self;
}

- (void)dealloc{
	if( _session )
		_session->shutdown();
	_session.reset();
}

- (void)loadView{
	NSButton* up = [NSButton buttonWithTitle:@"↑" target:self action:@selector( goUp: )];
	up.bezelStyle = NSBezelStyleTexturedRounded;
	up.toolTip    = @"Parent directory";

	NSButton* refresh = [NSButton buttonWithTitle:@"⟳" target:self action:@selector( refresh: )];
	refresh.bezelStyle = NSBezelStyleTexturedRounded;

	_path_field           = [NSTextField textFieldWithString:@"/"];
	_path_field.target    = self;
	_path_field.action    = @selector( pathEntered: );
	_path_field.font      = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];

	_spinner                    = [NSProgressIndicator new];
	_spinner.style              = NSProgressIndicatorStyleSpinning;
	_spinner.controlSize        = NSControlSizeSmall;
	_spinner.displayedWhenStopped = NO;
	[_spinner.widthAnchor constraintEqualToConstant:16].active = YES;

	NSStackView* toolbar = [NSStackView stackViewWithViews:@[ up, refresh, _path_field, _spinner ]];
	toolbar.orientation  = NSUserInterfaceLayoutOrientationHorizontal;
	toolbar.spacing      = 6;
	toolbar.edgeInsets   = NSEdgeInsetsMake( 6, 8, 6, 8 );

	_table                      = [NSTableView new];
	_table.dataSource           = self;
	_table.delegate             = self;
	_table.usesAlternatingRowBackgroundColors = YES;
	_table.rowSizeStyle         = NSTableViewRowSizeStyleDefault;
	_table.target               = self;
	_table.doubleAction         = @selector( openSelected: );

	NSMenu* menu = [NSMenu new];
	[menu addItemWithTitle:@"Download…" action:@selector( downloadSelected: ) keyEquivalent:@""].target = self;
	[menu addItem:NSMenuItem.separatorItem];
	[menu addItemWithTitle:@"Copy Path" action:@selector( copyPath: ) keyEquivalent:@""].target = self;
	[menu addItemWithTitle:@"Refresh" action:@selector( refresh: ) keyEquivalent:@""].target     = self;
	_table.menu = menu;

	struct
	{
		NSString* identifier;
		NSString* title;
		CGFloat   width;
	} const columns[] = {
		{ @"name", @"Name", 320 },
		{ @"size", @"Size", 90 },
		{ @"modified", @"Modified", 140 },
		{ @"permissions", @"Permissions", 110 },
	};

	for( auto const& column : columns ){
		NSTableColumn* table_column = [[NSTableColumn alloc] initWithIdentifier:column.identifier];
		table_column.title          = column.title;
		table_column.width          = column.width;
		[_table addTableColumn:table_column];
	}

	NSScrollView* scroll        = [NSScrollView new];
	scroll.documentView         = _table;
	scroll.hasVerticalScroller  = YES;
	scroll.autohidesScrollers   = YES;

	_status           = [NSTextField labelWithString:@"Connecting…"];
	_status.textColor = NSColor.secondaryLabelColor;
	_status.font      = [NSFont systemFontOfSize:11];

	_progress                     = [NSProgressIndicator new];
	_progress.style               = NSProgressIndicatorStyleBar;
	_progress.indeterminate       = NO;
	_progress.minValue            = 0;
	_progress.maxValue            = 1;
	_progress.hidden              = YES;
	[_progress.widthAnchor constraintEqualToConstant:160].active = YES;

	_cancel        = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector( cancelTransfer: )];
	_cancel.bezelStyle = NSBezelStyleTexturedRounded;
	_cancel.hidden     = YES;

	NSStackView* footer = [NSStackView stackViewWithViews:@[ _status, _progress, _cancel ]];
	footer.orientation  = NSUserInterfaceLayoutOrientationHorizontal;
	footer.edgeInsets   = NSEdgeInsetsMake( 4, 8, 4, 8 );

	NSStackView* column = [NSStackView stackViewWithViews:@[ toolbar, scroll, footer ]];
	column.orientation  = NSUserInterfaceLayoutOrientationVertical;
	column.spacing      = 0;
	column.distribution = NSStackViewDistributionFill;

	toolbar.translatesAutoresizingMaskIntoConstraints = NO;
	scroll.translatesAutoresizingMaskIntoConstraints  = NO;
	footer.translatesAutoresizingMaskIntoConstraints  = NO;
	[NSLayoutConstraint activateConstraints:@[
		[toolbar.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
		[toolbar.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
		[scroll.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
		[scroll.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
		[footer.leadingAnchor constraintEqualToAnchor:column.leadingAnchor],
		[footer.trailingAnchor constraintEqualToAnchor:column.trailingAnchor],
	]];

	self.view = column;
}

- (void)viewDidLoad{
	[super viewDidLoad];
	[_spinner startAnimation:nil];

	// Same reasoning as the shell tab: the keychain can block, and on an
	// ad-hoc-signed build it can put up a permission dialog.
	__weak ArTermFilesViewController* weak_self  = self;
	std::string const                 profile_id = _profile.id;

	dispatch_async( dispatch_get_global_queue( QOS_CLASS_USER_INITIATED, 0 ), ^{
		auto password   = model::SecretStore::retrieve( profile_id, model::SecretStore::PASSWORD );
		auto passphrase = model::SecretStore::retrieve( profile_id, model::SecretStore::PASSPHRASE );

		on_main( [weak_self, password, passphrase]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil )
				return;
			if( password )
				strong_self->_profile.password = *password;
			if( passphrase )
				strong_self->_profile.key_passphrase = *passphrase;
			[strong_self startSession];
		} );
	} );
}

- (void)startSession{
	_interaction = std::make_shared<ssh::SessionInteraction>();
	_session     = ssh::SftpSession::create( _profile, _interaction );
	_session->set_transfer_backend( _profile.transfer_backend );

	__weak ArTermFilesViewController* weak_self = self;

	_session->ready.connect( [weak_self]( std::string const& home ){
		on_main( [weak_self, home]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil )
				return;
			[strong_self->_spinner stopAnimation:nil];
			[strong_self navigateTo:home];
		} );
	} );

	_session->failed.connect( [weak_self]( Error const& error ){
		std::string const message = error.message;
		on_main( [weak_self, message]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil )
				return;
			[strong_self->_spinner stopAnimation:nil];
			strong_self->_status.stringValue = @( ( "Connection failed: " + message ).c_str() );
		} );
	} );

	_session->listing_ready.connect(
		[weak_self]( std::uint64_t, std::string const& path, ssh::RemoteListing const& entries ){
			ssh::RemoteListing copy = entries;
			std::string const  where = path;
			on_main( [weak_self, where, copy = std::move( copy )]{
				ArTermFilesViewController* strong_self = weak_self;
				if( strong_self == nil )
					return;
				[strong_self showListing:copy at:where];
			} );
		} );

	_session->operation_failed.connect( [weak_self]( std::uint64_t, Error const& error ){
		std::string const message = error.message;
		on_main( [weak_self, message]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil )
				return;
			[strong_self->_spinner stopAnimation:nil];
			if( strong_self->_active_request != 0 )
				[strong_self finishTransferWithMessage:@( ( "Download failed: " + message ).c_str() )];
			else
				strong_self->_status.stringValue = @( message.c_str() );
		} );
	} );

	_session->transfer_started.connect( [weak_self]( std::uint64_t request, std::uint64_t total ){
		on_main( [weak_self, request, total]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil || request != strong_self->_active_request )
				return;
			strong_self->_progress.hidden      = NO;
			strong_self->_cancel.hidden        = NO;
			strong_self->_progress.doubleValue = 0;
			strong_self->_status.stringValue =
				@( std::format( "Downloading {} …", format_bytes( total ) ).c_str() );
		} );
	} );

	_session->transfer_progress.connect( [weak_self]( std::uint64_t request, ssh::TransferProgress const& progress ){
		ssh::TransferProgress const copy = progress;
		on_main( [weak_self, request, copy]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil || request != strong_self->_active_request )
				return;

			strong_self->_progress.doubleValue = copy.fraction();
			strong_self->_status.stringValue   = @(
				std::format( "{} of {} - {}/s", format_bytes( copy.transferred ), format_bytes( copy.total ),
							 format_bytes( static_cast<std::uint64_t>( copy.bytes_per_second ) ) )
					.c_str() );
		} );
	} );

	_session->transfer_finished.connect( [weak_self]( std::uint64_t request ){
		on_main( [weak_self, request]{
			ArTermFilesViewController* strong_self = weak_self;
			if( strong_self == nil || request != strong_self->_active_request )
				return;
			[strong_self finishTransferWithMessage:@"Download complete"];
		} );
	} );

	_session->start();
}

- (void)finishTransferWithMessage:(NSString*)message{
	_active_request      = 0;
	_progress.hidden     = YES;
	_cancel.hidden       = YES;
	_status.stringValue  = message;
}

- (void)showListing:(ssh::RemoteListing const&)entries at:(std::string const&)path{
	_entries = entries;
	_path    = path;

	_path_field.stringValue = @( path.c_str() );
	[_spinner stopAnimation:nil];
	[_table reloadData];

	std::size_t directories = 0;
	for( auto const& entry : _entries )
		directories += entry.is_directory ? 1 : 0;

	_status.stringValue = @( std::format( "{} items - {} directories, {} files", _entries.size(), directories,
										  _entries.size() - directories )
								 .c_str() );
}

- (void)navigateTo:(std::string const&)path{
	if( !_session )
		return;

	[_spinner startAnimation:nil];
	_session->list_directory( next_request_id(), path );
}

// -- Actions ----------------------------------------------------------------

- (void)goUp:(id)sender{
	[self navigateTo:parent_of( _path )];
}

- (void)refresh:(id)sender{
	[self navigateTo:_path];
}

- (void)pathEntered:(id)sender{
	char const* entered = _path_field.stringValue.UTF8String;
	[self navigateTo:entered != nullptr ? std::string( entered ) : _path];
}

/// The row the user acted on: the one right-clicked, else the selected one.
- (ssh::RemoteFileEntry const*)targetEntry{
	NSInteger const row = _table.clickedRow >= 0 ? _table.clickedRow : _table.selectedRow;
	if( row < 0 || row >= static_cast<NSInteger>( _entries.size() ) )
		return nullptr;
	return &_entries[static_cast<std::size_t>( row )];
}

- (void)openSelected:(id)sender{
	ssh::RemoteFileEntry const* entry = [self targetEntry];
	if( entry == nullptr )
		return;

	// Double-clicking a directory descends into it; on a file it is the natural
	// "I want this" gesture, so it downloads.
	if( entry->is_directory )
		[self navigateTo:entry->path];
	else
		[self downloadSelected:sender];
}

- (void)downloadSelected:(id)sender{
	ssh::RemoteFileEntry const* entry = [self targetEntry];
	if( entry == nullptr || !_session )
		return;

	if( _active_request != 0 ){
		NSAlert* alert        = [NSAlert new];
		alert.messageText     = @"A download is already running";
		alert.informativeText = @"Transfers share the tab's single SFTP connection, so they run one at a time.";
		[alert runModal];
		return;
	}

	std::string const remote = entry->path;
	bool const        is_directory = entry->is_directory;

	// A directory is copied recursively, so the panel asks for the folder to
	// copy it into rather than for a file name.
	if( is_directory ){
		NSOpenPanel* panel         = [NSOpenPanel openPanel];
		panel.canChooseFiles       = NO;
		panel.canChooseDirectories = YES;
		panel.canCreateDirectories = YES;
		panel.prompt               = @"Download Here";
		panel.message = [NSString stringWithFormat:@"Choose where to put “%s”", entry->name.c_str()];

		if( [panel runModal] != NSModalResponseOK || panel.URL == nil )
			return;

		std::string const local =
			std::string( panel.URL.path.UTF8String ) + "/" + entry->name;
		[self startDownloadFrom:remote to:local];
		return;
	}

	NSSavePanel* panel        = [NSSavePanel savePanel];
	panel.nameFieldStringValue = @( entry->name.c_str() );
	panel.canCreateDirectories = YES;
	panel.prompt               = @"Download";

	if( [panel runModal] != NSModalResponseOK || panel.URL == nil )
		return;

	[self startDownloadFrom:remote to:std::string( panel.URL.path.UTF8String )];
}

- (void)startDownloadFrom:(std::string const&)remote to:(std::string const&)local{
	_active_request = next_request_id();
	_status.stringValue = @"Starting…";
	_session->download( _active_request, remote, local );
}

- (void)cancelTransfer:(id)sender{
	if( _active_request == 0 || !_session )
		return;

	// request_cancel is the one SftpSession method safe to call while a transfer
	// is in flight; the running copy notices at the next chunk boundary.
	_session->request_cancel( _active_request );
	_status.stringValue = @"Cancelling…";
}

- (void)copyPath:(id)sender{
	ssh::RemoteFileEntry const* entry = [self targetEntry];
	if( entry == nullptr )
		return;

	NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
	[pasteboard clearContents];
	[pasteboard setString:@( entry->path.c_str() ) forType:NSPasteboardTypeString];
}

- (BOOL)validateMenuItem:(NSMenuItem*)item{
	if( item.action == @selector( downloadSelected: ) || item.action == @selector( copyPath: ) )
		return [self targetEntry] != nullptr;
	return YES;
}

// -- NSTableViewDataSource --------------------------------------------------

- (NSInteger)numberOfRowsInTableView:(NSTableView*)table{
	return static_cast<NSInteger>( _entries.size() );
}

- (NSView*)tableView:(NSTableView*)table
	viewForTableColumn:(NSTableColumn*)column
				   row:(NSInteger)row{
	if( row < 0 || row >= static_cast<NSInteger>( _entries.size() ) )
		return nil;

	auto const& entry = _entries[static_cast<std::size_t>( row )];

	NSTextField* label   = [NSTextField labelWithString:@""];
	label.lineBreakMode  = NSLineBreakByTruncatingMiddle;
	label.font           = [NSFont systemFontOfSize:12];

	if( [column.identifier isEqualToString:@"name"] ){
		label.stringValue = [NSString stringWithFormat:@"%s%s", entry.is_directory ? "📁 " : "   ",
													   entry.name.c_str()];
		if( entry.is_symlink )
			label.textColor = NSColor.systemTealColor;
	}
	else if( [column.identifier isEqualToString:@"size"] ){
		label.stringValue = format_size( entry.size, entry.is_directory );
		label.alignment   = NSTextAlignmentRight;
	}
	else if( [column.identifier isEqualToString:@"modified"] ){
		label.stringValue = format_modified( entry.modified );
	}
	else{
		label.stringValue = @( entry.permission_string().c_str() );
		label.font        = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
		label.textColor   = NSColor.secondaryLabelColor;
	}

	NSTableCellView* cell = [NSTableCellView new];
	[cell addSubview:label];
	cell.textField = label;

	label.translatesAutoresizingMaskIntoConstraints = NO;
	[NSLayoutConstraint activateConstraints:@[
		[label.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:4],
		[label.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-4],
		[label.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
	]];
	return cell;
}

@end
