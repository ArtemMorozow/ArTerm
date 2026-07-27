# ---------------------------------------------------------------------------
# arterm_core - the platform-independent half of ArTerm: the VT emulator, and
# in time the SSH transport and the data model. No AppKit, no Objective-C, so
# it can be exercised entirely from a plain C++ test binary.
#
# Migration in progress: the modules still written against Qt are listed at the
# bottom of this file and are not compiled yet.
# ---------------------------------------------------------------------------
add_library(arterm_core STATIC
    core/non_copyable.hpp
    core/result.hpp
    core/rgb.hpp
    core/signal.hpp
    core/base64.hpp
    core/base64.cpp
    core/dispatch.hpp
    core/dispatch.cpp
    core/log.hpp
    core/log.cpp
    core/paths.hpp
    core/paths.cpp
    core/json.hpp
    core/json.cpp
    core/uuid.hpp
    core/uuid.cpp

    terminal/box_drawing.hpp
    terminal/box_drawing.cpp
    terminal/cell.hpp
    terminal/char_width.hpp
    terminal/char_width.cpp
    terminal/color_scheme.hpp
    terminal/color_scheme.cpp
    terminal/key_event.hpp
    terminal/key_encoder.hpp
    terminal/key_encoder.cpp
    terminal/selection.hpp
    terminal/selection.cpp
    terminal/screen.hpp
    terminal/screen.cpp
    terminal/vt_parser.hpp
    terminal/vt_parser.cpp
    terminal/terminal.hpp
    terminal/terminal.cpp

    ssh/ssh_types.hpp
    ssh/ssh_types.cpp
    ssh/ssh_library.hpp
    ssh/ssh_library.cpp
    ssh/known_hosts.hpp
    ssh/known_hosts.cpp
    ssh/session_interaction.hpp
    ssh/session_interaction.cpp
    ssh/ssh_connection.hpp
    ssh/ssh_connection.cpp
    ssh/shell_session.hpp
    ssh/shell_session.cpp
    ssh/scp_transfer.hpp
    ssh/scp_transfer.cpp
    ssh/sftp_session.hpp
    ssh/sftp_session.cpp

    model/secret_store.hpp
    model/secret_store.cpp
    model/host_store.hpp
    model/host_store.cpp)

target_include_directories(arterm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(arterm_core
    PUBLIC
        arterm::flags
        arterm::libssh2
        Threads::Threads
        "-framework Security"
        "-framework CoreFoundation")

# ---------------------------------------------------------------------------
# The application: the AppKit layer in Objective-C++ on top of arterm_core.
# ---------------------------------------------------------------------------
add_executable(arterm MACOSX_BUNDLE
    app/main.mm
    app/app_delegate.h
    app/app_delegate.mm
    app/main_window_controller.h
    app/main_window_controller.mm
    app/host_list_controller.h
    app/host_list_controller.mm
    app/host_editor_controller.h
    app/host_editor_controller.mm
    app/terminal_view.h
    app/terminal_view.mm
    app/render_probe.h
    app/render_probe.mm
    app/session_view_controller.h
    app/session_view_controller.mm)

target_link_libraries(arterm PRIVATE
    arterm_core
    "-framework Cocoa")

target_compile_options(arterm PRIVATE -fobjc-arc)

set_target_properties(arterm PROPERTIES
    OUTPUT_NAME "ArTerm"
    MACOSX_BUNDLE_BUNDLE_NAME "ArTerm"
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.arterm.ArTerm"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "© ArTerm contributors"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")

install(TARGETS arterm BUNDLE DESTINATION .)

# ---------------------------------------------------------------------------
# Not yet migrated off Qt. Each block moves up as its AppKit replacement lands.
# ---------------------------------------------------------------------------
#   files/    file_list_model, local/remote_file_model, transfer_manager,
#             file_mime_data, file_pane, transfer_panel, file_browser
#   ui/       theme, icons, host_sidebar, host_editor_dialog, host_key_dialog,
#             session_tab, main_window
#   terminal/ terminal_widget - superseded by the AppKit view, not ported
#
# The session/ facades are gone rather than pending: their whole job was
# owning the Qt worker threads, and the sessions own their dispatch queues now,
# so the AppKit layer talks to ShellSession/SftpSession directly.
