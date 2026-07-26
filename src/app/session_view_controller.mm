#import "app/session_view_controller.h"

#import "app/terminal_view.h"

#include "core/dispatch.hpp"
#include "core/log.hpp"
#include "ssh/session_interaction.hpp"
#include "ssh/shell_session.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>

using namespace arterm;

namespace
{

	/// Host-key confirmation, shown from `on_main_sync` while the worker waits.
	bool confirm_host_key_alert( ssh::HostKeyInfo const& info ){
		NSAlert* alert = [NSAlert new];

		bool const changed = info.verdict == ssh::HostKeyVerdict::MISMATCH;
		alert.messageText  = changed ? @"Host key has changed" : @"Unknown host";
		alert.alertStyle   = changed ? NSAlertStyleCritical : NSAlertStyleWarning;

		std::string const details =
			std::format( "{}:{}\n\n{} key\n{}\n{}\n\n{}", info.hostname, info.port, info.key_type, info.sha256,
						 info.md5,
						 changed ? "The key differs from the one on record. This can mean the server was "
								   "reinstalled - or that the connection is being intercepted."
								 : "This host is not in known_hosts yet. Trusting it stores the key." );
		alert.informativeText = @( details.c_str() );

		[alert addButtonWithTitle:changed ? @"Accept the New Key" : @"Trust and Connect"];
		[alert addButtonWithTitle:@"Cancel"];

		return [alert runModal] == NSAlertFirstButtonReturn;
	}

	/// Password/passphrase prompt, same calling convention.
	std::optional<std::string> ask_credential_alert( std::string const& prompt, bool echo ){
		NSAlert* alert        = [NSAlert new];
		alert.messageText     = @( prompt.c_str() );
		alert.informativeText = @"The answer is kept for this run only; save it permanently in the host editor.";

		NSTextField* field = echo ? [[NSTextField alloc] initWithFrame:NSMakeRect( 0, 0, 260, 24 )]
								  : [[NSSecureTextField alloc] initWithFrame:NSMakeRect( 0, 0, 260, 24 )];
		alert.accessoryView       = field;
		alert.window.initialFirstResponder = field;

		[alert addButtonWithTitle:@"OK"];
		[alert addButtonWithTitle:@"Cancel"];

		if( [alert runModal] != NSAlertFirstButtonReturn )
			return std::nullopt;
		return std::string( field.stringValue.UTF8String != nullptr ? field.stringValue.UTF8String : "" );
	}

} // namespace

@implementation ArTermSessionViewController{
	ssh::HostProfile                          _profile;
	std::shared_ptr<ssh::SessionInteraction>  _interaction;
	std::shared_ptr<ssh::ShellSession>        _session;

	ArTermTerminalView* _terminal_view;
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
	// The shared_ptr release serialises against in-flight queue work in the
	// session destructor.
	_session.reset();
}

- (void)loadView{
	_terminal_view = [[ArTermTerminalView alloc] initWithFrame:NSMakeRect( 0, 0, 800, 600 )];
	self.view      = _terminal_view;
}

- (void)viewDidLoad{
	[super viewDidLoad];

	_interaction = std::make_shared<ssh::SessionInteraction>();
	_interaction->set_host_key_handler( confirm_host_key_alert );
	_interaction->set_credential_handler( ask_credential_alert );

	_session = ssh::ShellSession::create( _profile, _interaction );

	[self wireSession];

	[_terminal_view setInputHandler:[session = _session]( std::string bytes ){
		session->write( std::move( bytes ) );
	}];
	[_terminal_view setResizeHandler:[session = _session]( int columns, int rows, int width, int height ){
		session->resize( columns, rows, width, height );
	}];

	_session->start();
}

- (void)viewDidAppear{
	[super viewDidAppear];
	[self.view.window makeFirstResponder:_terminal_view];

	// The view got its real size before the session existed; push it now.
	NSSize const size = _terminal_view.frame.size;
	[_terminal_view setFrameSize:size];
}

- (void)wireSession{
	__weak ArTermSessionViewController* weak_self = self;

	// Session signals fire on the session queue; everything UI marshals to main.
	_session->data_received.connect( [weak_self]( std::string const& data ){
		std::string copy = data;
		on_main( [weak_self, copy = std::move( copy )]{
			ArTermSessionViewController* strong_self = weak_self;
			if( strong_self != nil )
				[strong_self->_terminal_view feed:copy];
		} );
	} );

	_session->connected.connect( [weak_self]( std::string const& banner, std::string const& auth ){
		std::string const message = std::format( "connected ({}){}", auth, banner.empty() ? "" : " - " + banner );
		on_main( [weak_self, message]{
			ArTermSessionViewController* strong_self = weak_self;
			if( strong_self != nil )
				log_info( "app", "{}: {}", strong_self->_profile.endpoint(), message );
		} );
	} );

	_session->failed.connect( [weak_self]( Error const& error ){
		std::string const message = error.message;
		on_main( [weak_self, message]{
			ArTermSessionViewController* strong_self = weak_self;
			if( strong_self == nil )
				return;

			// Render the failure into the terminal itself: keeps the context and
			// avoids a second modal after the interactive prompts.
			std::string const text = "\r\n\033[31mConnection failed: " + message + "\033[0m\r\n";
			[strong_self->_terminal_view feed:text];
		} );
	} );

	_session->closed.connect( [weak_self]( int exit_status ){
		on_main( [weak_self, exit_status]{
			ArTermSessionViewController* strong_self = weak_self;
			if( strong_self == nil )
				return;

			std::string const text =
				std::format( "\r\n\033[2m[session closed with status {}]\033[0m\r\n", exit_status );
			[strong_self->_terminal_view feed:text];
		} );
	} );

	// Emulator signals fire on main (feed runs there).
	auto& terminal = [_terminal_view terminal];

	terminal.reply.connect( [session = std::weak_ptr( _session )]( std::string const& data ){
		if( auto locked = session.lock() )
			locked->write( data );
	} );

	terminal.title_changed.connect( [weak_self]( std::string const& title ){
		ArTermSessionViewController* strong_self = weak_self;
		if( strong_self != nil && strong_self.view.window != nil )
			strong_self.view.window.title = @( title.c_str() );
	} );

	terminal.bell_rang.connect( []{ NSBeep(); } );
}

- (ArTermTerminalView*)terminalView{
	return _terminal_view;
}

@end
