// Prints the replay checkpoint hashes. Used to mint the golden values in
// tests/test_replay_determinism.cpp and to localize divergence if a CI leg
// ever disagrees (the first differing checkpoint brackets the failure).

#include <cinttypes>
#include <cstdio>
#include <vector>

#include "sim/rebase.hpp"
#include "sim/replay.hpp"

int main()
{
	const sim::ReplayResult r = sim::runReplay();
	std::puts("spike-3 replay (tests/test_replay_determinism.cpp goldens):");
	for (int i = 0; i < sim::kReplayCheckpoints; ++i) {
		std::printf("checkpoint %d (step %5d): 0x%016" PRIX64 "\n",
				i, (i + 1) * sim::kReplayCheckpointInterval,
				r.checkpoints[static_cast<std::size_t>(i)]);
	}

	// Same standard schedule as tests/test_rebase.cpp.
	const std::vector<sim::RebaseEvent> schedule = {{1024, 192}, {2048, 192}, {3072, 192}};
	const sim::RebasedReplayResult rb = sim::runRebasedReplay(schedule);
	std::puts("spike-2 rebased replay (tests/test_rebase.cpp goldens):");
	for (int i = 0; i < sim::kRebaseCheckpoints; ++i) {
		std::printf("checkpoint %d (step %5d): 0x%016" PRIX64 "\n",
				i, (i + 1) * sim::kRebaseCheckpointInterval,
				rb.checkpoints[static_cast<std::size_t>(i)]);
	}
	std::printf("allRebasesExact: %s\n", rb.allRebasesExact ? "true" : "false");
	return 0;
}
