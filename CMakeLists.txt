cmake_minimum_required(VERSION 4.1)
project(turbo_twig LANGUAGES C CXX)

# Set the default C++ standard to C++23
set(PROJECT_NAME turbo_twig)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23) # Requires C11 standard
set(CMAKE_CXX_STANDARD_REQUIRED ON)  # optional, ensure standard is supported
set(CMAKE_CXX_EXTENSIONS OFF)  # optional, keep compiler extensions off
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_VERBOSE_MAKEFILE ON)

set(BUILD_EXECUTABLE ON)
set(BUILD_EXEMAIN "exemain")
set(BUILD_SHARED_LIBS OFF)
set(BUILD_SHARED OFF)

option(BUILD_TESTS "Build and run tests" ON)
option(BUILD_APP "Build the application" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Catch2
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "disable building tests" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "disable building examples" FORCE)
set(CATCH_BUILD_FUZZERS OFF CACHE BOOL "disable building fuzzers" FORCE)

message("CMAKE_SOURCE_DIR location: ${CMAKE_SOURCE_DIR}")
include(FetchContent)
include_directories(/usr/local/include)
include_directories(${HOME_INCLUDE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/dependencies)
include_directories(${CMAKE_SOURCE_DIR}/dependencies/Catch2-3.9.0/extras)
include_directories(include)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/dependencies/highway)

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(BUILD_EXAMPLES OFF)
set(BUILD_SHARED_LIBS OFF)

# highway
set(HWY_TEST_STANDALONE OFF)
set(HWY_ENABLE_EXAMPLES OFF)
set(HWY_ENABLE_INSTALL OFF)
set(HWY_ENABLE_TESTS OFF)
set(HWY_BUILD_TESTS OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/highway)

link_directories(/usr/local/lib)
link_directories(${HOME_LIB_DIR})

file(GLOB_RECURSE APP_SOURCE
        "${CMAKE_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_SOURCE_DIR}/include/**/*.hpp"
        "${CMAKE_SOURCE_DIR}/src/**/*.c"
        "${CMAKE_SOURCE_DIR}/src/**/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/**/*.cc"
        "${CMAKE_SOURCE_DIR}/src/**/*.cxx"
)

# Separate real compilable source files (C/C++) from headers so we can
# avoid creating an executable target with only headers (which causes
# "No SOURCES given to target" errors).
set(APP_SOURCES_CPP)
foreach(_f IN LISTS APP_SOURCE)
    if(_f MATCHES "\\.(c|cc|cpp|cxx)$")
        list(APPEND APP_SOURCES_CPP "${_f}")
    endif()
endforeach()

SET(CATCH2_SOURCE
        "${CMAKE_SOURCE_DIR}/dependencies/Catch2-3.9.0/extras/catch_amalgamated.cpp"
        "${CMAKE_SOURCE_DIR}/dependencies/Catch2-3.9.0/extras/catch_amalgamated.hpp"
)
add_library(Catch2 STATIC ${CATCH2_SOURCE})

if (BUILD_EXECUTABLE)
    if (BUILD_EXEMAIN STREQUAL "exemain")
        if (APP_SOURCES_CPP)
            add_executable(${PROJECT_NAME}
                    ${APP_SOURCES_CPP}
            )
            target_link_libraries(${PROJECT_NAME}
                    PRIVATE
                    Catch2
            )
        else()
            message(WARNING "No C/C++ source files found for target '${PROJECT_NAME}'. Skipping executable target creation.")
        endif()
    endif ()
endif ()

if (BUILD_TESTS)
    FILE(
            GLOB TEST_SOURCE
            "${CMAKE_SOURCE_DIR}/src/tests/*.cpp"
    )
    add_executable(turbo_twig_tests ${TEST_SOURCE})
    target_link_libraries(turbo_twig_tests PRIVATE
            Catch2
            hwy
    )
endif ()