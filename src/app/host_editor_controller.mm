#import "app/host_editor_controller.h"

#include <cstdlib>
#include <string>
#include <utility>

using namespace arterm;

namespace
{

	NSTextField* text_field( NSString* placeholder ){
		NSTextField* field   = [NSTextField textFieldWithString:@""];
		field.placeholderString = placeholder;
		[field.widthAnchor constraintGreaterThanOrEqualToConstant:260].active = YES;
		return field;
	}

	NSTextField* form_label( NSString* title ){
		NSTextField* label = [NSTextField labelWithString:title];
		label.alignment    = NSTextAlignmentRight;
		label.textColor    = NSColor.secondaryLabelColor;
		return label;
	}

	NSString* from_std( std::string const& text ){
		return @( text.c_str() );
	}

	std::string to_std( NSTextField* field ){
		char const* utf8 = field.stringValue.UTF8String;
		return utf8 != nullptr ? utf8 : "";
	}

} // namespace

@implementation ArTermHostEditorController{
	std::optional<ssh::HostProfile>                                _original;
	std::function<void( std::optional<ssh::HostProfile> )>         _completion;

	NSTextField* _label;
	NSTextField* _hostname;
	NSTextField* _port;
	NSTextField* _username;
	NSTextField* _group;

	NSPopUpButton*     _auth;
	NSTextField*       _key_path;
	NSSecureTextField* _passphrase;
	NSSecureTextField* _password;

	NSTextField*   _startup_directory;
	NSTextField*   _startup_command;
	NSPopUpButton* _backend;
	NSButton*      _compression;
	NSButton*      _strict_host_key;
}

- (instancetype)initWithProfile:(std::optional<arterm::ssh::HostProfile>)profile{
	if( ( self = [super initWithNibName:nil bundle:nil] ) ){
		_original = std::move( profile );
		self.title = _original ? @"Edit Host" : @"New Host";
	}
	return self;
}

- (void)setCompletionHandler:(std::function<void( std::optional<arterm::ssh::HostProfile> )>)handler{
	_completion = std::move( handler );
}

- (void)loadView{
	_label    = text_field( @"Display name (optional)" );
	_hostname = text_field( @"host.example.com" );
	_port     = text_field( @"22" );
	_username = text_field( NSUserName() );
	_group    = text_field( @"Sidebar group (optional)" );

	_auth = [NSPopUpButton new];
	[_auth addItemsWithTitles:@[ @"SSH agent", @"Public key", @"Password", @"Keyboard interactive" ]];

	_key_path            = text_field( @"~/.ssh/id_ed25519" );
	NSButton* browse     = [NSButton buttonWithTitle:@"Browse…" target:self action:@selector( browseForKey: )];
	NSStackView* key_row = [NSStackView stackViewWithViews:@[ _key_path, browse ]];
	key_row.orientation  = NSUserInterfaceLayoutOrientationHorizontal;

	_passphrase                   = [[NSSecureTextField alloc] initWithFrame:NSZeroRect];
	_passphrase.placeholderString = _original ? @"Unchanged" : @"Key passphrase (optional)";
	_password                     = [[NSSecureTextField alloc] initWithFrame:NSZeroRect];
	_password.placeholderString   = _original ? @"Unchanged" : @"Password (optional, stored in the keychain)";

	_startup_directory = text_field( @"Remote directory (optional)" );
	_startup_command   = text_field( @"Command run after login (optional)" );

	_backend = [NSPopUpButton new];
	[_backend addItemsWithTitles:@[ @"SFTP", @"SCP" ]];

	_compression     = [NSButton checkboxWithTitle:@"Compression" target:nil action:nil];
	_strict_host_key = [NSButton checkboxWithTitle:@"Strict host key checking" target:nil action:nil];

	NSGridView* grid = [NSGridView gridViewWithViews:@[
		@[ form_label( @"Label" ), _label ],
		@[ form_label( @"Host" ), _hostname ],
		@[ form_label( @"Port" ), _port ],
		@[ form_label( @"User" ), _username ],
		@[ form_label( @"Group" ), _group ],
		@[ form_label( @"Auth" ), _auth ],
		@[ form_label( @"Private key" ), key_row ],
		@[ form_label( @"Passphrase" ), _passphrase ],
		@[ form_label( @"Password" ), _password ],
		@[ form_label( @"Directory" ), _startup_directory ],
		@[ form_label( @"Command" ), _startup_command ],
		@[ form_label( @"Transfers" ), _backend ],
		@[ [NSView new], _compression ],
		@[ [NSView new], _strict_host_key ],
	]];
	grid.rowSpacing    = 8;
	grid.columnSpacing = 10;

	NSButton* ok     = [NSButton buttonWithTitle:_original ? @"Save" : @"Add Host"
										  target:self
										  action:@selector( accept: )];
	ok.keyEquivalent = @"\r";
	NSButton* cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector( reject: )];
	cancel.keyEquivalent = @"\033";

	NSStackView* buttons = [NSStackView stackViewWithViews:@[ cancel, ok ]];
	buttons.orientation  = NSUserInterfaceLayoutOrientationHorizontal;

	NSStackView* column = [NSStackView stackViewWithViews:@[ grid, buttons ]];
	column.orientation  = NSUserInterfaceLayoutOrientationVertical;
	column.alignment    = NSLayoutAttributeTrailing;
	column.spacing      = 14;
	column.edgeInsets   = NSEdgeInsetsMake( 20, 20, 20, 20 );

	self.view = column;

	[self populate];
}

- (void)populate{
	if( !_original ){
		_strict_host_key.state = NSControlStateValueOn;
		return;
	}

	ssh::HostProfile const& profile = *_original;

	_label.stringValue             = from_std( profile.label );
	_hostname.stringValue          = from_std( profile.hostname );
	_port.stringValue              = @( profile.port ).stringValue;
	_username.stringValue          = from_std( profile.username );
	_group.stringValue             = from_std( profile.group );
	_key_path.stringValue          = from_std( profile.private_key_path );
	_startup_directory.stringValue = from_std( profile.startup_directory );
	_startup_command.stringValue   = from_std( profile.startup_command );

	switch( profile.preferred_auth ){
		case ssh::AuthMethod::AGENT:
			[_auth selectItemAtIndex:0];
			break;
		case ssh::AuthMethod::PUBLIC_KEY:
			[_auth selectItemAtIndex:1];
			break;
		case ssh::AuthMethod::PASSWORD:
			[_auth selectItemAtIndex:2];
			break;
		case ssh::AuthMethod::KEYBOARD_INTERACTIVE:
			[_auth selectItemAtIndex:3];
			break;
	}

	[_backend selectItemAtIndex:profile.transfer_backend == ssh::TransferBackend::SCP ? 1 : 0];
	_compression.state     = profile.compression ? NSControlStateValueOn : NSControlStateValueOff;
	_strict_host_key.state = profile.strict_host_key_checking ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)browseForKey:(id)sender{
	NSOpenPanel* panel            = [NSOpenPanel openPanel];
	panel.canChooseFiles          = YES;
	panel.canChooseDirectories    = NO;
	panel.allowsMultipleSelection = NO;
	panel.showsHiddenFiles        = YES;
	panel.directoryURL = [NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:@".ssh"]];

	if( [panel runModal] == NSModalResponseOK && panel.URL != nil )
		_key_path.stringValue = panel.URL.path;
}

- (void)accept:(id)sender{
	std::string const hostname = to_std( _hostname );
	if( hostname.empty() ){
		NSAlert* alert    = [NSAlert new];
		alert.messageText = @"A hostname is required";
		[alert runModal];
		return;
	}

	ssh::HostProfile profile = _original.value_or( ssh::HostProfile{} );

	profile.label             = to_std( _label );
	profile.hostname          = hostname;
	profile.username          = to_std( _username );
	profile.group             = to_std( _group );
	profile.private_key_path  = to_std( _key_path );
	profile.startup_directory = to_std( _startup_directory );
	profile.startup_command   = to_std( _startup_command );

	int const port = std::atoi( _port.stringValue.UTF8String != nullptr ? _port.stringValue.UTF8String : "" );
	profile.port   = ( port > 0 && port <= 65535 ) ? static_cast<std::uint16_t>( port ) : 22;

	switch( _auth.indexOfSelectedItem ){
		case 1:
			profile.preferred_auth = ssh::AuthMethod::PUBLIC_KEY;
			break;
		case 2:
			profile.preferred_auth = ssh::AuthMethod::PASSWORD;
			break;
		case 3:
			profile.preferred_auth = ssh::AuthMethod::KEYBOARD_INTERACTIVE;
			break;
		default:
			profile.preferred_auth = ssh::AuthMethod::AGENT;
			break;
	}

	profile.transfer_backend =
		_backend.indexOfSelectedItem == 1 ? ssh::TransferBackend::SCP : ssh::TransferBackend::SFTP;
	profile.compression              = _compression.state == NSControlStateValueOn;
	profile.strict_host_key_checking = _strict_host_key.state == NSControlStateValueOn;

	// Blank secrets mean "keep the keychain entry"; the store only writes
	// non-empty ones.
	profile.password       = to_std( _password );
	profile.key_passphrase = to_std( _passphrase );

	auto completion = std::move( _completion );
	[self.presentingViewController dismissViewController:self];
	if( completion )
		completion( std::move( profile ) );
}

- (void)reject:(id)sender{
	auto completion = std::move( _completion );
	[self.presentingViewController dismissViewController:self];
	if( completion )
		completion( std::nullopt );
}

@end
