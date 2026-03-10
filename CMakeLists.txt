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
endif()
