# Spike-1 minigen: INTEGER-ONLY module. Unlike src/sim it carries no pinned
# FP flags because it contains no floating-point arithmetic at all — its
# determinism is a structural property (pure functions of seed + coordinates),
# which is exactly what the spike demonstrates.

add_library(voxel2026_gen STATIC
	minigen.cpp
)
target_include_directories(voxel2026_gen PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel2026_gen
	PRIVATE voxel2026_warnings
	PUBLIC voxel2026_sanitize
)
