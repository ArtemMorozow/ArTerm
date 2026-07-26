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

    terminal/cell.hpp
    terminal/char_width.hpp
    terminal/char_width.cpp
    terminal/color_scheme.hpp
    terminal/color_scheme.cpp
    terminal/key_event.hpp
    terminal/key_encoder.hpp
    terminal/key_encoder.cpp
    terminal/screen.hpp
    terminal/screen.cpp
    terminal/vt_parser.hpp
    terminal/vt_parser.cpp
    terminal/terminal.hpp
    terminal/terminal.cpp

    ssh/ssh_types.hpp
    ssh/ssh_types.cpp)

target_include_directories(arterm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(arterm_core
    PUBLIC
        arterm::flags
        Threads::Threads)

# ---------------------------------------------------------------------------
# Not yet migrated off Qt. Each block moves up into arterm_core as it lands.
# ---------------------------------------------------------------------------
#   ssh/      ssh_library, ssh_connection, known_hosts, session_interaction,
#             shell_session, sftp_session, scp_transfer
#   model/    host_store, secret_store
#   session/  shell_service, remote_file_service
#   files/    file_list_model, local/remote_file_model, transfer_manager,
#             file_mime_data, file_pane, transfer_panel, file_browser
#   ui/       theme, icons, host_sidebar, host_editor_dialog, host_key_dialog,
#             session_tab, main_window
#   terminal/ terminal_widget - superseded by the AppKit view, not ported
#
# The application target returns with the AppKit layer; until then there is no
# main() to link.
