// VCT pass 3 — the per-pixel cost driver: a 6-cone diffuse gather through
// the sampled radiance clipmap (hardware trilinear, lod = cone radius), from
// a synthetic surface grid standing in for G-buffer positions.
//
// Benchmark-validity design (review-corrected — the original marched ~8 of
// 64 nominal steps before exiting the unit cube, silently under-measuring):
//  - The volume sampler uses REPEAT addressing and there is NO bounds break:
//    cones march through the infinitely tiled scene, so the step budget is
//    actually executed and rows at different extents are comparable.
//  - Occlusion early-out is a push-constant flag: OFF measures the
//    fixed-work worst case (the cost envelope's top); ON measures
//    content-dependent realistic cost.
//  - An optional executed-step counter (separate untimed pass) reports the
//    REAL average steps/cone so costs are quoted per executed work, never
//    per nominal cap.
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc vct_trace.slang -target spirv -o vct_trace.spv

Texture3D<float4> radiance;        // set 0, binding 0: full mip chain
SamplerState volumeSampler;        // set 0, binding 1: trilinear, REPEAT
RWStructuredBuffer<float4> output; // set 0, binding 2: one gather per pixel
RWStructuredBuffer<uint> stats;    // set 0, binding 3: [0] total executed steps

struct Push {
	uint width;
	uint height;
	uint coneSteps;
	uint extent;     // level-0 edge, for lod math
	float originY;   // normalized surface height (fixed ABSOLUTE voxel height
	                 // divided by extent on the CPU side, so every extent row
	                 // measures the same surface-over-terrain geometry)
	uint flags;      // bit0: occlusion early-out enabled; bit1: count steps
};
[[vk::push_constant]] Push push;

// Classic 6-cone hemisphere arrangement (one up, five tilted ring).
static const float3 kConeDirs[6] = {
	float3(0.0f, 1.0f, 0.0f),
	float3(0.0f, 0.5f, 0.866025f),
	float3(0.823639f, 0.5f, 0.267617f),
	float3(0.509037f, 0.5f, -0.700629f),
	float3(-0.509037f, 0.5f, -0.700629f),
	float3(-0.823639f, 0.5f, 0.267617f),
};
static const float kConeAperture = 0.577f; // ~60 degree cones

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
	if (tid.x >= push.width || tid.y >= push.height)
		return;

	const bool earlyOut = (push.flags & 1u) != 0;
	const bool countSteps = (push.flags & 2u) != 0;

	const float3 origin = float3(
			(float(tid.x) + 0.5f) / float(push.width),
			push.originY,
			(float(tid.y) + 0.5f) / float(push.height));

	const float maxLod = log2(float(push.extent));
	float4 total = float4(0, 0, 0, 0);
	uint executed = 0;

	[loop]
	for (uint c = 0; c < 6; ++c) {
		const float3 dir = kConeDirs[c];
		float3 accumRgb = float3(0, 0, 0);
		float occlusion = 0.0f;
		float t = 1.5f / float(push.extent);

		[loop]
		for (uint s = 0; s < push.coneSteps; ++s) {
			if (earlyOut && occlusion >= 0.99f)
				break;
			const float radius = t * kConeAperture;
			const float lod = clamp(log2(radius * float(push.extent)), 0.0f, maxLod);
			const float3 p = origin + dir * t; // REPEAT sampler tiles the scene
			const float4 sample = radiance.SampleLevel(volumeSampler, p, lod);
			accumRgb += (1.0f - occlusion) * sample.rgb * sample.a;
			occlusion += (1.0f - occlusion) * sample.a * 0.35f;
			t += radius + (1.0f / float(push.extent));
			++executed;
		}
		total += float4(accumRgb, occlusion) * (1.0f / 6.0f);
	}

	output[tid.y * push.width + tid.x] = total;
	if (countSteps) {
		uint ignored;
		InterlockedAdd(stats[0], executed, ignored);
	}
}
