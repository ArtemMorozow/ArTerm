# ArTerm

An SSH client and SFTP/SCP file manager for macOS, in the spirit of MobaXterm:
a terminal and a dual-pane file browser in the same window, with drag-and-drop
transfers between them.

Written in C++23, built with clang and CMake. The interface is AppKit; libssh2
is the only third-party dependency.

> **Migration in progress.** ArTerm is being moved off Qt onto native AppKit.
> `core/` and `terminal/` are already pure C++23 and build without Qt; `ssh/`,
> `model/`, `session/`, `files/` and `ui/` are still Qt and are not compiled
> yet. See the source list at the bottom of `src/CMakeLists.txt`.

## What it does

**Terminal**
- xterm-compatible emulator written from scratch: CSI/OSC/DCS parsing, SGR with
  16/256/true-colour, alternate screen, scroll regions, insert/delete, DEC line
  drawing, origin and auto-wrap modes, bracketed paste, focus reporting.
- Mouse tracking in all four modes (X10/1000/1002/1003) with the default, UTF-8,
  SGR and urxvt encodings.
- UTF-8 decoding with proper handling of overlong and truncated sequences, and a
  self-contained `wcwidth` so CJK and emoji occupy two columns regardless of the
  process locale.
- Scrollback with word and line selection, copy/paste, adjustable font size.

**Files**
- Dual-pane browser: local on the left, remote on the right.
- Drag and drop in every direction — local→remote uploads, remote→local
  downloads, remote→remote renames server-side, and a drop onto a folder row
  lands inside that folder. Dropping files on the terminal uploads them to the
  directory the remote pane is showing.
- Directories transfer recursively, with a queue, per-file progress, transfer
  rates and cancellation.
- Create, rename and delete on both sides.

**Hosts**
- Sidebar with grouping and search; import from `~/.ssh/config`.
- Agent, public key, password and keyboard-interactive authentication.
- `known_hosts` verification sharing OpenSSH's file, with a fingerprint dialog
  that treats a *changed* key very differently from a first connection.
- Passwords and passphrases live in the macOS Keychain, never in the profile
  file.

## Building

```sh
brew install libssh2 cmake ninja

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$(brew --prefix libssh2)"

cmake --build build
```

Once the AppKit layer lands the result is `build/src/ArTerm.app`. Because
nothing but libssh2 is linked from outside the system, the bundle needs no
framework deployment step:

```sh
cmake --build build --target sign     # ad-hoc signature; override with
                                      # -DARTERM_CODESIGN_IDENTITY="Developer ID..."
cmake --build build --target dmg      # produces a .dmg
```

Universal (arm64 + x86_64) is the default; override with
`-DCMAKE_OSX_ARCHITECTURES=arm64` for a single-arch build.

### Tests

```sh
ctest --test-dir build --output-on-failure
```

The suite covers the parser, screen buffer, emulator, key encoding, character
widths, colour scheme and host store — the parts that are pure logic and
therefore worth testing. Nothing in it touches the UI, so it needs no display.

The tests are being moved from QTest to Catch2 along with the sources they
cover; `tests/` currently builds no targets.

### Other platforms

There are none. ArTerm targets macOS 13 and later and talks to AppKit,
CoreText, libdispatch and Security.framework directly; CMake refuses to
configure anywhere else.

## Layout

```
src/
  core/       Result/Error type used throughout
  ssh/        libssh2: connection, auth, known_hosts, shell channel, SFTP, SCP
  terminal/   VT parser, screen buffer, emulator, key encoder, rendering widget
  session/    GUI-thread facades over the SSH worker threads
  files/      File models, dual-pane browser, transfer queue
  model/      Host profiles and Keychain-backed secrets
  ui/         Window, sidebar, dialogs, theme
```

### Threading

Each session opens **two** SSH connections: one drives the interactive shell,
one drives SFTP. A libssh2 session may not be used from two threads, and putting
both on one connection would make the terminal stutter during a large transfer.
Each connection lives on its own worker thread; the GUI thread only ever talks
to the `session::` facades through queued signals.

The shell connection switches to non-blocking mode after login and is pumped by
a socket notifier, so output appears without polling. The SFTP connection stays
blocking, which is why it needs its own thread — a multi-gigabyte download runs
there without touching the event loop.

### Theming

All colours come from `ui::Theme`. The stylesheet in
`resources/theme/arterm-dark.qss` is written against `@token` placeholders that
the theme substitutes, so the same file serves both the dark and light
appearance. ArTerm follows the system setting; `ARTERM_THEME=dark|light`
overrides it.

## Notes and limits

- **Dragging to Finder is not supported.** A remote file has no local path, so a
  drag out of the remote pane carries an ArTerm-private payload rather than a
  file URL. Doing it properly needs macOS file promises
  (`NSFilePromiseProvider`), which is not wired up yet. Dragging *from* Finder
  into ArTerm works normally.
- Transfers run one at a time, because they share a single SFTP connection.
- SCP is a per-host setting that only affects how file contents are copied;
  directory listings always use SFTP, since SCP cannot enumerate a directory.
- Without a keychain (i.e. off macOS) secrets are not saved and ArTerm prompts
  per connection rather than writing them to a file.
