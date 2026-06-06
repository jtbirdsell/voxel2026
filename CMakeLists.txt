cmake_minimum_required(VERSION 3.25)

project(voxel2026
	VERSION 0.0.1
	DESCRIPTION "Clean-break voxel engine architecture program"
	LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings-as-errors from commit 1 (CONTRIBUTING.md), applied per-target so
# third-party code (Catch2 via FetchContent) builds with its own flags.
add_library(voxel2026_warnings INTERFACE)
if(MSVC)
	target_compile_options(voxel2026_warnings INTERFACE /W4 /WX /permissive-)
else()
	target_compile_options(voxel2026_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(voxel2026_core INTERFACE)
target_include_directories(voxel2026_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(voxel2026 src/main.cpp)
target_link_libraries(voxel2026 PRIVATE voxel2026_core voxel2026_warnings)

include(CTest)
if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
