# Greedy chunk mesher (issue #17) — CPU geometry over Contract-1 cells.
add_library(voxel2026_mesh STATIC
	greedy.cpp
)
target_include_directories(voxel2026_mesh PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel2026_mesh PRIVATE voxel2026_warnings)
