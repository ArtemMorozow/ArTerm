#pragma once

#import <Cocoa/Cocoa.h>

#include "terminal/terminal.hpp"

#include <functional>
#include <string>

/// The terminal: renders `term::Terminal` with CoreText and turns key events
/// into the byte sequences the remote PTY expects.
///
/// First iteration renders the live screen only; scrollback, selection and
/// mouse reporting arrive in later passes.
@interface ArTermTerminalView : NSView

/// The emulator this view renders. Owned by the view.
- (arterm::term::Terminal&)terminal;

/// Bytes the user produced (keys, paste); the session sends them to the host.
- (void)setInputHandler:(std::function<void( std::string )>)handler;

/// The grid was resized; the session forwards the new size to the PTY.
- (void)setResizeHandler:(std::function<void( int columns, int rows, int pixelWidth, int pixelHeight )>)handler;

/// Feed bytes received from the host. Main queue only.
- (void)feed:(std::string const&)data;

/// Paints as though focused even without a window. Only the render probe uses
/// this; a real view follows its first-responder state.
- (void)setForcesFocusedAppearance:(BOOL)forced;

/// Scrolls `delta` rows towards the history (positive) or the live screen.
- (void)scrollByRows:(int)delta;

/// Returns the view to the live screen.
- (void)scrollToBottom;

/// Drops any selection.
- (void)clearSelection;

/// Selects a range for the render probe, which has no mouse to drag with.
- (void)selectFromRow:(int)fromRow column:(int)fromColumn toRow:(int)toRow column:(int)toColumn;

@end
