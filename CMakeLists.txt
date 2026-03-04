cmake_minimum_required(VERSION 3.19)
project(AegisMediaSuite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Quick Qml Widgets Multimedia Sql OpenGLWidgets PrintSupport DBus)
find_package(KF6Solid REQUIRED)

# Collect all .cpp/.h sources from src to ensure implementations are built into aegis_core
file(GLOB_RECURSE AEGIS_SRC_CXX CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE AEGIS_SRC_H        CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.h")

add_library(aegis_core STATIC ${AEGIS_SRC_CXX} ${AEGIS_SRC_H})

target_include_directories(aegis_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

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
        KF6::Solid
)

# Try to find optional third-party libraries; link if available
find_library(FFTW3F_LIB fftwf)
find_library(EBUR128_LIB ebur128)
find_library(RUBBERBAND_LIB rubberband)
find_library(OPENMPT_LIB openmpt)
find_library(MPV_LIB mpv)
find_library(PIPEWIRE_LIB pipewire)

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

add_executable(aegis src/main.cpp)

target_link_libraries(aegis PRIVATE aegis_core Qt6::Qml Qt6::Quick Qt6::Widgets Qt6::Gui)

enable_testing()
add_subdirectory(tests)
