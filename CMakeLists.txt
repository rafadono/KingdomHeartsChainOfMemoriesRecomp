cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
project(KHCOMRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Reusable gba-improvements library (namespace gbaimp). It lives in the
# external/gba-improvements git submodule; -DGBAIMP_ROOT=<path> overrides it
# (e.g. a local checkout). Its own unit tests are not built as a subproject.
set(GBAIMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/gba-improvements"
    CACHE PATH "gba-improvements library root (git submodule)")

if(MSVC)
    # src/msvc_compat.h includes gbaimp/msvc_compat.h; the library headers must be reachable from
    # every translation unit (the force-include applies to the C runtime sources too).
    include_directories("${GBAIMP_ROOT}/include")
    file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/msvc_compat.h" MSVC_COMPAT_HEADER)
    add_compile_options("/FI${MSVC_COMPAT_HEADER}")
endif()

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to gbarecomp platform core (git submodule)")

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT does not point to a gbarecomp checkout: ${GBARECOMP_ROOT}")
endif()

# SDL2 dependency configuration
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/include/SDL.h")
    set(SDL2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/include" CACHE PATH "SDL2 include dir" FORCE)
    set(SDL2_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib/x64/SDL2.lib" CACHE FILEPATH "SDL2 library" FORCE)
endif()

# recomp-ui in-game overlay dependency configuration
set(RECOMP_UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/recomp-ui")
if(EXISTS "${RECOMP_UI_DIR}/src/recomp_runtime_ui.h")
    set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_DIR}" CACHE PATH "Path to recomp-ui" FORCE)
    set(RECOMP_UI_ROOT "${RECOMP_UI_DIR}" CACHE PATH "recomp-ui root" FORCE)
    include("${RECOMP_UI_DIR}/recomp_ui.cmake")
endif()

# Statically recompiled BIOS source directory
set(GBARECOMP_GENERATED_BIOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated_bios"
    CACHE PATH "Directory containing locally generated BIOS recompilation sources" FORCE)

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

if(NOT EXISTS "${GBAIMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBAIMP_ROOT does not point to a gba-improvements checkout: ${GBAIMP_ROOT}\n"
        "Run: git submodule update --init external/gba-improvements\n"
        "or pass -DGBAIMP_ROOT=<path to gba-improvements>.")
endif()

# The library calls find_package(SDL2 REQUIRED). On Windows/MSVC KHR ships the SDL2 "VC"
# devel package in third_party/sdl2, which provides cmake/sdl2-config.cmake (SDL2::SDL2).
# Point find_package at it so the library target gets valid include/link settings.
# (Not buildable in the Docker/Linux environment; validate on Windows.)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/cmake/sdl2-config.cmake")
    set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/cmake" CACHE PATH "SDL2 config dir" FORCE)
endif()

add_subdirectory("${GBAIMP_ROOT}" gba_improvements_build EXCLUDE_FROM_ALL)

# KHCOMRecomp game target
file(GLOB KHCOM_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp"
)

set(KHCOM_RUNTIME_SOURCES
    src/main.cpp
    src/game_config.cpp
    src/gbaimp_integration.cpp
    src/sdl_intercepts.cpp
    src/capture_harness.cpp
    src/runtime_ui_standard.cpp
    src/dialogue_backlog.cpp
    src/text_fonts_data.cpp
    src/text_overlay/overlay_fonts.cpp
    src/text_overlay/overlay_fonts_data.cpp
    src/text_overlay/overlay_layout.cpp
    src/text_overlay/overlay_raster.cpp
    src/text_overlay/overlay_renderer.cpp
    src/ram_overlay_dispatch.cpp
    src/widescreen_adapter.cpp
    src/music_hook.cpp
    src/text_overlay/engine_hooks.cpp
    src/text_overlay/battle_text.cpp
    src/text_overlay/fade_sampler.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/dispatch_table.cpp")
    list(APPEND KHCOM_RUNTIME_SOURCES ${KHCOM_GENERATED_SOURCES})
else()
    list(APPEND KHCOM_RUNTIME_SOURCES src/scaffold_stub.cpp)
endif()

# Suppress compiler warnings for machine-generated translation units
set(GENERATED_SUPPRESS_FLAGS "")
if(MSVC)
    set(GENERATED_SUPPRESS_FLAGS "/w")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(GENERATED_SUPPRESS_FLAGS "-w")
endif()

if(GENERATED_SUPPRESS_FLAGS)
    set_source_files_properties(
        ${KHCOM_GENERATED_SOURCES}
        PROPERTIES COMPILE_OPTIONS "${GENERATED_SUPPRESS_FLAGS}"
    )
    if(TARGET gbarecomp_runtime)
        set_source_files_properties(
            "${GBARECOMP_GENERATED_BIOS_DIR}/bios_dispatch_table.cpp"
            "${GBARECOMP_GENERATED_BIOS_DIR}/bios_recompiled.cpp"
            TARGET_DIRECTORY gbarecomp_runtime
            PROPERTIES COMPILE_OPTIONS "${GENERATED_SUPPRESS_FLAGS}"
        )
    endif()
endif()

add_executable(KHCOMRecomp ${KHCOM_RUNTIME_SOURCES})
target_include_directories(KHCOMRecomp PRIVATE
    src
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${GBARECOMP_ROOT}/src"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/armv4t"
    "${SDL2_INCLUDE_DIR}"
)
target_link_libraries(KHCOMRecomp PRIVATE gbaimp::gba_improvements)

if(COMMAND recomp_target_launcher_ui)
    recomp_target_launcher_ui(KHCOMRecomp CONSOLE gba)
    get_target_property(_khcom_sources KHCOMRecomp SOURCES)
    list(FILTER _khcom_sources EXCLUDE REGEX "runtime_ui_imgui\\.cpp$")
    set_target_properties(KHCOMRecomp PROPERTIES SOURCES "${_khcom_sources}")
    target_sources(KHCOMRecomp PRIVATE src/runtime_ui_imgui_enhanced.cpp)
endif()

if(COMMAND target_link_gbarecomp_runtime_stack)
    target_link_gbarecomp_runtime_stack(KHCOMRecomp)
else()
    if(APPLE OR MSVC)
        target_link_libraries(KHCOMRecomp PRIVATE
            gbarecomp_armv4t gbarecomp_gba gbarecomp_runtime gbarecomp_debug)
    else()
        target_link_libraries(KHCOMRecomp PRIVATE
            -Wl,--start-group
            gbarecomp_armv4t gbarecomp_gba gbarecomp_runtime gbarecomp_debug
            -Wl,--end-group)
    endif()
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(KHCOMRecomp PRIVATE pthread dl m)
endif()

if(NOT MSVC)
    target_link_options(KHCOMRecomp PRIVATE
        "-Wl,--wrap=SDL_PollEvent"
        "-Wl,--wrap=SDL_RenderPresent"
        "-Wl,--wrap=SDL_UpdateTexture"
        "-Wl,--wrap=SDL_RenderCopy"
        "-Wl,--wrap=SDL_QueueAudio"
        "-Wl,--wrap=SDL_OpenAudioDevice"
        "-Wl,--wrap=recomp_runtime_ui_create_standard"
    )
endif()
set_target_properties(KHCOMRecomp PROPERTIES CXX_SCAN_FOR_MODULES OFF)

add_custom_command(TARGET KHCOMRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:KHCOMRecomp> "${CMAKE_CURRENT_SOURCE_DIR}/KHCOMRecomp${CMAKE_EXECUTABLE_SUFFIX}"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib/x64/SDL2.dll")
    # SDL2.dll next to the built exe and next to the deployed copy in the repo root.
    add_custom_command(TARGET KHCOMRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:KHCOMRecomp>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib/x64/SDL2.dll"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

# Native C++ Unit Test Runner Target
add_executable(khcom_unit_tests
    tests/test_all_modules.cpp
    tests/test_overlay_render.cpp
    tests/test_overlay_engine.cpp
    src/gbaimp_integration.cpp
    src/sdl_intercepts.cpp
    src/capture_harness.cpp
    src/runtime_ui_standard.cpp
    src/dialogue_backlog.cpp
    src/text_fonts_data.cpp
    src/text_overlay/overlay_fonts.cpp
    src/text_overlay/overlay_fonts_data.cpp
    src/text_overlay/overlay_layout.cpp
    src/text_overlay/overlay_raster.cpp
    src/text_overlay/overlay_renderer.cpp
    src/widescreen_adapter.cpp
    src/music_hook.cpp
    src/text_overlay/engine_hooks.cpp
    src/text_overlay/battle_text.cpp
    src/text_overlay/fade_sampler.cpp
    src/ram_overlay_dispatch.cpp
    src/scaffold_stub.cpp
    src/game_config.cpp
)
# game_config.cpp (settings registry, khcom_config.ini) is linked for real; scaffold_stub.cpp then only
# provides the empty dispatch table.
target_compile_definitions(khcom_unit_tests PRIVATE KHCOM_TEST_REAL_CONFIG)
target_include_directories(khcom_unit_tests PRIVATE
    src
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${GBARECOMP_ROOT}/src"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/gba"
    "${SDL2_INCLUDE_DIR}"
)
target_link_libraries(khcom_unit_tests PRIVATE gbaimp::gba_improvements)
if(COMMAND recomp_target_launcher_ui)
    recomp_target_launcher_ui(khcom_unit_tests CONSOLE gba)
    get_target_property(_test_sources khcom_unit_tests SOURCES)
    list(FILTER _test_sources EXCLUDE REGEX "runtime_ui_imgui\\.cpp$")
    set_target_properties(khcom_unit_tests PROPERTIES SOURCES "${_test_sources}")
    target_sources(khcom_unit_tests PRIVATE src/runtime_ui_imgui_enhanced.cpp)
endif()
if(COMMAND target_link_gbarecomp_runtime_stack)
    target_link_gbarecomp_runtime_stack(khcom_unit_tests)
else()
    target_link_libraries(khcom_unit_tests PRIVATE
        gbarecomp_armv4t gbarecomp_gba gbarecomp_runtime gbarecomp_debug)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(khcom_unit_tests PRIVATE pthread dl m)
endif()
if(NOT MSVC)
    target_link_options(khcom_unit_tests PRIVATE
        "-Wl,--wrap=SDL_PollEvent"
        "-Wl,--wrap=SDL_RenderPresent"
        "-Wl,--wrap=SDL_UpdateTexture"
        "-Wl,--wrap=SDL_RenderCopy"
        "-Wl,--wrap=SDL_QueueAudio"
        "-Wl,--wrap=SDL_OpenAudioDevice"

        "-Wl,--wrap=recomp_runtime_ui_create_standard"
    )
endif()
set_target_properties(khcom_unit_tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# ---- SDL interception, MSVC side ------------------------------------------------
# GNU ld uses `-Wl,--wrap` (above). MSVC has no equivalent, so:
#  * SDL_PollEvent / SDL_UpdateTexture / SDL_RenderCopy / SDL_RenderPresent /
#    SDL_QueueAudio / SDL_OpenAudioDevice (imported from SDL2.dll) are interposed at run time by
#    gbaimp::install_sdl_interposers() (Import Address Table patching), called from
#    main() through khcom_install_sdl_interposers() in src/sdl_intercepts.cpp. No
#    link option is needed.
#  * recomp_runtime_ui_create_standard (a plain function compiled into this target
#    from recomp-ui) is interposed by renaming the symbol while compiling that one
#    third-party translation unit (no third-party source is modified); the host
#    definition lives in src/runtime_ui_standard.cpp.
if(MSVC AND COMMAND recomp_target_launcher_ui)
    set(_khcom_rui_settings_c "${RECOMP_UI_DIR}/src/common/recomp_runtime_settings.c")
    if(EXISTS "${_khcom_rui_settings_c}")
        set_source_files_properties("${_khcom_rui_settings_c}" PROPERTIES
            COMPILE_DEFINITIONS
            "recomp_runtime_ui_create_standard=khcom_real_recomp_runtime_ui_create_standard")
        target_compile_definitions(KHCOMRecomp PRIVATE KHCOM_RENAME_RUNTIME_UI_CREATE_STANDARD=1)
        target_compile_definitions(khcom_unit_tests PRIVATE KHCOM_RENAME_RUNTIME_UI_CREATE_STANDARD=1)
    endif()
endif()
