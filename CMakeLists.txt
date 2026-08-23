cmake_minimum_required(VERSION 3.16)
project(YouTubeMusicPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────
# ncurses (wide-character version for UTF-8 support)
find_package(Curses REQUIRED)

# pthreads
find_package(Threads REQUIRED)

# nlohmann/json — bundled as single header in include/nlohmann/json.hpp
# No FetchContent or find_package needed.

# ── Sources ───────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/utils.cpp
    src/youtube_searcher.cpp
    src/audio_player.cpp
    src/playlist_manager.cpp
    src/ui.cpp
)

# ── Executable ────────────────────────────────────────────────────────
add_executable(music_player ${SOURCES})

target_include_directories(music_player PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CURSES_INCLUDE_DIRS}
)

target_link_libraries(music_player PRIVATE
    ${CURSES_LIBRARIES}
    Threads::Threads
)

# On macOS, prefer Homebrew ncursesw for wide-char support
if(APPLE)
    execute_process(
        COMMAND brew --prefix ncurses
        OUTPUT_VARIABLE NCURSES_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NCURSES_PREFIX)
        target_include_directories(music_player PRIVATE ${NCURSES_PREFIX}/include)
        target_link_directories(music_player PRIVATE ${NCURSES_PREFIX}/lib)
        target_link_libraries(music_player PRIVATE ncursesw)
    endif()
endif()

# Compiler warnings
target_compile_options(music_player PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
