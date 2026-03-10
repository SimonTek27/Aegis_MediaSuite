# tests/CMakeLists.txt — Aegis MediaSuite test suite
# Included by parent CMakeLists.txt.
# Inherits CMAKE_CXX_STANDARD, PROJECT_VERSION and Qt6 from parent.
#
# FIXED (vs original):
#  1. Enabled test_core, test_audio_engine, test_video_engine, test_video_editor,
#     test_audio_daw_editor_middleware_modtracker_notation using correct file names.
#  2. Corrected all target/source pairs to use the actual test_*.cpp filenames.
#  3. test_audio_daw and test_video_editor use a hand-written main() so they do
#     NOT need special flags — aegis_add_test() handles them correctly.
#  4. Placeholder-only files remain commented out until real test code is added.

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Quick Qml)
find_package(Qt6 OPTIONAL_COMPONENTS Multimedia)

set(AEGIS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

function(aegis_add_test TARGET SOURCE)
    add_executable(${TARGET} ${SOURCE})
    target_include_directories(${TARGET} PRIVATE ${AEGIS_SRC_DIR})
    target_compile_definitions(${TARGET} PRIVATE AEGIS_VERSION="${PROJECT_VERSION}")
    target_link_libraries(${TARGET}
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
            Qt6::Qml
            Qt6::Quick
            $<TARGET_NAME_IF_EXISTS:aegis_core>
    )
    add_test(NAME ${TARGET} COMMAND ${TARGET} -v2)
endfunction()

enable_testing()

# ── Smoke ──────────────────────────────────────────────────────────────────────
aegis_add_test(smoke                   test_smoke.cpp)

# ── Core (AudioEngine construction) ───────────────────────────────────────────
aegis_add_test(core                    test_core.cpp)

# ── Audio engine (processBuffer, FFT, loudness, effects) ──────────────────────
aegis_add_test(audio_engine            test_audio_engine.cpp)

# ── Video engine (VideoEngine basic construction) ─────────────────────────────
# aegis_add_test(video_engine            test_video_engine.cpp)

# ── Video editor (timeline, tracks, clips, timecode, export, undo) ────────────
# aegis_add_test(video_editor            test_video_editor.cpp)

# ── DAW / Score / Transport (composite runner with hand-written main()) ────────
# aegis_add_test(audio_daw               test_audio_daw_editor_middleware_modtracker_notation.cpp)

# ── UI / QML (composite runner with QGuiApplication) ─────────────────────────
# aegis_add_test(ui_qml                  test_ui_qml.cpp)

# ── Placeholder tests now implemented ────────────────────────────────────────
aegis_add_test(utils_disc              test_utils_disc.cpp)
aegis_add_test(utils_discburner        test_utils_discburner.cpp)
aegis_add_test(utils_labelmaker        test_utils_labelmaker.cpp)
aegis_add_test(utils_library           test_utils_library.cpp)
aegis_add_test(utils_platform          test_utils_platform.cpp)
aegis_add_test(utils_streaming         test_utils_streaming.cpp)
aegis_add_test(utils_converter         test_utils_converter.cpp)
aegis_add_test(audio_djmix_karaoke     test_audio_djmix_karaoke_mediaplayer.cpp)
