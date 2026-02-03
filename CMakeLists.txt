# Top-level CMakeLists.txt for Aegis MediaSuite (simplified, single binary)

cmake_minimum_required(VERSION 3.20)
project(AegisMediaSuite VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# Build options
# =============================================================================
option(AEGIS_BUILD_TESTS "Build unit tests" ON)
option(AEGIS_SANITIZERS "Enable Address/UB sanitizers" OFF)
option(AEGIS_LTO "Enable Link Time Optimization" ON)

option(AEGIS_ENABLE_KF6  "Enable KDE Frameworks 6 integration" ON)
option(AEGIS_ENABLE_DISC "Enable CD/DVD/Blu-ray support" ON)
option(AEGIS_ENABLE_DJ   "Enable DJ mixing mode" ON)
option(AEGIS_ENABLE_EDITOR "Enable audio/video editor modes" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# =============================================================================
# Compiler flags
# =============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wshadow -Wnon-virtual-dtor
        -Wold-style-cast -Wcast-align
        -Woverloaded-virtual -Wdouble-promotion
        -Wformat=2 -Wmissing-include-dirs
        -Wno-unused-parameter
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g3 -fno-omit-frame-pointer)
        if(AEGIS_SANITIZERS)
            add_compile_options(-fsanitize=address,undefined)
            add_link_options(-fsanitize=address,undefined)
        endif()
    else()
        add_compile_options(-O3 -DNDEBUG)
        if(AEGIS_LTO)
            add_compile_options(-flto=auto)
            add_link_options(-flto=auto)
        endif()
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od /Zi)
    else()
        add_compile_options(/O2 /Ob2)
    endif()
endif()

# =============================================================================
# Qt and dependencies
# =============================================================================
find_package(Qt6 6.6 REQUIRED COMPONENTS
    Core Gui Widgets Qml Quick QuickControls2
    Network Sql Concurrent DBus PrintSupport Multimedia
)

qt_standard_project_setup()

include_directories(${CMAKE_SOURCE_DIR}/src)

# KF6 (optional)
if(AEGIS_ENABLE_KF6)
    find_package(ECM 6.0 NO_MODULE QUIET)
    if(ECM_FOUND)
        list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
        find_package(KF6 COMPONENTS
            ConfigWidgets
            Notifications
            StatusNotifierItem
            Solid
        )
        if(KF6_FOUND)
            add_definitions(-DKF6_VERSION)
        endif()
    endif()
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(FFTW3    REQUIRED fftw3f)
pkg_check_modules(EBUR128  REQUIRED libebur128)
pkg_check_modules(SNDFILE  REQUIRED sndfile)
pkg_check_modules(SAMPLERATE REQUIRED samplerate)
pkg_check_modules(MPV      REQUIRED mpv)

if(AEGIS_ENABLE_DJ)
    pkg_check_modules(RUBBERBAND rubberband)
    if(RUBBERBAND_FOUND)
        add_definitions(-DHAVE_RUBBERBAND)
    endif()
endif()

if(AEGIS_ENABLE_DISC)
    pkg_check_modules(CDIO libcdio)
    pkg_check_modules(CDIO_PARANOIA libcdio_paranoia)
    if(CDIO_FOUND AND CDIO_PARANOIA_FOUND)
        add_definitions(-DHAVE_CDIO)
    endif()
endif()

find_package(TagLib 2.0 QUIET)
if(NOT TagLib_FOUND)
    pkg_check_modules(TAGLIB taglib)
endif()

# =============================================================================
# Sources
# =============================================================================
set(AEGIS_CORE_SOURCES
    src/core.cpp           src/core.h
    src/audio.cpp          src/audio.h
    src/audio_daw.cpp      src/audio_daw.h
    src/audio_effects.h
    src/audio_middleware.cpp src/audio_middleware.h
    src/audio_output.h
    src/video.cpp          src/video.h
    src/video_output.cpp   src/video_output.h
    src/video_effects.cpp  src/video_effects.h
    src/mpv_backend.cpp    src/mpv_backend.h
    src/platform.cpp       src/platform.h
    src/library.cpp        src/library.h
    src/streaming.cpp      src/streaming.h
    src/capture.cpp        src/capture.h
    src/converter.cpp      src/converter.h
    src/kdeconnect.cpp     src/kdeconnect.h
    src/mediaplayer.cpp    src/mediaplayer.h
    src/ffmpeg_utils.cpp   src/ffmpeg_utils.h
    src/raii_wrappers.h
    src/plugin_interface.h
)

set(AEGIS_EDITOR_SOURCES)
if(AEGIS_ENABLE_EDITOR)
    list(APPEND AEGIS_EDITOR_SOURCES
        src/audioeditor.cpp   src/audioeditor.h
        src/videoeditor.cpp   src/videoeditor.h
    )
    add_definitions(-DAEGIS_ENABLE_EDITOR)
endif()

set(AEGIS_DISC_SOURCES)
if(AEGIS_ENABLE_DISC)
    list(APPEND AEGIS_DISC_SOURCES
        src/disc.cpp           src/disc.h
        src/discburner.cpp     src/discburner.h
        src/disc_labelmaker.cpp src/disc_labelmaker.h
    )
    add_definitions(-DAEGIS_ENABLE_DISC)
endif()

set(AEGIS_DJ_SOURCES)
if(AEGIS_ENABLE_DJ AND RUBBERBAND_FOUND)
    list(APPEND AEGIS_DJ_SOURCES
        src/djmix.cpp      src/djmix.h
        src/modtracker.cpp src/modtracker.h
    )
    add_definitions(-DAEGIS_ENABLE_DJ)
endif()

set(AEGIS_KARAOKE_SOURCES
    src/karaoke.cpp src/karaoke.h
)

set(AEGIS_ALL_SOURCES
    ${AEGIS_CORE_SOURCES}
    ${AEGIS_EDITOR_SOURCES}
    ${AEGIS_DISC_SOURCES}
    ${AEGIS_DJ_SOURCES}
    ${AEGIS_KARAOKE_SOURCES}
)

# =============================================================================
# Core library (for tests, possible plugins)
# =============================================================================
add_library(aegis_core STATIC ${AEGIS_ALL_SOURCES})

target_include_directories(aegis_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Third‑party libs
if(FFTW3_FOUND)
    target_include_directories(aegis_core PRIVATE ${FFTW3_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${FFTW3_LIBRARIES})
endif()
if(EBUR128_FOUND)
    target_include_directories(aegis_core PRIVATE ${EBUR128_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${EBUR128_LIBRARIES})
endif()
if(SNDFILE_FOUND)
    target_include_directories(aegis_core PRIVATE ${SNDFILE_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${SNDFILE_LIBRARIES})
endif()
if(SAMPLERATE_FOUND)
    target_include_directories(aegis_core PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${SAMPLERATE_LIBRARIES})
endif()
if(MPV_FOUND)
    target_include_directories(aegis_core PRIVATE ${MPV_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${MPV_LIBRARIES})
endif()
if(RUBBERBAND_FOUND)
    target_include_directories(aegis_core PRIVATE ${RUBBERBAND_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${RUBBERBAND_LIBRARIES})
endif()
if(CDIO_FOUND)
    target_include_directories(aegis_core PRIVATE ${CDIO_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${CDIO_LIBRARIES})
endif()
if(TagLib_FOUND)
    target_include_directories(aegis_core PRIVATE ${TagLib_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${TagLib_LIBRARIES})
elseif(TAGLIB_LIBRARIES)
    target_include_directories(aegis_core PRIVATE ${TAGLIB_INCLUDE_DIRS})
    target_link_libraries(aegis_core PRIVATE ${TAGLIB_LIBRARIES})
endif()

# Qt modules used from headers
target_link_libraries(aegis_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets
    Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Sql Qt6::Concurrent
    Qt6::DBus Qt6::PrintSupport Qt6::Multimedia
)

# =============================================================================
# Main GUI executable (single binary, all modes via ui_launcher + rootWindow)
# =============================================================================
add_executable(aegis
    src/main.cpp
)

target_link_libraries(aegis PRIVATE aegis_core)

set_target_properties(aegis PROPERTIES
    OUTPUT_NAME "aegis"
)

# =============================================================================
# Tests
# =============================================================================
if(AEGIS_BUILD_TESTS)
    add_subdirectory(tests ${CMAKE_BINARY_DIR}/tests_build)
endif()

# =============================================================================
# Install
# =============================================================================
include(GNUInstallDirs)

install(TARGETS aegis RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES org.aegis.Aegis.desktop.in
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        RENAME org.aegis.Aegis.desktop)

install(FILES assets/icons/app_icon.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME org.aegis.Aegis.svg)
