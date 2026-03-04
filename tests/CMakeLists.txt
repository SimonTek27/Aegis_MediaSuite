# tests/CMakeLists.txt — Aegis MediaSuite test suite
cmake_minimum_required(VERSION 3.19)
project(AegisTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Quick Qml)
find_package(Qt6 OPTIONAL_COMPONENTS Multimedia)

set(AEGIS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

function(aegis_add_test TARGET SOURCE)
    add_executable(${TARGET} ${SOURCE})
    target_include_directories(${TARGET} PRIVATE ${AEGIS_SRC_DIR})
    target_compile_definitions(${TARGET} PRIVATE AEGIS_VERSION="2.1.1")
    target_link_libraries(${TARGET}
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
            Qt6::Qml
            Qt6::Quick
    )
    add_test(NAME ${TARGET} COMMAND ${TARGET} -v2)
endfunction()

enable_testing()

# Gruppo audio_
# audio_core ha già un QTEST_MAIN
# aegis_add_test(audio_core              audio_core.cpp)

# audio_engine e modtracker hanno test corposi
# aegis_add_test(audio_engine            audio_engine.cpp)
# aegis_add_test(audio_daw_editor_middleware_modtracker_notation audio_daw_editor_middleware_modtracker_notation.cpp)

# Placeholder (senza main) — NON abilitati finché non servono
# aegis_add_test(audio_djmix             audio_djmix.cpp)
# aegis_add_test(audio_karaoke           audio_karaoke.cpp)

# Gruppo video_
# aegis_add_test(video_core              video_core.cpp)
# aegis_add_test(video_editor            video_editor.cpp)

# Gruppo utils_ (disc burner, label maker, capture, library, platform, streaming, converter, ecc.)
# Questi file sono dichiaratamente placeholder senza main; li si abiliterà in futuro
# aegis_add_test(utils_disc              utils_disc.cpp)
# aegis_add_test(utils_discburner        utils_discburner.cpp)
# aegis_add_test(utils_labelmaker        utils_labelmaker.cpp)
# utils_capture.cpp potrai aggiungerlo quando esiste
# aegis_add_test(utils_library           utils_library.cpp)
# aegis_add_test(utils_platform          utils_platform.cpp)
# aegis_add_test(utils_streaming         utils_streaming.cpp)
# aegis_add_test(utils_converter         utils_converter.cpp)

# Core / UI / smoke
# aegis_add_test(core_core               core_core.cpp)
aegis_add_test(ui_qml                  test_ui_qml.cpp)

aegis_add_test(smoke                   test_smoke.cpp)
