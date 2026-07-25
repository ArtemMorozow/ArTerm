# ---------------------------------------------------------------------------
# arterm_core - everything except main(), so the tests can link against it.
# ---------------------------------------------------------------------------
add_library(arterm_core STATIC
    core/Result.hpp

    ssh/SshTypes.hpp
    ssh/SshTypes.cpp
    ssh/SshLibrary.hpp
    ssh/SshLibrary.cpp
    ssh/SshConnection.hpp
    ssh/SshConnection.cpp
    ssh/KnownHosts.hpp
    ssh/KnownHosts.cpp
    ssh/SessionInteraction.hpp
    ssh/SessionInteraction.cpp
    ssh/ShellSession.hpp
    ssh/ShellSession.cpp
    ssh/SftpSession.hpp
    ssh/SftpSession.cpp
    ssh/ScpTransfer.hpp
    ssh/ScpTransfer.cpp

    terminal/Cell.hpp
    terminal/CharWidth.hpp
    terminal/CharWidth.cpp
    terminal/VtParser.hpp
    terminal/VtParser.cpp
    terminal/Screen.hpp
    terminal/Screen.cpp
    terminal/Terminal.hpp
    terminal/Terminal.cpp
    terminal/ColorScheme.hpp
    terminal/ColorScheme.cpp
    terminal/KeyEncoder.hpp
    terminal/KeyEncoder.cpp
    terminal/TerminalWidget.hpp
    terminal/TerminalWidget.cpp

    session/ShellService.hpp
    session/ShellService.cpp
    session/RemoteFileService.hpp
    session/RemoteFileService.cpp

    model/HostStore.hpp
    model/HostStore.cpp
    model/SecretStore.hpp
    model/SecretStore.cpp

    files/FileListModel.hpp
    files/FileListModel.cpp
    files/FileMimeData.hpp
    files/FileMimeData.cpp
    files/LocalFileModel.hpp
    files/LocalFileModel.cpp
    files/RemoteFileModel.hpp
    files/RemoteFileModel.cpp
    files/TransferManager.hpp
    files/TransferManager.cpp
    files/FilePane.hpp
    files/FilePane.cpp
    files/TransferPanel.hpp
    files/TransferPanel.cpp
    files/FileBrowser.hpp
    files/FileBrowser.cpp

    ui/Theme.hpp
    ui/Theme.cpp
    ui/Icons.hpp
    ui/Icons.cpp
    ui/HostSidebar.hpp
    ui/HostSidebar.cpp
    ui/HostEditorDialog.hpp
    ui/HostEditorDialog.cpp
    ui/HostKeyDialog.hpp
    ui/HostKeyDialog.cpp
    ui/SessionTab.hpp
    ui/SessionTab.cpp
    ui/MainWindow.hpp
    ui/MainWindow.cpp)

target_include_directories(arterm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(arterm_core
    PUBLIC
        arterm::flags
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
        arterm::libssh2
        Threads::Threads)

if(APPLE)
    # SecretStore talks to the login keychain through Security.framework's C API,
    # so no Objective-C is involved.
    target_link_libraries(arterm_core PUBLIC "-framework Security" "-framework CoreFoundation")
endif()

# ---------------------------------------------------------------------------
# The application.
# ---------------------------------------------------------------------------
# The .qrc is compiled into the executable rather than into arterm_core: a
# static library's resource initialiser is not referenced by any symbol, so the
# linker discards the object and every icon silently disappears.
qt_add_executable(arterm
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

target_link_libraries(arterm PRIVATE arterm_core)

set_target_properties(arterm PROPERTIES
    OUTPUT_NAME "ArTerm"
    WIN32_EXECUTABLE OFF)

if(APPLE)
    set_target_properties(arterm PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_BUNDLE_NAME "ArTerm"
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.arterm.ArTerm"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "© ArTerm contributors"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")
endif()

install(TARGETS arterm
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin)
