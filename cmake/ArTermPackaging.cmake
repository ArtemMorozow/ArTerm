# macOS bundle deployment.
#
#   cmake --build build --target bundle   # runs macdeployqt on ArTerm.app
#   cmake --build build --target dmg      # produces ArTerm-<version>.dmg
#
# Both targets only exist on macOS; elsewhere the file is a no-op.

if(NOT APPLE)
    return()
endif()

# macdeployqt sits next to qmake in every Qt installation.
get_target_property(_qt_qmake_executable Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qt_qmake_executable}" DIRECTORY)

find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")

if(NOT MACDEPLOYQT_EXECUTABLE)
    message(WARNING "macdeployqt was not found; the bundle and dmg targets are unavailable")
    return()
endif()

set(ARTERM_BUNDLE "$<TARGET_BUNDLE_DIR:arterm>")

add_custom_target(bundle
    COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${ARTERM_BUNDLE}" -always-overwrite
    DEPENDS arterm
    COMMENT "Copying the Qt frameworks into ArTerm.app"
    VERBATIM)

add_custom_target(dmg
    COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${ARTERM_BUNDLE}" -dmg -always-overwrite
    DEPENDS arterm
    COMMENT "Building ArTerm.dmg"
    VERBATIM)

# Ad-hoc signing keeps Gatekeeper from refusing a locally built bundle. Set
# ARTERM_CODESIGN_IDENTITY to a Developer ID for a distributable build.
set(ARTERM_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for the macOS bundle")

add_custom_target(sign
    COMMAND codesign --force --deep --sign "${ARTERM_CODESIGN_IDENTITY}"
            --options runtime "${ARTERM_BUNDLE}"
    DEPENDS bundle
    COMMENT "Signing ArTerm.app with identity ${ARTERM_CODESIGN_IDENTITY}"
    VERBATIM)
