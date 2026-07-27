#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

#include <functional>
#include <optional>

/// Sidebar: the saved hosts grouped the way the store reports them.
/// Owns the application's HostStore instance.
@interface ArTermHostListController : NSViewController

/// Fired on double-click with the profile including its keychain secrets.
- (void)setConnectHandler:(std::function<void( arterm::ssh::HostProfile )>)handler;

/// The profile selected in the sidebar, with its keychain secrets, or nothing
/// when no host is selected.
- (std::optional<arterm::ssh::HostProfile>)selectedProfile;

/// Opens the editor sheet for a new host.
- (void)createHost;

/// Imports from ~/.ssh/config and reports how many hosts were added.
- (void)importFromSSHConfig;

@end
