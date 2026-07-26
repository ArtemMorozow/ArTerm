# macOS bundle deployment.
#
#   cmake --build build --target sign     # ad-hoc signs ArTerm.app
#   cmake --build build --target dmg      # produces ArTerm-<version>.dmg
#
# There is nothing to deploy into the bundle any more: ArTerm links only the
# system frameworks plus libssh2, so `macdeployqt` and its Qt framework copy are
# gone. Only signing and disk-image packaging remain.

if(NOT APPLE)
    return()
endif()

if(NOT TARGET arterm)
    # The application target comes back with the AppKit layer.
    return()
endif()

set(ARTERM_BUNDLE "$<TARGET_BUNDLE_DIR:arterm>")

# Ad-hoc signing keeps Gatekeeper from refusing a locally built bundle. Set
# ARTERM_CODESIGN_IDENTITY to a Developer ID for a distributable build.
set(ARTERM_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for the macOS bundle")

add_custom_target(sign
    COMMAND codesign --force --deep --sign "${ARTERM_CODESIGN_IDENTITY}"
            --options runtime "${ARTERM_BUNDLE}"
    DEPENDS arterm
    COMMENT "Signing ArTerm.app with identity ${ARTERM_CODESIGN_IDENTITY}"
    VERBATIM)

add_custom_target(dmg
    COMMAND hdiutil create -volname "ArTerm" -srcfolder "${ARTERM_BUNDLE}" -ov -format UDZO
            "${CMAKE_BINARY_DIR}/ArTerm-${PROJECT_VERSION}.dmg"
    DEPENDS sign
    COMMENT "Building ArTerm-${PROJECT_VERSION}.dmg"
    VERBATIM)
