#pragma once

#import <Cocoa/Cocoa.h>

#include "ssh/ssh_types.hpp"

#include <functional>
#include <optional>

/// The host form, presented as a sheet. Creates a new profile or edits an
/// existing one; the completion fires with the result, or with nothing on
/// cancel.
///
/// When editing, the secret fields start empty and empty means "keep what the
/// keychain already has" - the stored secret is never echoed back into the UI.
@interface ArTermHostEditorController : NSViewController

- (instancetype)initWithProfile:(std::optional<arterm::ssh::HostProfile>)profile;

- (void)setCompletionHandler:(std::function<void( std::optional<arterm::ssh::HostProfile> )>)handler;

@end
