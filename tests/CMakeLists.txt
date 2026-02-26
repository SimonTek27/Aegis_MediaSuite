# tests/CMakeLists.txt
# Aegis MediaSuite — Test suite
# Requires: Qt6 (Test, Quick, Qml), CMake >= 3.19
cmake_minimum_required(VERSION 3.19)
project(AegisTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test Quick Qml)

# ── Shared include / link settings ────────────────────────────────────────────
# Adjust AEGIS_SRC_DIR to point at the actual source tree when integrating
set(AEGIS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

function(aegis_add_test TARGET SOURCE)
    add_executable(${TARGET} ${SOURCE})
    target_include_directories(${TARGET} PRIVATE ${AEGIS_SRC_DIR})
    target_compile_definitions(${TARGET} PRIVATE AEGIS_VERSION="2.1.1")
    target_link_libraries(${TARGET}
        PRIVATE
            Qt6::Core
            Qt6::Test
            Qt6::Quick
            Qt6::Qml
            # Link against the Aegis static/shared library when available:
            # AegisCore
    )
    add_test(NAME ${TARGET} COMMAND ${TARGET} -v2)
endfunction()

enable_testing()

aegis_add_test(TestAudioEngine  test_audio_engine.cpp)
aegis_add_test(TestVideoEditor  test_video_editor.cpp)
aegis_add_test(TestDawNotation  test_daw_notation.cpp)
aegis_add_test(TestUiQml        test_ui_qml.cpp)

# ── CTest labels for selective runs ──────────────────────────────────────────
set_tests_properties(TestAudioEngine PROPERTIES LABELS "audio;unit")
set_tests_properties(TestVideoEditor PROPERTIES LABELS "video;unit")
set_tests_properties(TestDawNotation PROPERTIES LABELS "daw;notation;unit")
set_tests_properties(TestUiQml       PROPERTIES LABELS "ui;qml;unit")

# Run: ctest -L audio        → only audio tests
# Run: ctest -L unit         → all unit tests
# Run: ctest --output-on-failure
