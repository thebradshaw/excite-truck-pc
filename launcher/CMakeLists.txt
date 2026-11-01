# Excite PC launcher: the window players open first. It lists Excite Truck and Excitebots, sets
# each one up from the player's disc (running its build script with a progress bar), shows each
# game's settings (written to that game's Config.toml) and starts the game.
#
# It needs none of the games' code, so it is built on its own and ships ready to run:
#     cmake -S launcher -B build/launcher -G Ninja
# SDL3 and imgui are fetched (or taken from FETCHCONTENT_SOURCE_DIR_SDL / _IMGUI); toml11 comes
# from the WiiCompiled submodule (ET_TOML11_DIR).
cmake_minimum_required(VERSION 3.25)
project(ExcitePC LANGUAGES C CXX)

set(ET_LAUNCHER_VERSION "2.0.0")
include(FetchContent)

# --- SDL3, imgui and toml11 -------------------------------------------------------------------
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
set(ET_TOML11_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wiicompiled/runtime/third_party/toml11" CACHE PATH
    "Folder holding toml11's single-header toml.hpp (v4)")
if(NOT EXISTS "${ET_TOML11_DIR}/toml.hpp")
    message(FATAL_ERROR "toml.hpp not found in ${ET_TOML11_DIR}: run git submodule update --init, or set ET_TOML11_DIR")
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

add_executable(ExcitePC
    src/main.cpp
    src/achievements.cpp
    src/banner.cpp
    src/games.cpp
    src/launcher_config.cpp
    src/setup_job.cpp
    src/scene.cpp
    src/ui.cpp
    "${ET_LAUNCHER_FONTS_CPP}"
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
target_include_directories(ExcitePC PRIVATE
    src
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
target_include_directories(ExcitePC SYSTEM PRIVATE "${ET_TOML11_DIR}")
target_compile_features(ExcitePC PRIVATE cxx_std_20)
target_compile_definitions(ExcitePC PRIVATE
    ET_LAUNCHER_VERSION="${ET_LAUNCHER_VERSION}"
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_DISABLE_DEMO_WINDOWS
    IMGUI_DISABLE_DEBUG_TOOLS)
target_link_libraries(ExcitePC PRIVATE SDL3::SDL3-static)

if(WIN32)
    set_target_properties(ExcitePC PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(ExcitePC PRIVATE shell32 ole32)
    if(MINGW)
        # One file to ship: no libstdc++/libgcc/winpthread DLLs next to it.
        target_link_options(ExcitePC PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/res/excite-pc.ico" ET_LAUNCHER_ICON)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/launcher.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" @ONLY)
    target_sources(ExcitePC PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" PROPERTIES OBJECT_DEPENDS "${ET_LAUNCHER_ICON}")
endif()
