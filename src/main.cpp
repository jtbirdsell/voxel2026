// voxel2026 — program skeleton.
//
// This binary intentionally does nothing yet: the project is in its design
// phase (see docs/architecture.md). It exists so the toolchain, warnings-as-
// errors policy, and CI matrix are exercised from the first commit.

#include <cstdio>

#include "voxel/morton.hpp"

int main()
{
	constexpr voxel::LocalPos corner{15, 15, 15};
	static_assert(voxel::mortonDecode(voxel::mortonEncode(corner)) == corner,
			"Morton codec must round-trip at compile time");

	std::puts("voxel2026 0.0.1 — design phase; see docs/architecture.md");
	return 0;
}
