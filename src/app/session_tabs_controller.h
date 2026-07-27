#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

#include <functional>
#include <optional>

/// The tabbed session area: a tab bar with a + button beside it.
///
/// Terminal tabs and SFTP tabs are separate kinds. + offers both, so a file
/// browser is opened deliberately rather than appearing alongside every shell.
@interface ArTermSessionTabsController : NSViewController

/// Opens a shell tab and makes it current.
- (void)openTerminalForProfile:(arterm::ssh::HostProfile)profile;

/// Opens a file browser tab and makes it current.
- (void)openFilesForProfile:(arterm::ssh::HostProfile)profile;

/// Asked for the host to open when + is used; nothing means no host is
/// selected and the button does nothing.
- (void)setProfileProvider:(std::function<std::optional<arterm::ssh::HostProfile>()>)provider;

/// Closes the tab in front, shutting its session down.
- (void)closeCurrentTab;

/// True when no tab is open.
@property( nonatomic, readonly ) BOOL isEmpty;

@end
