#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

#include <functional>
#include <vector>

/// The tabbed session area: a tab bar with a + button beside it.
///
/// Terminal tabs and SFTP tabs are separate kinds. + offers both, so a file
/// browser is opened deliberately rather than appearing alongside every shell.
@interface ArTermSessionTabsController : NSViewController

/// Opens a shell tab and makes it current.
- (void)openTerminalForProfile:(arterm::ssh::HostProfile)profile;

/// Opens a file browser tab and makes it current.
- (void)openFilesForProfile:(arterm::ssh::HostProfile)profile;

/// Supplies the saved hosts a new tab lists. Called each time one opens, so
/// the page reflects the store without further wiring.
- (void)setHostsProvider:(std::function<std::vector<arterm::ssh::HostProfile>()>)provider;

/// Opens a new-tab start page.
- (void)newTab;

/// Closes the tab in front, shutting its session down.
- (void)closeCurrentTab;

/// True when no tab is open.
@property( nonatomic, readonly ) BOOL isEmpty;

@end
