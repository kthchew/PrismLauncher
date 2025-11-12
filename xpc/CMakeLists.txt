project(xpcservices LANGUAGES Swift)

# From https://github.com/swiftlang/swift-cmake-examples/blob/main/3_bidirectional_cxx_interop/cmake/modules/AddSwift.cmake
function(_swift_generate_cxx_header target header)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "Target ${target} not defined.")
    endif()

    if(NOT DEFINED CMAKE_Swift_COMPILER)
        message(WARNING "Swift not enabled in project. Cannot generate headers for Swift files.")
        return()
    endif()

    cmake_parse_arguments(ARG "" "" "SEARCH_PATHS;MODULE_NAME" ${ARGN})

    if(NOT ARG_MODULE_NAME)
        set(target_module_name $<TARGET_PROPERTY:${target},Swift_MODULE_NAME>)
        set(ARG_MODULE_NAME $<IF:$<BOOL:${target_module_name}>,${target_module_name},${target}>)
    endif()

    if(ARG_SEARCH_PATHS)
        list(TRANSFORM ARG_SEARCH_PATHS PREPEND "-I")
    endif()

    if(APPLE AND CMAKE_OSX_SYSROOT)
        set(SDK_FLAGS "-sdk" "${CMAKE_OSX_SYSROOT}")
    elseif(WIN32)
        set(SDK_FLAGS "-sdk" "$ENV{SDKROOT}")
    elseif(CMAKE_SYSROOT)
        set(SDK_FLAGS "-sdk" "${CMAKE_SYSROOT}")
    endif()

    cmake_path(APPEND CMAKE_CURRENT_BINARY_DIR include
            OUTPUT_VARIABLE base_path)

    cmake_path(APPEND base_path ${header}
            OUTPUT_VARIABLE header_path)

    cmake_path(APPEND CMAKE_CURRENT_BINARY_DIR "${ARG_MODULE_NAME}.emit-module.d" OUTPUT_VARIABLE depfile_path)

    set(_AllSources $<PATH:ABSOLUTE_PATH,$<TARGET_PROPERTY:${target},SOURCES>,${CMAKE_CURRENT_SOURCE_DIR}>)
    set(_SwiftSources $<FILTER:${_AllSources},INCLUDE,\\.swift$>)
    add_custom_command(OUTPUT ${header_path}
            DEPENDS ${_SwiftSources}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMAND
            ${CMAKE_Swift_COMPILER} -typecheck
            ${ARG_SEARCH_PATHS}
            ${_SwiftSources}
            ${SDK_FLAGS}
            -module-name "${ARG_MODULE_NAME}"
            -cxx-interoperability-mode=default
            -emit-clang-header-path ${header_path}
            -emit-dependencies
            DEPFILE "${depfile_path}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT
            "Generating '${header_path}'"
            COMMAND_EXPAND_LISTS)

    # Added to public interface for dependees to find.
    target_include_directories(${target} PUBLIC ${base_path})
    # Added to the target to ensure target rebuilds if header changes and is used
    # by sources in the target.
    target_sources(${target} PRIVATE ${header_path})
endfunction()

################################ FILES ################################

set(SANDBOXSERVICE_FILES
        SandboxService/main.swift
        SandboxService/PrismSandboxService.swift
        SandboxService/PrismSandboxServiceProtocol.swift
)

add_executable(SandboxService MACOSX_BUNDLE
        ${SANDBOXSERVICE_FILES})
target_link_libraries(SandboxService
        "-framework Foundation"
        "-framework UniformTypeIdentifiers"
        "-framework AppKit")
set_target_properties(SandboxService PROPERTIES
        BUNDLE True
        BUNDLE_EXTENSION "xpc"
        MACOSX_BUNDLE True
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MACOSX_BUNDLE_GUI_IDENTIFIER}.PrismSandboxService"
        MACOSX_BUNDLE_BUNDLE_NAME "PrismSandboxService"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MACOSX_BUNDLE_BUNDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${MACOSX_BUNDLE_SHORT_VERSION_STRING}"
        MACOSX_BUNDLE_INFO_PLIST "MacOSXPCBundleInfo.plist.in"
        OUTPUT_NAME "PrismSandboxService"
)
_swift_generate_cxx_header(SandboxService
        SandboxProtocol.h
)
