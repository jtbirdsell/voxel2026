// Prints the replay checkpoint hashes. Used to mint the golden values in
// tests/test_replay_determinism.cpp and to localize divergence if a CI leg
// ever disagrees (the first differing checkpoint brackets the failure).

#include <cinttypes>
#include <cstdio>

#include "sim/replay.hpp"

int main()
{
	const sim::ReplayResult r = sim::runReplay();
	for (int i = 0; i < sim::kReplayCheckpoints; ++i) {
		std::printf("checkpoint %d (step %5d): 0x%016" PRIX64 "\n",
				i, (i + 1) * sim::kReplayCheckpointInterval,
				r.checkpoints[static_cast<std::size_t>(i)]);
	}
	return 0;
}
