cmake_minimum_required(VERSION 3.16)
project(zipwrapper++ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
find_package(ZLIB REQUIRED)

# --- Library ---
add_library(zipwrapper
    src/ZipWrapper.cpp
    src/ZipHeader.cpp
)

target_include_directories(zipwrapper PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(zipwrapper PUBLIC
    PkgConfig::LIBZIP
    ZLIB::ZLIB
)

# --- Tests ---
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(zipwrapper_tests
        tests/zipfile.cpp
        tests/zipstreams.cpp
        tests/collectioncollection.cpp
        tests/testfiles.cpp
    )

    target_compile_definitions(zipwrapper_tests PRIVATE
        TESTFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testfiles"
        FREECAD_TESTFILES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/freecadtestfiles"
    )

    target_link_libraries(zipwrapper_tests PRIVATE
        zipwrapper
        GTest::gtest_main
        ZLIB::ZLIB
    )

    include(GoogleTest)
    gtest_discover_tests(zipwrapper_tests)

    # --- Valgrind memory-leak check ---
    find_program(VALGRIND_EXECUTABLE valgrind)
    if(VALGRIND_EXECUTABLE)
        add_custom_target(valgrind
            COMMAND ${CMAKE_COMMAND} -E echo
                    "${VALGRIND_EXECUTABLE}"
                    --leak-check=full
                    --show-leak-kinds=all
                    --track-origins=yes
                    --malloc-fill=0x42
                    --free-fill=0x61
                    --errors-for-leak-kinds=definite,possible
                    --error-exitcode=1
                    "$<TARGET_FILE:zipwrapper_tests>"
            COMMAND ${VALGRIND_EXECUTABLE}
                    --leak-check=full
                    --show-leak-kinds=all
                    --track-origins=yes
                    --malloc-fill=0x42
                    --free-fill=0x61
                    --errors-for-leak-kinds=definite,possible
                    --error-exitcode=1
                    $<TARGET_FILE:zipwrapper_tests>
            DEPENDS zipwrapper_tests
            COMMENT "Running all tests under Valgrind"
            VERBATIM
        )
    endif()
endif()

# --- Documentation ---
option(BUILD_DOCS "Build documentation (requires Doxygen and Python3)" OFF)
if(BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(docs
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/docs/m.css/documentation/doxygen.py
                ${CMAKE_CURRENT_SOURCE_DIR}/docs/conf.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs
        COMMENT "Generating documentation with m.css + Doxygen"
        VERBATIM
    )
endif()
