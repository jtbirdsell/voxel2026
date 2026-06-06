# Job-system substrate (issue #20, ADR-0012). enkiTS is pinned per the
# supply-chain policy and confined behind the jobs:: facade — its headers are
# PRIVATE to this library by design (the ADR exit strategy).

include(FetchContent)

# SHA pin, deliberately ahead of the v1.11 tag: the first TSan CI pass
# showed v1.11's wait path leaving a trailing atomic write to the TaskSet's
# own counter after WaitforTask returned (use-after-wait on a stack task);
# upstream fixed the completion memory ordering on master in June 2023
# (m_RunningCount / WaitForTaskCompletion to acq_rel), post-tag. Pinned to
# master as of 2026-06-03 — ADR-0012 records the episode.
FetchContent_Declare(
	enkiTS
	GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
	GIT_TAG        0289cf6ffce86697e1f46cb4eeca1c946bffec71
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
