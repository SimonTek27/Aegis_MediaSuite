cmake_minimum_required(VERSION 3.19)
project(AegisMediaSuite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Quick Qml Widgets Multimedia Sql OpenGLWidgets PrintSupport DBus)

add_library(aegis_core STATIC
    src/audio.cpp
    src/audio.h
    src/audio_daw.cpp
    src/audio_daw.h
    src/audio_effects.cpp
    src/audio_effects.h
    src/audio_middleware.cpp
    src/audio_middleware.h
    src/audio_output.cpp
    src/audio_output.h
    src/core.cpp
    src/core.h
    src/audio_notation_editor.cpp
    src/audio_notation_editor.h
)

add_executable(aegis
    src/main.cpp
)

target_link_libraries(aegis PRIVATE aegis_core Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::Gui)

target_include_directories(aegis_core PUBLIC src)

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
)

target_include_directories(aegis_core PUBLIC src)

enable_testing()
add_subdirectory(tests)
