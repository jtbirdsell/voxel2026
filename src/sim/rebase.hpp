// Streaming origin rebasing for the spike-2 experiment (issue #2,
// docs/spikes/spike-2-origin-rebasing.md) — the Contract 4 mechanism.
//
// Model: entity positions are floats LOCAL to a frame origin held in whole
// cells (int32). The world is queried at origin + local (integer translation
// only — see WorldOffset in collision_step.hpp). Rebasing shifts the origin
// by an integer delta and subtracts that delta from every local position.
//
// The properties under test:
//  1. Two machines applying the SAME rebase schedule stay bit-identical in
//     virtual-absolute space (float subtraction is deterministic, lossy or
//     not — Contract 4's "server and client rebase identically").
//  2. DIFFERENT schedules genuinely diverge (the identical-schedule clause is
//     load-bearing, not decorative).
//  3. Rebase subtraction is bit-EXACT inside the Sterbenz window
//     (delta/2 <= local <= 2*delta, same sign) and deterministically LOSSY
//     outside it (bounded by one ulp of the result) — an honest limit the
//     engine contract must carry: rebasing is lossless near the new origin,
//     micro-rounding for entities far from it, identical on every machine.
//  4. Far-origin ulp starvation is a real disease (motion freezes when the
//     per-step delta drops below the position's ulp) and rebasing cures it.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "sim/collision_step.hpp"

namespace sim {

struct Frame {
	std::int32_t ox = 0, oy = 0, oz = 0; // origin, whole cells
};

// Virtual absolute coordinate in double. Each conversion is exact (int32 and
// binary32 both embed losslessly in binary64); the SUM is exact only when the
// bit span from the origin's MSB down to the local's lowest set bit fits in
// 53 bits (review-corrected — magnitude alone is the wrong invariant). That
// holds throughout this harness (origins stay in the hundreds), so virtual
// hashing here compares exactly. ENGINE-PHASE FLAG: at contract-scale s32
// origins (~2^31) with fractional locals it does NOT hold — two frames
// encoding the same virtual point can reconstruct different doubles; the
// engine must compare via {int32 origin, float local} pairs or
// origin-relative diffs there, never a single double sum.
inline double virtualCoord(std::int32_t origin, float local)
{
	return static_cast<double>(origin) + static_cast<double>(local);
}

// Shift the frame origin by (dx,dy,dz) cells and subtract the delta from
// every entity's local position. Velocities and extents are frame-invariant
// and untouched. Returns true iff EVERY subtraction was bit-exact, verified
// by add-back reconstruction (exactness of x-d makes (x-d)+d == x bitwise).
bool rebaseFrame(Frame &frame, std::span<Entity> entities,
		std::int32_t dx, std::int32_t dy, std::int32_t dz);

// ---- Rebased replay harness -------------------------------------------------

inline constexpr std::uint64_t kRebaseSeed = 0x564F58454C322032ull; // "VOXEL2 2"
inline constexpr int kRebaseEntities = 32;
inline constexpr int kRebaseSteps = 4096;
inline constexpr int kRebaseCheckpointInterval = 512;
inline constexpr int kRebaseCheckpoints = kRebaseSteps / kRebaseCheckpointInterval;

struct RebaseEvent {
	int step;         // 1-based step AFTER which the rebase applies
	std::int32_t dx;  // origin shift in cells (+x drift direction)
};
// Schedule precondition: sorted ascending by `step` (multiple events with the
// same step apply in array order). Out-of-order schedules are rejected by a
// debug assert in runRebasedReplay — they would otherwise be silently
// dropped, yielding an unannounced different trajectory.

struct RebasedReplayResult {
	// FNV-1a-64 over the VIRTUAL-absolute state (double bits of
	// origin+local per axis, float bits of velocity, ground flag) at each
	// checkpoint — comparable across machines with different frames.
	std::array<std::uint64_t, kRebaseCheckpoints> checkpoints{};
	bool allRebasesExact = true;
};

// Deterministic drift population (sustained +x impulses) stepped through the
// collision kernel in the frame's local space, rebased per `schedule`.
// Pure function of (schedule); pins MXCSR like the spike-3 replay.
RebasedReplayResult runRebasedReplay(std::span<const RebaseEvent> schedule);

} // namespace sim
