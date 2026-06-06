include(FetchContent)

# Pinned per the supply-chain policy (CONTRIBUTING.md): exact release tag.
FetchContent_Declare(
	Catch2
	GIT_REPOSITORY https://github.com/catchorg/Catch2.git
	GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(voxel2026_tests
	test_collision_step.cpp
	test_det_math.cpp
	test_minigen.cpp
	test_morton.cpp
	test_replay_determinism.cpp
)
target_link_libraries(voxel2026_tests PRIVATE voxel2026_core voxel2026_gen voxel2026_sim
	voxel2026_warnings voxel2026_sanitize Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(voxel2026_tests)
