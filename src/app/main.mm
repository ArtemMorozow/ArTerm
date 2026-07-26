#import <Cocoa/Cocoa.h>

#import "app/app_delegate.h"

#include "core/log.hpp"

#include <cstring>

int main( int argc, char* argv[] ){
	for( int i = 1; i < argc; ++i ){
		if( std::strcmp( argv[i], "--verbose" ) == 0 )
			arterm::set_verbose_logging( true );
	}

	@autoreleasepool{
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
