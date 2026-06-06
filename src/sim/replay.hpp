// Deterministic replay harness for the Contract 5 FP-determinism spike.
//
// Runs a fixed, PRNG-driven population of entities through collisionStep()
// and hashes the full bit-level state (positions, velocities, ground flags)
// at fixed checkpoints. Two builds are FP-deterministic with respect to each
// other exactly when they produce identical checkpoint sequences.

#pragma once

#include <array>
#include <cstdint>

namespace sim {

inline constexpr std::uint64_t kReplaySeed = 0x564F58454C323032ull; // "VOXEL202"
inline constexpr int kReplayEntities = 64;
inline constexpr int kReplaySteps = 4096;
inline constexpr int kReplayCheckpointInterval = 512;
inline constexpr int kReplayCheckpoints = kReplaySteps / kReplayCheckpointInterval; // 8

struct ReplayResult {
	// FNV-1a 64 of all entity state bits at each checkpoint step
	// (after steps 512, 1024, ..., 4096). The last entry is the final state.
	std::array<std::uint64_t, kReplayCheckpoints> checkpoints{};
};

// Pure function of the constants above — no time, no I/O, no threads.
ReplayResult runReplay();

} // namespace sim
