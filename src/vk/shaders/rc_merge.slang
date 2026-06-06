// RC pass 2 — cascade merge: composite level i+1's far-field intervals onto
// level i, top-down (i = L-2 .. 0), in place. One thread per destination
// (probe, direction) entry: trilinearly interpolate the 8 surrounding
// source probes (source spacing is 2x), average the 4 child directions of
// this direction (source octa side is 2x), then composite behind the near
// interval with the stored transmittance:
//
//   merged.rgb = near.rgb + near.T * far.rgb
//   merged.T   = near.T * far.T
//
// This compositing is associative, which the tests exploit: on a uniform
// volume the fully merged cascade 0 must equal one continuous march over
// the whole radial range (analytic closed form).
//
// Cost shape: 8 probes x 4 directions = 32 buffer fetches per entry.
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc rc_merge.slang -target spirv -o rc_merge.spv

RWStructuredBuffer<float4> cascades; // set 0, binding 0: all levels, packed

struct Push {
	uint dstProbesAxis;
	uint dstOctaSide;
	uint dstOffsetEntries;
	uint srcProbesAxis;    // dst axis / 2 (rounded), spacing 2x
	uint srcOffsetEntries;
};
[[vk::push_constant]] Push push;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
	const uint probeCount =
			push.dstProbesAxis * push.dstProbesAxis * push.dstProbesAxis;
	if (tid.x >= probeCount)
		return;

	const uint dstDirCount = push.dstOctaSide * push.dstOctaSide;
	const uint srcSide = push.dstOctaSide * 2;
	const uint srcDirCount = srcSide * srcSide;
	const uint du = tid.y % push.dstOctaSide;
	const uint dv = tid.y / push.dstOctaSide;

	const uint px = tid.x % push.dstProbesAxis;
	const uint py = (tid.x / push.dstProbesAxis) % push.dstProbesAxis;
	const uint pz = tid.x / (push.dstProbesAxis * push.dstProbesAxis);

	// Probe centers: world = (p + 0.5) * spacing; source spacing is 2x, so
	// the source-grid coordinate of this probe is (p - 0.5) / 2.
	const float3 f = (float3(float(px), float(py), float(pz)) - 0.5f) * 0.5f;
	const float3 fl = floor(f);
	const int3 base = int3(fl);
	const float3 frac = f - fl;
	const int axisMax = int(push.srcProbesAxis) - 1;

	float4 far = float4(0, 0, 0, 0);
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
				const uint srcProbe = (uint(c.z) * push.srcProbesAxis + uint(c.y)) *
						push.srcProbesAxis + uint(c.x);
				float4 children = float4(0, 0, 0, 0);
				[unroll]
				for (uint j = 0; j < 2; ++j)
					[unroll]
					for (uint i = 0; i < 2; ++i) {
						const uint sd = (2 * dv + j) * srcSide + (2 * du + i);
						children += cascades[push.srcOffsetEntries +
								srcProbe * srcDirCount + sd];
					}
				far += w * 0.25f * children;
			}

	const uint dstIdx = push.dstOffsetEntries + tid.x * dstDirCount + tid.y;
	const float4 near = cascades[dstIdx];
	cascades[dstIdx] = float4(near.rgb + near.a * far.rgb, near.a * far.a);
}
