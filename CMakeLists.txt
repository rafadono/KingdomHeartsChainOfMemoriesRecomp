cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
project(KHCOMRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/include/SDL.h")
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

# Tool: verify_rom_hash
add_executable(verify_rom_hash tools/verify_rom_hash/main.cpp)
target_include_directories(verify_rom_hash PRIVATE
    "${GBARECOMP_ROOT}/src"
)
target_link_libraries(verify_rom_hash PRIVATE
    gbarecomp_gba
)
set_target_properties(verify_rom_hash PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# KHCOMRecomp game target
file(GLOB KHCOM_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp"
)

set(KHCOM_RUNTIME_SOURCES
    src/main.cpp
    src/game_config.cpp
    src/audio_dsp.cpp
    src/perf_hud.cpp
    src/frame_interpolator.cpp
    src/screen_filters.cpp
    src/input_enhancements.cpp
    src/hd_audio_player.cpp
    src/savestate_thumbnails.cpp
    src/xbrz_filter.cpp
    src/dialog_turbo.cpp
    src/hud_anchoring.cpp
    src/dialogue_enhancer.cpp
    src/dialogue_backlog.cpp
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/dispatch_table.cpp")
    list(APPEND KHCOM_RUNTIME_SOURCES ${KHCOM_GENERATED_SOURCES})
else()
    list(APPEND KHCOM_RUNTIME_SOURCES src/scaffold_stub.cpp)
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

if(COMMAND recomp_target_launcher_ui)
    recomp_target_launcher_ui(KHCOMRecomp CONSOLE gba)
endif()

target_link_libraries(KHCOMRecomp PRIVATE
    gbarecomp_runtime
    gbarecomp_debug
    gbarecomp_armv4t
    gbarecomp_gba
)
set_target_properties(KHCOMRecomp PROPERTIES CXX_SCAN_FOR_MODULES OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib/x64/SDL2.dll")
    add_custom_command(TARGET KHCOMRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:KHCOMRecomp>
    )
endif()
