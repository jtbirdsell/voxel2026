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

# Local-only sanitizer opt-in (default OFF; CI runs without it for now — a
# follow-up issue adds Linux ASan/UBSan/TSan CI legs). Applied GLOBALLY in a
# dedicated build tree: MSVC's ASan container annotations are an ABI that
# must match across every object including Catch2 (LNK2038 otherwise), and
# whole-program instrumentation is the methodologically correct mode anyway.
# 'address' = ASan (MSVC) / ASan+UBSan (GCC/Clang); 'thread' = TSan (GCC/Clang).
set(VOXEL2026_SANITIZE "" CACHE STRING
	"Sanitize the whole build: '' (off), 'address', or 'thread' (GCC/Clang only)")
add_library(voxel2026_sanitize INTERFACE) # retained as a no-op link target
if(VOXEL2026_SANITIZE STREQUAL "address")
	if(MSVC)
		# /Zi: ASan without debug info trips warning C5072, which /WX promotes.
		add_compile_options(/fsanitize=address /Zi)
		add_link_options(/DEBUG)
	else()
		# -fno-sanitize-recover: UBSan findings abort instead of printing and
		# continuing, so the CI leg is a blocking gate, not advisory output.
		add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
			-fno-omit-frame-pointer)
		add_link_options(-fsanitize=address,undefined)
	endif()
elseif(VOXEL2026_SANITIZE STREQUAL "thread")
	if(MSVC)
		message(FATAL_ERROR "TSan is not available with MSVC; use GCC/Clang")
	endif()
	add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
	add_link_options(-fsanitize=thread)
elseif(NOT VOXEL2026_SANITIZE STREQUAL "")
	message(FATAL_ERROR "VOXEL2026_SANITIZE must be '', 'address', or 'thread'")
endif()

add_library(voxel2026_core INTERFACE)
target_include_directories(voxel2026_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src/sim)
add_subdirectory(src/gen)

add_executable(voxel2026 src/main.cpp)
target_link_libraries(voxel2026 PRIVATE voxel2026_core voxel2026_warnings)

# Prints the Contract 5 replay checkpoint hashes; used to mint/regenerate the
# goldens in tests/test_replay_determinism.cpp and to localize CI divergence.
add_executable(replay_hash tools/replay_hash.cpp)
target_link_libraries(replay_hash PRIVATE voxel2026_sim voxel2026_warnings)

include(CTest)
if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
