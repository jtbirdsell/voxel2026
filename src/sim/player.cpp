// Pinned-FP translation unit — see src/sim/CMakeLists.txt. Only
// IEEE-determined operations are permitted here (det_math.hpp).

#include "sim/player.hpp"

namespace sim {

void playerStep(Entity &e, const PlayerInput &in, const WalkParams &walk,
		const StepParams &params, const WorldOffset &origin, SolidFn solid,
		const void *ctx)
{
	// Single IEEE multiplies — correctly rounded, no fusible mul-add here
	// (the kernel's load-bearing fusible site stays the gravity integration
	// inside collisionStep).
	e.vel.x = in.moveX * walk.moveSpeed;
	e.vel.z = in.moveZ * walk.moveSpeed;

	// Jump off the previous tick's ground contact. Exact assignment; the
	// collision step's gravity/damping take over from the next line on.
	if (in.jump && e.onGround)
		e.vel.y = walk.jumpSpeed;

	collisionStep(e, params, origin, solid, ctx);
}

} // namespace sim
