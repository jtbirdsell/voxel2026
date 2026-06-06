# The simulation kernel is the Contract 5 pinned-FP unit: these flags apply to
# its translation units regardless of the parent build type, which is exactly
# the mechanism the spike validates (bit-identity across compilers, platforms,
# and -O levels).
#
#   MSVC      /fp:strict        no contractions, no FP reassociation
#   GCC/Clang -ffp-contract=off no FMA contraction
#             -fno-fast-math    no reassociation/unsafe FP (also the default;
#                               pinned here so a parent flag can never leak in)
#
# FP reassociation is what would make autovectorized reductions
# order-dependent; with fast-math off, conforming compilers cannot reassociate,
# so no extra vectorization flags are needed.

# Negative control (review finding — falsifiability): a build with contraction
# ENABLED must produce trajectories that DIVERGE from the goldens, proving the
# pinned flags are load-bearing rather than decorative. The golden test flips
# to an inequality assertion under this define.
option(VOXEL2026_SIM_UNPINNED_CONTROL
	"Build the sim kernel WITHOUT pinned FP flags (FMA contraction on) as a determinism negative control"
	OFF)

add_library(voxel2026_sim STATIC
	collision_step.cpp
	rebase.cpp
	replay.cpp
)
target_include_directories(voxel2026_sim PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel2026_sim PRIVATE voxel2026_warnings)

if(VOXEL2026_SIM_UNPINNED_CONTROL)
	target_compile_definitions(voxel2026_sim PUBLIC VOXEL2026_SIM_UNPINNED_CONTROL=1)
	if(MSVC)
		# /fp:fast + AVX2 codegen lets MSVC emit vfmadd for a*b + c.
		target_compile_options(voxel2026_sim PRIVATE /fp:fast /arch:AVX2)
	else()
		# Plain x86-64 has no FMA instruction; -mfma makes contraction real.
		target_compile_options(voxel2026_sim PRIVATE -ffp-contract=fast -mfma -mavx2)
	endif()
elseif(MSVC)
	target_compile_options(voxel2026_sim PRIVATE /fp:strict)
else()
	target_compile_options(voxel2026_sim PRIVATE -ffp-contract=off -fno-fast-math)
endif()
