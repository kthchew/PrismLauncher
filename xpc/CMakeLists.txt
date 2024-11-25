project(xpcservices C OBJC)

################################ FILES ################################

set(QUARANTINEREMOVER_FILES
        QuarantineRemover/main.m
        QuarantineRemover/QuarantineRemovalService.m
)

add_executable(QuarantineRemovalService MACOSX_BUNDLE
        ${QUARANTINEREMOVER_FILES})
target_link_libraries(QuarantineRemovalService
        "-framework Foundation"
        "-framework UniformTypeIdentifiers"
        "-framework AppKit")
set_target_properties(QuarantineRemovalService PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION "xpc"
        MACOSX_BUNDLE True
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MACOSX_BUNDLE_GUI_IDENTIFIER}.QuarantineRemovalService"
        MACOSX_BUNDLE_BUNDLE_NAME "QuarantineRemovalService"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MACOSX_BUNDLE_BUNDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MACOSX_BUNDLE_SHORT_VERSION_STRING}"
        MACOSX_BUNDLE_INFO_PLIST "MacOSXPCBundleInfo.plist.in"
)
