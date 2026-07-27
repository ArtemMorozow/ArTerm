#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

/// A remote file browser tab, driven by its own SftpSession.
///
/// Opened deliberately from the + button rather than alongside every shell,
/// because it costs a second SSH connection to the host.
@interface ArTermFilesViewController : NSViewController

- (instancetype)initWithProfile:(arterm::ssh::HostProfile)profile;

@end
