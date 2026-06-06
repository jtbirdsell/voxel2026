// RC pass 4 — per-pixel query: the radiance-cascade analog of the VCT cone
// gather's per-frame cost. One thread per synthetic pixel: trilinearly
// interpolate the 8 surrounding cascade-0 ambient cubes (6 faces each = 48
// fetches) and project a pseudo-normal onto the blended cube.
//
// The pseudo-normal varies per pixel at runtime (hash of the pixel index,
// upper hemisphere) so the 6-face projection does representative work — a
// constant normal would let the compiler fold five of the six face fetches
// away and under-measure the pass.
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc rc_sample.slang -target spirv -o rc_sample.spv

RWStructuredBuffer<float4> cubes;  // set 0, binding 0: probeCount x 6 faces
RWStructuredBuffer<float4> output; // set 0, binding 1: one quad per pixel

struct Push {
	uint width;
	uint height;
	uint probesAxis;
	float originYVoxels;  // fixed ABSOLUTE surface height (same as VCT rows)
	float spacingVoxels;  // cascade-0 probe spacing
	float extentVoxels;   // level-0 volume edge
};
[[vk::push_constant]] Push push;

static const float3 kAxes[6] = {
	float3(1, 0, 0), float3(-1, 0, 0),
	float3(0, 1, 0), float3(0, -1, 0),
	float3(0, 0, 1), float3(0, 0, -1),
};

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
	if (tid.x >= push.width || tid.y >= push.height)
		return;

	const float3 posVoxels = float3(
			(float(tid.x) + 0.5f) / float(push.width) * push.extentVoxels,
			push.originYVoxels,
			(float(tid.y) + 0.5f) / float(push.height) * push.extentVoxels);

	// Probe-grid coordinate; clamp-to-edge like the merge pass.
	const float3 f = posVoxels / push.spacingVoxels - 0.5f;
	const float3 fl = floor(f);
	const int3 base = int3(fl);
	const float3 frac = f - fl;
	const int axisMax = int(push.probesAxis) - 1;

	// Per-pixel pseudo-normal, upper hemisphere (y component dominates).
	const float3 n = normalize(float3(
			sin(float(tid.x) * 0.7331f), 1.0f, cos(float(tid.y) * 1.7193f)));

	// Trilinear-blend the ambient cube, then project the normal onto it.
	float3 faces[6] = {float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0),
			float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0)};
	[unroll]
	for (uint cz = 0; cz < 2; ++cz)
		[unroll]
		for (uint cy = 0; cy < 2; ++cy)
			[unroll]
			for (uint cx = 0; cx < 2; ++cx) {
				const int3 c = clamp(base + int3(int(cx), int(cy), int(cz)),
						int3(0, 0, 0), int3(axisMax, axisMax, axisMax));
				const float w = (cx != 0 ? frac.x : 1.0f - frac.x) *
						(cy != 0 ? frac.y : 1.0f - frac.y) *
						(cz != 0 ? frac.z : 1.0f - frac.z);
				const uint probe = (uint(c.z) * push.probesAxis + uint(c.y)) *
						push.probesAxis + uint(c.x);
				[loop]
				for (uint face = 0; face < 6; ++face)
					faces[face] += w * cubes[probe * 6 + face].rgb;
			}

	float3 irr = float3(0, 0, 0);
	float wsum = 0.0f;
	[unroll]
	for (uint face = 0; face < 6; ++face) {
		const float w = max(0.0f, dot(n, kAxes[face]));
		irr += w * faces[face];
		wsum += w;
	}
	output[tid.y * push.width + tid.x] = float4(irr / max(wsum, 1e-6f), 1.0f);
}
