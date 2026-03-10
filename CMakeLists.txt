cmake_minimum_required(VERSION 3.19)
project(AegisMediaSuite
    VERSION 2.1.1
    LANGUAGES CXX
)

# Version string for C++ code
add_compile_definitions(AEGIS_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Quick Qml Widgets Multimedia Sql OpenGLWidgets PrintSupport DBus LinguistTools Svg)
find_package(KF6Solid REQUIRED)

# Collect all .cpp/.h sources from src to ensure implementations are built into aegis_core
file(GLOB_RECURSE AEGIS_SRC_CXX CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE AEGIS_SRC_H        CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.h")

add_library(aegis_core STATIC ${AEGIS_SRC_CXX} ${AEGIS_SRC_H})

target_include_directories(aegis_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

# ─── GCC 15 / Qt 6 two-phase name-lookup fix ────────────────────────────────
# GCC 15 enforces strict two-phase name lookup inside template bodies.
# Several Qt 6 system headers (qqmlprivate.h, qvectornd.h, qtvideo.h) rely on
# non-dependent names (qmlregister, operator-, tuple_size) being visible at the
# point of template definition rather than instantiation.  When these headers are
# parsed while a user namespace (Aegis) is active the names resolve incorrectly.
#
# The ONLY reliable fix is a Precompiled Header (PCH): CMake pre-compiles
# qt_pch.h in the *global* namespace (no user namespace open yet), writes the
# binary .gch, and force-includes it into *every* TU—including the AUTOMOC
# mocs_compilation.cpp—before any project source is seen.  This guarantees Qt
# templates are always parsed in the correct namespace context.
target_precompile_headers(aegis_core PRIVATE src/qt_pch.h)


# Link Qt components used by the core library
target_link_libraries(aegis_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::OpenGLWidgets
        Qt6::Sql
        Qt6::PrintSupport
        Qt6::DBus
        Qt6::Qml
        Qt6::Quick
        Qt6::Svg
        KF6::Solid
)

# Explicit FFmpeg linkage
target_link_libraries(aegis_core
    PUBLIC
        avformat
        avcodec
        avutil
        swscale
        swresample
)

# Try to find optional third-party libraries; link if available
find_library(FFTW3F_LIB NAMES fftw3f fftwf)
find_library(EBUR128_LIB ebur128)
find_library(RUBBERBAND_LIB rubberband)
find_library(OPENMPT_LIB openmpt)
find_library(MPV_LIB mpv)
find_library(PIPEWIRE_LIB pipewire)
find_library(TAGLIB_LIB tag)
find_library(SNDFILE_LIB sndfile)
find_library(SAMPLERATE_LIB samplerate)

# Add found libraries to aegis_core (only those found)
if(FFTW3F_LIB)
    target_link_libraries(aegis_core PRIVATE ${FFTW3F_LIB})
endif()
if(EBUR128_LIB)
    target_link_libraries(aegis_core PRIVATE ${EBUR128_LIB})
endif()
if(RUBBERBAND_LIB)
    target_link_libraries(aegis_core PRIVATE ${RUBBERBAND_LIB})
endif()
if(OPENMPT_LIB)
    target_link_libraries(aegis_core PRIVATE ${OPENMPT_LIB})
endif()
if(MPV_LIB)
    target_link_libraries(aegis_core PRIVATE ${MPV_LIB})
endif()
if(PIPEWIRE_LIB)
    target_link_libraries(aegis_core PRIVATE ${PIPEWIRE_LIB})
endif()

# ─── Internationalization (i18n) ────────────────────────────────────────────
# qt6_add_translations must be called AFTER the executable target exists.
# We compile .ts → .qm with qt6_create_translation (Qt6 Linguist macro) and
# embed the resulting .qm files directly via qt_add_resources on the target.
set(AEGIS_TRANSLATION_SOURCES
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_it.ts
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_de.ts
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_fr.ts
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_pt_BR.ts
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_ru.ts
    ${CMAKE_SOURCE_DIR}/translations/aegis-mediasuite_es.ts
)

qt6_add_resources(AEGIS_RESOURCES resources.qrc)

# ─── Single executable ───────────────────────────────────────────────────────
# One binary handles every mode.  Mode is resolved at runtime from:
#   1. --mode=<n> command-line flag  (always wins)
#   2. argv[0] basename with the "aegis-" prefix stripped
#      e.g.  aegis-mediaplayer  →  mediaplayer
# Per-mode stub executables (see below) expose each mode as its own binary.
add_executable(aegis src/main.cpp ${AEGIS_RESOURCES})
target_link_libraries(aegis PRIVATE aegis_core Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::Gui)
target_precompile_headers(aegis REUSE_FROM aegis_core)

# ─── Translations: compile .ts → .qm and embed into the executable ──────────
# qt6_add_translations must be called after the target is defined.
qt6_add_translations(aegis
    TS_FILES ${AEGIS_TRANSLATION_SOURCES}
    LUPDATE_OPTIONS -no-obsolete
)

# ─── Install: main binary ────────────────────────────────────────────────────
install(TARGETS aegis RUNTIME DESTINATION bin)

# Per-app symlinks
# The aegis binary detects mode from argv[0]: aegis-mediaplayer -> mediaplayer mode.
set(AEGIS_APP_MODES
    launcher mediaplayer audioeditor videoeditor daw modtracker middleware
    djmixer karaoke disctools labelmaker converter streaming capture library
)

foreach(mode IN LISTS AEGIS_APP_MODES)
    set(_sym "aegis-${mode}")
    add_custom_command(TARGET aegis POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "$<TARGET_FILE_NAME:aegis>"
            "$<TARGET_FILE_DIR:aegis>/${_sym}"
        COMMENT "Symlink: ${_sym} -> aegis"
        VERBATIM
    )
endforeach()

# Install-tree symlinks (cmake --install)
foreach(mode IN LISTS AEGIS_APP_MODES)
    install(CODE
        "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink aegis \
            \${CMAKE_INSTALL_PREFIX}/bin/aegis-${mode})"
    )
endforeach()

if(TAGLIB_LIB)
    target_link_libraries(aegis_core PRIVATE ${TAGLIB_LIB})
endif()
if(SNDFILE_LIB)
    target_link_libraries(aegis_core PRIVATE ${SNDFILE_LIB})
endif()
if(SAMPLERATE_LIB)
    target_link_libraries(aegis_core PRIVATE ${SAMPLERATE_LIB})
endif()

enable_testing()
add_subdirectory(tests)

# Optional: GStreamer support for capture
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0)
    if(GSTREAMER_FOUND)
        target_compile_definitions(aegis_core PUBLIC HAVE_GSTREAMER)
        target_include_directories(aegis_core PRIVATE ${GSTREAMER_INCLUDE_DIRS})
        target_link_libraries(aegis_core PRIVATE ${GSTREAMER_LIBRARIES})
        message(STATUS "GStreamer found: ${GSTREAMER_VERSION}")
    else()
        message(STATUS "GStreamer NOT found — capture module compiled without GStreamer")
    endif()

    # Required: libcdio and libcdio_paranoia for CD audio ripping
    pkg_check_modules(LIBCDIO QUIET libcdio)
    pkg_check_modules(LIBCDIO_PARANOIA QUIET libcdio_paranoia)
    if(LIBCDIO_FOUND AND LIBCDIO_PARANOIA_FOUND)
        target_compile_definitions(aegis_core PUBLIC HAVE_LIBCDIO)
        target_include_directories(aegis_core PRIVATE ${LIBCDIO_INCLUDE_DIRS} ${LIBCDIO_PARANOIA_INCLUDE_DIRS})
        target_link_libraries(aegis_core PRIVATE ${LIBCDIO_LIBRARIES} ${LIBCDIO_PARANOIA_LIBRARIES})
        message(STATUS "libcdio found: ${LIBCDIO_VERSION}, libcdio_paranoia: ${LIBCDIO_PARANOIA_VERSION}")
    else()
        message(STATUS "libcdio/libcdio_paranoia NOT found — linking directly as fallback")
        target_link_libraries(aegis_core PRIVATE cdio cdio_paranoia cdio_cdda)
    endif()

    # Optional: libburn/libisofs for disc burning
    pkg_check_modules(LIBBURN QUIET libburn-1)
    pkg_check_modules(LIBISOFS QUIET libisofs-1)
    if(LIBBURN_FOUND AND LIBISOFS_FOUND)
        target_compile_definitions(aegis_core PUBLIC HAVE_LIBBURN)
        target_include_directories(aegis_core PRIVATE ${LIBBURN_INCLUDE_DIRS} ${LIBISOFS_INCLUDE_DIRS})
        target_link_libraries(aegis_core PRIVATE ${LIBBURN_LIBRARIES} ${LIBISOFS_LIBRARIES})
        message(STATUS "libburn found: ${LIBBURN_VERSION}, libisofs: ${LIBISOFS_VERSION}")
    else()
        message(STATUS "libburn/libisofs NOT found — disc burning compiled in stub mode")
    endif()
endif()

# ─── Install: desktop integration ───────────────────────────────────────────
configure_file(org.aegis.Aegis.desktop.in org.aegis.Aegis.desktop @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.aegis.Aegis.desktop"
    DESTINATION share/applications)

# App icon (scalable + hicolor sizes)
install(FILES assets/icons/sc-apps-aegis.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME aegis.svg)

# MPRIS D-Bus service
configure_file(org.mpris.MediaSuite.aegis.service.in
    org.mpris.MediaSuite.aegis.service @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.mpris.MediaSuite.aegis.service"
    DESTINATION share/dbus-1/services)

# Translations (.qm files built by qt6_add_translations)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/"
    DESTINATION share/aegis/translations
    FILES_MATCHING PATTERN "*.qm")
