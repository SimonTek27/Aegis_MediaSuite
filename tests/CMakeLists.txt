# CMake for unit tests (regenerated for single-binary Aegis)

enable_testing()

find_package(Qt6 REQUIRED COMPONENTS Core Test)

# All product code is in aegis_core
# (defined in top-level CMakeLists.txt)

function(aegis_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE
        aegis_core
        Qt6::Core
        Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Focused smoke tests that only touch stable public headers

aegis_add_test(test_core      test_core.cpp)
aegis_add_test(test_audio     test_audio.cpp)
aegis_add_test(test_video     test_video.cpp)
aegis_add_test(test_streaming test_streaming.cpp)
aegis_add_test(test_converter test_converter.cpp)
