cmake_minimum_required(VERSION 3.10)
project(ConwayGameOfLife)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

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

# Backend library
add_library(
        GameOfLifeLib
        src/GameOfLife.cpp
        src/GameOfLife.h
)

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
