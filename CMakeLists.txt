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

# Spdlog
set(SPDLOG_BUILD_EXAMPLES OFF)
set(SPDLOG_USE_STD_FORMAT ON)
set(SPDLOG_BUILD_SHARED OFF)
set(SPDLOG_ENABLE_PCH OFF)
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_BENCH OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/highway)
add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/spdlog)

# ===================== MLX (Apple GPU/CPU ML framework) =======================
option(BUILD_MLX "Build with MLX support" ON)

if(BUILD_MLX AND APPLE)
    # MLX build configuration — static library, minimal footprint
    set(MLX_BUILD_TESTS OFF CACHE BOOL "Disable MLX tests" FORCE)
    set(MLX_BUILD_EXAMPLES OFF CACHE BOOL "Disable MLX examples" FORCE)
    set(MLX_BUILD_BENCHMARKS OFF CACHE BOOL "Disable MLX benchmarks" FORCE)
    set(MLX_BUILD_SAFETENSORS ON CACHE BOOL "Enable safetensors support" FORCE)
    set(MLX_BUILD_GGUF ON CACHE BOOL "Enable GGUF support" FORCE)
    set(MLX_BUILD_METAL ON CACHE BOOL "Enable Metal GPU backend" FORCE)
    set(MLX_METAL_JIT ON CACHE BOOL "Enable Metal JIT compilation" FORCE)
    set(MLX_BUILD_CPU ON CACHE BOOL "Enable CPU backend" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build MLX as static library" FORCE)

    # Use MinSizeRel for MLX to keep binary size small
    set(MLX_ORIGINAL_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE MinSizeRel)
    endif()

    add_subdirectory(${CMAKE_SOURCE_DIR}/dependencies/mlx)

    # Restore original build type for the rest of the project
    if(DEFINED MLX_ORIGINAL_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE ${MLX_ORIGINAL_BUILD_TYPE})
    endif()

    # Make MLX headers available
    include_directories(${CMAKE_SOURCE_DIR}/dependencies/mlx)

    set(HAS_MLX TRUE)
    message(STATUS "MLX: Building as static library from dependencies/mlx")
else()
    set(HAS_MLX FALSE)
    if(NOT APPLE)
        message(STATUS "MLX: Skipped (Apple platform required)")
    else()
        message(STATUS "MLX: Disabled via BUILD_MLX=OFF")
    endif()
endif()


link_directories(/usr/local/lib)
link_directories(${HOME_LIB_DIR})

# Separate real compilable source files (C/C++) from headers so we can
# avoid creating an executable target with only headers (which causes
# "No SOURCES given to target" errors).
#set(APP_SOURCES_CPP)
#foreach(_f IN LISTS APP_SOURCE)
#    if(_f MATCHES "\\.(c|cc|cpp|cxx)$")
#        list(APPEND APP_SOURCES_CPP "${_f}")
#    endif()
#endforeach()

SET(CATCH2_SOURCE
        "${CMAKE_SOURCE_DIR}/dependencies/Catch2-3.9.0/extras/catch_amalgamated.cpp"
        "${CMAKE_SOURCE_DIR}/dependencies/Catch2-3.9.0/extras/catch_amalgamated.hpp"
)
add_library(Catch2 STATIC ${CATCH2_SOURCE})

if (BUILD_EXECUTABLE)
    file(GLOB APP_SOURCE
            "${CMAKE_SOURCE_DIR}/src/examples/*.cpp"
            "${CMAKE_SOURCE_DIR}/src/examples/*.hpp"
    )

    add_executable(${PROJECT_NAME} ${APP_SOURCE})
    target_link_libraries(${PROJECT_NAME} PRIVATE
            hwy
            spdlog::spdlog_header_only
    )
endif ()

if (BUILD_TESTS)
    file(
            GLOB TEST_SOURCE
            "${CMAKE_SOURCE_DIR}/src/tests/*.cpp"
    )
    add_executable(turbo_twig_tests ${TEST_SOURCE})
    target_link_libraries(turbo_twig_tests PRIVATE
            Catch2
            hwy
            spdlog::spdlog_header_only
    )
    if(HAS_MLX)
        target_link_libraries(turbo_twig_tests PRIVATE mlx)
        target_compile_definitions(turbo_twig_tests PRIVATE HAS_MLX=1)
    endif()
endif ()