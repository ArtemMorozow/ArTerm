#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

/// One live SSH session: the terminal view wired to a ShellSession.
/// Deallocating the controller shuts the session down.
@interface ArTermSessionViewController : NSViewController

- (instancetype)initWithProfile:(arterm::ssh::HostProfile)profile;

@end
