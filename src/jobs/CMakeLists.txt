# Job-system substrate (issue #20, ADR-0012). enkiTS is pinned per the
# supply-chain policy and confined behind the jobs:: facade — its headers are
# PRIVATE to this library by design (the ADR exit strategy).

include(FetchContent)

FetchContent_Declare(
	enkiTS
	GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
	GIT_TAG        v1.11
)
set(ENKITS_BUILD_EXAMPLES OFF)
set(ENKITS_BUILD_C_INTERFACE OFF)
set(ENKITS_INSTALL OFF)
# enkiTS v1.11 declares a pre-3.5 cmake_minimum_required, which CMake >= 4
# refuses outright; this is the documented escape hatch and affects ONLY
# projects declaring such minimums (ours declares 3.25).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(enkiTS)
unset(CMAKE_POLICY_VERSION_MINIMUM)

add_library(voxel2026_jobs STATIC
	jobs.cpp
)
target_include_directories(voxel2026_jobs PUBLIC ${CMAKE_SOURCE_DIR}/src)
# enkiTS v1.11 uses directory-scoped include_directories (never exported on
# the target), so the header path is added here explicitly — PRIVATE, like
# everything about enkiTS (the facade boundary).
target_include_directories(voxel2026_jobs PRIVATE ${enkits_SOURCE_DIR}/src)
target_link_libraries(voxel2026_jobs
	PRIVATE voxel2026_warnings enkiTS
)
