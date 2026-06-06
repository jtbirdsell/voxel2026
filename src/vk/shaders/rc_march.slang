// RC pass 1 — cascade interval march: one thread per (probe, direction)
// ray of one cascade level. Each ray marches its level's radial interval
// [tStart, tEnd) through the SAME radiance mip chain the VCT path samples
// (level i samples mip i: step length and filter footprint scale together),
// accumulating front-to-back with the SAME per-sample opacity convention as
// vct_trace (occlusion += a * 0.35 per sample, here kept as transmittance
// T *= 1 - a * 0.35) so per-sample semantics match across the techniques
// being compared.
//
// Benchmark-validity rules carried over from the reviewed VCT kernel:
//  - REPEAT-addressed sampler, NO bounds break: high-cascade rays march far
//    outside the volume; tiling keeps every step doing real filtered work.
//  - Occlusion early-out is a push flag (OFF = fixed-work worst case).
//  - Optional executed-step counter (untimed pass) -> stats[level].
//
// Direction set: equal-ish-area octahedral map, octaSide^2 directions per
// probe; the canonical cascade scaling doubles octaSide per level. The
// decode is duplicated in rc_resolve.slang (slangc compiles standalone
// files here; keep the two in sync).
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc rc_march.slang -target spirv -o rc_march.spv

Texture3D<float4> radiance;          // set 0, binding 0: full mip chain
SamplerState volumeSampler;          // set 0, binding 1: trilinear, REPEAT
RWStructuredBuffer<float4> cascades; // set 0, binding 2: all levels, packed
RWStructuredBuffer<uint> stats;      // set 0, binding 3: executed steps/level

struct Push {
	uint probesAxis;     // probes per axis at this level
	uint octaSide;       // direction-map side; dirCount = octaSide^2
	uint offsetEntries;  // float4 index of this level in `cascades`
	uint stepCount;      // nominal steps over this level's interval
	float spacingVoxels; // probe spacing at this level
	float tStartVoxels;  // interval start (radial, voxels)
	float stepVoxels;    // march step length (voxels)
	float lod;           // mip sampled at this level
	float invExtent;     // 1 / level-0 volume edge
	uint level;          // cascade index (stats slot)
	uint flags;          // bit0: occlusion early-out; bit1: count steps
};
[[vk::push_constant]] Push push;

float3 octaDecode(uint du, uint dv, uint side)
{
	const float2 f =
			(float2(float(du) + 0.5f, float(dv) + 0.5f) / float(side)) * 2.0f - 1.0f;
	float3 n = float3(f.x, 1.0f - abs(f.x) - abs(f.y), f.y);
	if (n.y < 0.0f) {
		const float2 sgn = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.z >= 0.0f ? 1.0f : -1.0f);
		const float2 xz = (float2(1.0f, 1.0f) - abs(float2(n.z, n.x))) * sgn;
		n.x = xz.x;
		n.z = xz.y;
	}
	return normalize(n);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
	const uint probeCount = push.probesAxis * push.probesAxis * push.probesAxis;
	if (tid.x >= probeCount)
		return;

	const bool earlyOut = (push.flags & 1u) != 0;
	const bool countSteps = (push.flags & 2u) != 0;
	const uint dirCount = push.octaSide * push.octaSide;

	const uint px = tid.x % push.probesAxis;
	const uint py = (tid.x / push.probesAxis) % push.probesAxis;
	const uint pz = tid.x / (push.probesAxis * push.probesAxis);
	const float3 origin = (float3(float(px), float(py), float(pz)) + 0.5f) *
			push.spacingVoxels * push.invExtent;
	const float3 dir = octaDecode(tid.y % push.octaSide, tid.y / push.octaSide,
			push.octaSide);

	float t = push.tStartVoxels + 0.5f * push.stepVoxels;
	float3 rad = float3(0, 0, 0);
	float T = 1.0f;
	uint executed = 0;

	[loop]
	for (uint s = 0; s < push.stepCount; ++s) {
		if (earlyOut && T < 0.01f)
			break;
		const float3 p = origin + dir * (t * push.invExtent); // REPEAT tiles
		const float4 smp = radiance.SampleLevel(volumeSampler, p, push.lod);
		rad += T * smp.rgb * smp.a;
		T *= 1.0f - smp.a * 0.35f;
		t += push.stepVoxels;
		++executed;
	}

	cascades[push.offsetEntries + tid.x * dirCount + tid.y] = float4(rad, T);
	if (countSteps) {
		uint ignored;
		InterlockedAdd(stats[push.level], executed, ignored);
	}
}
