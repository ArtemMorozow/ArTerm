#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

/// The main window: a sidebar of saved hosts beside the session area.
@interface ArTermMainWindowController : NSWindowController

/// Opens a session tab for a profile. Used by the render probe to capture the
/// tab bar in the state it has once something is connected.
- (void)openSessionWithProfile:(arterm::ssh::HostProfile)profile;

@end
