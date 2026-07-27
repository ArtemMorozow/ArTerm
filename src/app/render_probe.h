#pragma once

#import <Cocoa/Cocoa.h>

/// Renders a set of terminal-view cases offscreen into `<directory>/<case>.png`
/// and returns whether all of them were written. Driven by
/// ARTERM_RENDER_PROBE=<directory>, which makes the process write the images
/// and exit without ever showing a window.
///
/// This exists because the drawing code is the one part of ArTerm that cannot
/// be asserted on in a unit test: the probe turns "does the grid line up" into
/// an artefact that can actually be looked at, in CI or by hand.
bool arterm_write_render_probe( NSString* directory );
