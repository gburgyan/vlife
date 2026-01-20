cmake_minimum_required(VERSION 3.10)
project(ConwayGameOfLife)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Enable testing
enable_testing()

# AVX-512 optimization option
option(ENABLE_AVX512 "Enable AVX-512 optimizations (uses SIMDE for emulation on non-x86)" OFF)

if(ENABLE_AVX512)
    message(STATUS "AVX-512 optimizations enabled")

    # Check if we're on x86_64 with native AVX-512 support
    include(CheckCXXCompilerFlag)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        check_cxx_compiler_flag("-mavx512f -mavx512bw" COMPILER_SUPPORTS_AVX512)
        if(COMPILER_SUPPORTS_AVX512)
            message(STATUS "Native AVX-512 support detected")
            set(AVX512_NATIVE ON)
        endif()
    endif()

    # Fetch SIMDE for portable AVX-512 emulation (always available for correctness testing)
    include(FetchContent)
    FetchContent_Declare(
        simde
        GIT_REPOSITORY https://github.com/simd-everywhere/simde.git
        GIT_TAG v0.8.2
    )
    FetchContent_MakeAvailable(simde)
    message(STATUS "SIMDE fetched for AVX-512 emulation support")
endif()

# Try to find Qt via Homebrew
execute_process(
        COMMAND brew --prefix qt@5
        RESULT_VARIABLE BREW_QT_RESULT
        OUTPUT_VARIABLE BREW_QT_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(BREW_QT_RESULT EQUAL 0)
    list(APPEND CMAKE_PREFIX_PATH "${BREW_QT_PREFIX}")
    message(STATUS "Found Qt5 via Homebrew at: ${BREW_QT_PREFIX}")
else()
    message(STATUS "Qt5 not found via Homebrew, checking system paths...")
endif()

# Try common macOS Qt installation locations
list(APPEND CMAKE_PREFIX_PATH
        "/usr/local/opt/qt@5"
        "/opt/homebrew/opt/qt@5"
        "/Users/${USER}/Qt/5.15.2/clang_64"
        "/Users/${USER}/Qt/5.15.2/macos"
        "/Users/${USER}/Qt/5.15/clang_64"
        "/Users/${USER}/Qt/5.15/macos"
)

# Find Qt packages
find_package(Qt5 COMPONENTS Core Widgets REQUIRED)

# Find threading library
find_package(Threads REQUIRED)

# Find TBB for parallel processing
find_package(TBB REQUIRED)

# Find GTest
find_package(GTest QUIET)
if(NOT GTEST_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# Backend library
add_library(
        GameOfLifeLib
        src/SimpleGameOfLife.cpp
        src/SimpleGameOfLife.h
        src/vlife/VLife.cpp
        src/vlife/VLife.h
        src/vlife/Tile.cpp
        src/vlife/Tile.h
)

# Link threading library and TBB to GameOfLifeLib
target_link_libraries(GameOfLifeLib PUBLIC Threads::Threads TBB::tbb)

# AVX-512 configuration for GameOfLifeLib
if(ENABLE_AVX512)
    target_compile_definitions(GameOfLifeLib PUBLIC VLIFE_AVX512_ENABLED)
    target_include_directories(GameOfLifeLib PUBLIC ${simde_SOURCE_DIR})

    if(AVX512_NATIVE)
        # Native x86_64 with AVX-512: use native intrinsics
        target_compile_options(GameOfLifeLib PRIVATE -mavx512f -mavx512bw -mavx512vl)
        target_compile_definitions(GameOfLifeLib PRIVATE VLIFE_AVX512_NATIVE)
        message(STATUS "GameOfLifeLib: Using native AVX-512 intrinsics")
    else()
        # Non-x86 or no native AVX-512: use SIMDE emulation
        target_compile_definitions(GameOfLifeLib PRIVATE SIMDE_ENABLE_NATIVE_ALIASES)
        message(STATUS "GameOfLifeLib: Using SIMDE for AVX-512 emulation (correctness testing)")
    endif()
endif()

# Frontend application
add_executable(
        GameOfLifeApp
        src/main.cpp
        src/GameOfLifeView.cpp
        src/GameOfLifeView.h
)

# Link libraries
target_link_libraries(GameOfLifeApp PRIVATE
        GameOfLifeLib
        Qt5::Core
        Qt5::Widgets
)

# Include directories
target_include_directories(GameOfLifeApp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation
install(TARGETS GameOfLifeApp DESTINATION bin)

# Tests
add_executable(
        GameOfLifeTests
        tests/main.cpp
        tests/vlife/VLifeTest.cpp
        tests/vlife/TileEdgeTest.cpp
)

target_link_libraries(
        GameOfLifeTests
        PRIVATE
        GameOfLifeLib
        GTest::gtest
        GTest::gtest_main
)

add_test(NAME GameOfLifeTests COMMAND GameOfLifeTests)

# Add a custom target to run tests
add_custom_target(run_tests
    COMMAND ctest --verbose
    DEPENDS GameOfLifeTests
)

# Benchmark executable
add_executable(
        VLifeBenchmark
        tests/benchmark/VLifeBenchmark.cpp
)

target_link_libraries(
        VLifeBenchmark
        PRIVATE
        GameOfLifeLib
)

target_include_directories(VLifeBenchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add a custom target to run benchmarks
add_custom_target(run_benchmark
    COMMAND VLifeBenchmark
    DEPENDS VLifeBenchmark
)

add_custom_target(run_benchmark_quick
    COMMAND VLifeBenchmark --quick
    DEPENDS VLifeBenchmark
)

# Extended benchmark executable (comprehensive publication-quality benchmarks)
add_executable(
        VLifeExtendedBenchmark
        tests/benchmark/ExtendedBenchmark.cpp
)

target_link_libraries(
        VLifeExtendedBenchmark
        PRIVATE
        GameOfLifeLib
)

target_include_directories(VLifeExtendedBenchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add custom targets for extended benchmarks
add_custom_target(run_extended_benchmark
    COMMAND VLifeExtendedBenchmark
    DEPENDS VLifeExtendedBenchmark
)

add_custom_target(run_density_sweep
    COMMAND VLifeExtendedBenchmark --density
    DEPENDS VLifeExtendedBenchmark
)

add_custom_target(run_activity_test
    COMMAND VLifeExtendedBenchmark --activity
    DEPENDS VLifeExtendedBenchmark
)

# Add test-related messages
message(STATUS "Tests configured. Build 'run_tests' target to execute tests.")
message(STATUS "Benchmark configured. Build 'run_benchmark' or 'run_benchmark_quick' targets.")
message(STATUS "Extended benchmarks: 'run_extended_benchmark', 'run_density_sweep', 'run_activity_test'.")
