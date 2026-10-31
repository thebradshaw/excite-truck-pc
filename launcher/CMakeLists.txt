# Excite Truck launcher: the window players open first. It shows the main menu and the most used
# settings, writes them to the game's Config.toml and then starts the game.
#
# Two ways to build it:
#  - Inside the WiiCompiled runtime tree (what the build scripts do): they copy this folder to
#    runtime/et_launcher and add it with add_subdirectory, so it reuses aurora's SDL3 and imgui.
#  - On its own, for working on the launcher without the game:
#        cmake -S launcher -B build/launcher -G Ninja -DET_WIICOMPILED_DIR=<patched WiiCompiled>
#    SDL3 and imgui are then fetched (or taken from FETCHCONTENT_SOURCE_DIR_SDL / _IMGUI).
cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ExciteTruckLauncher LANGUAGES C CXX)
    set(ET_LAUNCHER_STANDALONE ON)
endif()

set(ET_LAUNCHER_VERSION "1.0.0")
include(FetchContent)

# --- SDL3 and imgui ---------------------------------------------------------------------------
if(ET_LAUNCHER_STANDALONE)
    set(ET_WIICOMPILED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wiicompiled" CACHE PATH
        "WiiCompiled checkout with the Excite Truck patches applied (for runtime_config.h)")
    set(ET_RUNTIME_DIR "${ET_WIICOMPILED_DIR}/runtime")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.10
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b-docking
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(sdl)
    FetchContent_GetProperties(imgui)
    if(NOT imgui_POPULATED)
        FetchContent_Populate(imgui)
    endif()
    set(ET_SDL3_TARGET SDL3::SDL3-static)
    add_library(et_launcher_toml11 INTERFACE)
    target_include_directories(et_launcher_toml11 SYSTEM INTERFACE "${ET_RUNTIME_DIR}/third_party/toml11")
    set(ET_TOML11_TARGET et_launcher_toml11)
else()
    # Inside runtime/: aurora has already fetched SDL3 and imgui.
    set(ET_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    set(ET_SDL3_TARGET ${AURORA_SDL3_TARGET})
    FetchContent_GetProperties(imgui)
    set(ET_TOML11_TARGET mkw::toml11)
endif()
if(NOT imgui_SOURCE_DIR)
    message(FATAL_ERROR "Excite Truck launcher: imgui sources were not found")
endif()

# --- Fonts, compiled into the program so the launcher has no loose files ----------------------
set(ET_LAUNCHER_FONTS
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Barlow-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Barlow-SemiBold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/fonts/BarlowCondensed-ExtraBoldItalic.ttf")
set(ET_LAUNCHER_FONTS_CPP "${CMAKE_CURRENT_BINARY_DIR}/launcher_fonts.cpp")
add_custom_command(
    OUTPUT "${ET_LAUNCHER_FONTS_CPP}"
    COMMAND "${CMAKE_COMMAND}" "-DOUT=${ET_LAUNCHER_FONTS_CPP}" "-DFONTS=${ET_LAUNCHER_FONTS}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFonts.cmake"
    DEPENDS ${ET_LAUNCHER_FONTS} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFonts.cmake"
    VERBATIM)

add_executable(ExciteTruckLauncher
    src/main.cpp
    src/achievements.cpp
    src/banner.cpp
    src/launcher_config.cpp
    src/scene.cpp
    src/ui.cpp
    "${ET_LAUNCHER_FONTS_CPP}"
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(ExciteTruckLauncher PRIVATE
    src
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${ET_RUNTIME_DIR}/include")
target_compile_features(ExciteTruckLauncher PRIVATE cxx_std_20)
target_compile_definitions(ExciteTruckLauncher PRIVATE
    ET_LAUNCHER_VERSION="${ET_LAUNCHER_VERSION}"
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_DISABLE_DEMO_WINDOWS
    IMGUI_DISABLE_DEBUG_TOOLS)
# The runtime tree builds with UNITY_BUILD; imgui and the backends don't mix well in one unit.
set_target_properties(ExciteTruckLauncher PROPERTIES UNITY_BUILD OFF)
target_link_libraries(ExciteTruckLauncher PRIVATE ${ET_SDL3_TARGET} ${ET_TOML11_TARGET})

if(WIN32)
    set_target_properties(ExciteTruckLauncher PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(ExciteTruckLauncher PRIVATE shell32 ole32)
    # Same icon as the game (the build script writes the game's own icon there).
    if(EXISTS "${ET_RUNTIME_DIR}/resources/WiiCompiled.ico")
        file(TO_CMAKE_PATH "${ET_RUNTIME_DIR}/resources/WiiCompiled.ico" ET_LAUNCHER_ICON)
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/launcher.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY)
        target_sources(ExciteTruckLauncher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc")
        set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" PROPERTIES OBJECT_DEPENDS "${ET_LAUNCHER_ICON}")
    endif()
endif()

# Next to the game in the build folder, so the packaging step finds both together.
if(NOT ET_LAUNCHER_STANDALONE)
    set_target_properties(ExciteTruckLauncher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
