#import <Cocoa/Cocoa.h>

#import "app/app_delegate.h"
#import "app/render_probe.h"

#include "core/log.hpp"

#include <cstring>

int main( int argc, char* argv[] ){
	for( int i = 1; i < argc; ++i ){
		if( std::strcmp( argv[i], "--verbose" ) == 0 )
			arterm::set_verbose_logging( true );
	}

	@autoreleasepool{
		// The render probe writes a PNG of the terminal view and exits; it must
		// run before the app takes over the main loop.
		if( NSString* path = NSProcessInfo.processInfo.environment[@"ARTERM_RENDER_PROBE"] ){
			[NSApplication sharedApplication]; // AppKit needs waking before it draws.
			return arterm_write_render_probe( path ) ? 0 : 1;
		}

		NSApplication*     application = NSApplication.sharedApplication;
		ArTermAppDelegate* delegate    = [ArTermAppDelegate new];

		// NSApplication.delegate is unretained; the local strong reference keeps
		// the delegate alive for the whole of -run.
		application.delegate = delegate;
		[application setActivationPolicy:NSApplicationActivationPolicyRegular];
		[application run];
	}
	return 0;
}
