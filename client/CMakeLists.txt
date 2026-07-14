cmake_minimum_required(VERSION 3.16)
# CXX is enabled too (even though tinypad's own sources are all C) because
# webview's core library is C++ under the hood; enabling it here at the
# normal top-level time avoids CMake's CXX compiler probe running lazily
# and unreliably from inside the FetchContent'd subdirectory.
project(tinypad C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # MSVC's own vcruntime_c11_stdatomic.h refuses to compile under /std:c11
    # unless this is also set -- unrelated to whether our code uses atomics
    # at all, just how MSVC gates its (still-experimental) C11 support.
    add_compile_options($<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# --- webview (UI shell) ----------------------------------------------------
# Fetched rather than vendored: as of 0.12.0 the project is a full CMake
# library (WebView2/WebKitGTK/WKWebView backends selected internally per
# platform), not a drop-in single header anymore.
include(FetchContent)

set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_AMALGAMATION OFF CACHE BOOL "" FORCE)
set(WEBVIEW_INSTALL_TARGETS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_PACKAGING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview.git
    GIT_TAG 0.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(webview)

# --- sources ----------------------------------------------------------------
set(TINYPAD_SOURCES
    src/main.c
    src/core/mixer_state.c
    src/core/macro_map.c
    src/core/device_settings.c
    src/core/device_link.c
    src/ui/ui_bridge.c
)

if(WIN32)
    list(APPEND TINYPAD_SOURCES
        src/platform/serial/serial_windows.c
        src/platform/audio/audio_windows_wasapi.c
        src/platform/keyboard/keyboard_windows.c
    )
elseif(APPLE)
    list(APPEND TINYPAD_SOURCES
        src/platform/serial/serial_posix.c
        src/platform/audio/audio_macos_stub.c
        src/platform/keyboard/keyboard_macos.c
    )
else()
    list(APPEND TINYPAD_SOURCES
        src/platform/serial/serial_posix.c
        src/platform/audio/audio_linux_pipewire.c
        src/platform/keyboard/keyboard_linux.c
    )
endif()

add_executable(tinypad ${TINYPAD_SOURCES})

target_include_directories(tinypad PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)

target_link_libraries(tinypad PRIVATE webview::core_static)

if(WIN32)
    target_link_libraries(tinypad PRIVATE ole32 oleaut32 user32)

    # webview only ever fetches WebView2's *headers* (MSWebView2::headers,
    # an include-dir-only interface target -- see
    # _deps/webview-src/cmake/modules/FindMSWebView2.cmake); it never
    # copies the loader DLL anywhere. WebView2Loader.dll is a real runtime
    # dependency the app must ship next to its own .exe -- it is NOT part
    # of the system-wide WebView2 Runtime install (that only provides
    # msedgewebview2.exe/the browser process, discovered *through* this
    # loader). Without it, webview_create() just returns NULL with no
    # diagnostic. Same NuGet package FindMSWebView2.cmake already fetched,
    # so no extra download.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(TINYPAD_WEBVIEW2_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64|x64")
        set(TINYPAD_WEBVIEW2_ARCH "x64")
    else()
        set(TINYPAD_WEBVIEW2_ARCH "x86")
    endif()

    add_custom_command(TARGET tinypad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/_deps/microsoft_web_webview2-src/build/native/${TINYPAD_WEBVIEW2_ARCH}/WebView2Loader.dll"
            "$<TARGET_FILE_DIR:tinypad>"
        COMMENT "Copying WebView2Loader.dll (${TINYPAD_WEBVIEW2_ARCH}) next to tinypad.exe"
    )
elseif(APPLE)
    # Audio is a stub (no extra libs there); webview::core_static already
    # pulls in WebKit/Cocoa via webview's own CMake. ApplicationServices is
    # for keyboard_macos.c's CGEventPost-based keystroke injection.
    target_link_libraries(tinypad PRIVATE "-framework ApplicationServices")
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    target_link_libraries(tinypad PRIVATE PkgConfig::PIPEWIRE)

    # X11 + XTest for keyboard_linux.c's keystroke injection (X11/XWayland
    # only -- see README for the native-Wayland caveat).
    find_package(X11 REQUIRED)
    if(NOT X11_Xtst_FOUND)
        message(FATAL_ERROR "libXtst (X11 XTest extension) not found -- needed for macro "
                            "keystroke injection; install e.g. libxtst-dev")
    endif()
    target_include_directories(tinypad PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(tinypad PRIVATE ${X11_LIBRARIES} ${X11_Xtst_LIB})
endif()

# --- build the React/Tailwind/shadcn frontend (client/ui) and ship it --------
# ui/ is a Vite project (`npm run build` -> ui/dist/), not raw static files
# copied as-is. Two custom commands, chained by CMake's normal file-based
# DEPENDS so ninja/make only redo the expensive step that actually needs it:
#   1. `npm install`, gated on package.json/package-lock.json only.
#   2. `npm run build` + copy dist/ next to the executable, gated on the
#      install stamp plus every real source file (globbed with
#      CONFIGURE_DEPENDS so new/removed files are picked up on reconfigure).
# Excludes node_modules/dist explicitly -- globbing those would be enormous
# and, for dist/, circular (it's this rule's own output).
find_program(NPM_EXECUTABLE npm REQUIRED)

set(TINYPAD_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)
file(GLOB_RECURSE TINYPAD_UI_SOURCES CONFIGURE_DEPENDS
    ${TINYPAD_UI_DIR}/src/*
    ${TINYPAD_UI_DIR}/index.html
)
list(APPEND TINYPAD_UI_SOURCES
    ${TINYPAD_UI_DIR}/vite.config.ts
    ${TINYPAD_UI_DIR}/tsconfig.json
    ${TINYPAD_UI_DIR}/tsconfig.app.json
    ${TINYPAD_UI_DIR}/tsconfig.node.json
    ${TINYPAD_UI_DIR}/components.json
)

add_custom_command(
    OUTPUT ${TINYPAD_UI_DIR}/node_modules/.install.stamp
    COMMAND ${NPM_EXECUTABLE} install
    COMMAND ${CMAKE_COMMAND} -E touch ${TINYPAD_UI_DIR}/node_modules/.install.stamp
    WORKING_DIRECTORY ${TINYPAD_UI_DIR}
    DEPENDS ${TINYPAD_UI_DIR}/package.json ${TINYPAD_UI_DIR}/package-lock.json
    COMMENT "Installing client/ui npm dependencies"
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui.stamp
    COMMAND ${NPM_EXECUTABLE} run build
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${TINYPAD_UI_DIR}/dist
        ${CMAKE_CURRENT_BINARY_DIR}/ui
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/ui.stamp
    WORKING_DIRECTORY ${TINYPAD_UI_DIR}
    DEPENDS ${TINYPAD_UI_SOURCES} ${TINYPAD_UI_DIR}/node_modules/.install.stamp
    COMMENT "Building client/ui (Vite/React) frontend"
)

# Copies to CMAKE_CURRENT_BINARY_DIR rather than $<TARGET_FILE_DIR:tinypad>:
# the latter would make this custom command depend on tinypad's own target
# properties while tinypad also depends on this command's target, a cycle
# CMake rejects. This matches tinypad's actual output location for the
# single-config generators (Ninja/Make) this project targets; a multi-config
# generator (Visual Studio/Xcode) would need CMAKE_RUNTIME_OUTPUT_DIRECTORY
# pinned to match.
add_custom_target(tinypad_ui_assets ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/ui.stamp)
add_dependencies(tinypad tinypad_ui_assets)

# --- tests -------------------------------------------------------------------
# Exercises core/ (protocol framing, macro_map, device_settings, mixer_state)
# against a fake audio_backend_vtable_t -- no webview, no real audio/keyboard
# platform backend, so it builds and runs identically on every OS/CI.
enable_testing()

add_executable(tinypad_tests
    tests/test_core.c
    src/core/mixer_state.c
    src/core/macro_map.c
    src/core/device_settings.c
)
target_include_directories(tinypad_tests PRIVATE
    include
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared
)
if(UNIX AND NOT APPLE)
    target_link_libraries(tinypad_tests PRIVATE m) # lroundf (mixer_state.c)
endif()
add_test(NAME tinypad_tests COMMAND tinypad_tests)
