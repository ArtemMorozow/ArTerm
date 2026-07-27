#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

#include <functional>
#include <vector>

/// What a new tab shows before it is pointed at anything: an address field and
/// the saved hosts, like a browser's start page.
@interface ArTermNewTabController : NSViewController

/// The saved hosts to offer. Called again when the store changes.
- (void)setHosts:(std::vector<arterm::ssh::HostProfile>)hosts;

/// Invoked when the user picks a destination. `files` asks for a browser tab
/// rather than a shell.
- (void)setOpenHandler:(std::function<void( arterm::ssh::HostProfile profile, bool files )>)handler;

/// Focuses the address field, so a fresh tab is ready to be typed into.
- (void)focusAddressField;

@end
