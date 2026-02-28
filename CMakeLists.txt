# tests/CMakeLists.txt — Aegis MediaSuite test suite
# Requires: Qt6 (Core, Gui, Test, Quick, Qml), CMake >= 3.19
cmake_minimum_required(VERSION 3.19)
project(AegisTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Quick Qml)
find_package(Qt6 OPTIONAL_COMPONENTS Multimedia)

set(AEGIS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# ── Base helper ───────────────────────────────────────────────────────────────
# All test targets get at minimum: Core + Gui + Test
# Qt6::Gui is required by any target whose headers (directly or transitively)
# include <QColor>, <QFont>, <QPainter>, <QOpenGLContext>, etc.
# audio_daw.h includes <QColor> and <QPainter>, so ALL targets that include
# audio_daw.h (directly or via videoeditor.h / audio.h) must link Qt6::Gui.
function(aegis_add_test TARGET SOURCE)
    add_executable(${TARGET} ${SOURCE})
    target_include_directories(${TARGET} PRIVATE ${AEGIS_SRC_DIR})
    target_compile_definitions(${TARGET} PRIVATE AEGIS_VERSION="2.1.1")
    target_link_libraries(${TARGET}
        PRIVATE
            Qt6::Core
            Qt6::Gui        # <── needed by audio_daw.h (QColor, QPainter, QFont)
            Qt6::Test
            # AegisCore   # uncomment once the shared library is built
    )
    add_test(NAME ${TARGET} COMMAND ${TARGET} -v2)
endfunction()

enable_testing()

# ── TestAudioEngine ───────────────────────────────────────────────────────────
aegis_add_test(TestAudioEngine test_audio_engine.cpp)

# ── TestVideoEditor ───────────────────────────────────────────────────────────
# videoeditor.h → audio_output.h → <QAudioSink>  (Qt::Multimedia)
aegis_add_test(TestVideoEditor test_video_editor.cpp)
if(Qt6Multimedia_FOUND)
    target_link_libraries(TestVideoEditor PRIVATE Qt6::Multimedia)
    target_compile_definitions(TestVideoEditor PRIVATE QT_MULTIMEDIA_LIB)
endif()

# ── TestDawModtrackerNotation ─────────────────────────────────────────────────
# audio_daw.h includes <QColor>, <QFont>, <QPainter>, <QRectF> → needs Qt6::Gui
# (already added via aegis_add_test; listed here for clarity)
aegis_add_test(TestDawModtrackerNotation test_daw_modtracker_notation.cpp)

# ── TestUiQml ─────────────────────────────────────────────────────────────────
# Uses QQmlEngine/QtQuick → needs Qt6::Gui + Qt6::Quick + Qt6::Qml
aegis_add_test(TestUiQml test_ui_qml.cpp)
# QApplication (needed to fully initialize Qt for QQmlEngine) requires Qt6::Widgets
find_package(Qt6 REQUIRED COMPONENTS Widgets)
target_link_libraries(TestUiQml PRIVATE Qt6::Widgets Qt6::Quick Qt6::Qml)

# ── CTest labels ──────────────────────────────────────────────────────────────
set_tests_properties(TestAudioEngine           PROPERTIES LABELS "audio;unit")
set_tests_properties(TestVideoEditor           PROPERTIES LABELS "video;unit")
set_tests_properties(TestDawModtrackerNotation PROPERTIES LABELS "daw;notation;unit")
set_tests_properties(TestUiQml                PROPERTIES LABELS "ui;qml;unit")

# Selective runs:
#   ctest -L audio          → only audio tests
#   ctest -L unit           → all unit tests
#   ctest --output-on-failure
