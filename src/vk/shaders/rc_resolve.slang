// RC pass 3 — probe resolve: fold each cascade-0 probe's merged directional
// radiance into a 6-face ambient cube (cosine-weighted per face), so the
// per-pixel sample pass fetches 6 values per probe instead of integrating
// octaSide^2 directions per pixel. One thread per cascade-0 probe.
//
// Cascade 0 sits at entry offset 0 in the packed cascade buffer by
// construction (levels are packed in ascending order).
//
// octaDecode is duplicated from rc_march.slang (standalone compilation);
// keep the two in sync.
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc rc_resolve.slang -target spirv -o rc_resolve.spv

RWStructuredBuffer<float4> cascades; // set 0, binding 0: level 0 read at offset 0
RWStructuredBuffer<float4> cubes;    // set 0, binding 1: probeCount x 6 faces

struct Push {
	uint probesAxis;
	uint octaSide;
};
[[vk::push_constant]] Push push;

static const float3 kAxes[6] = {
	float3(1, 0, 0), float3(-1, 0, 0),
	float3(0, 1, 0), float3(0, -1, 0),
	float3(0, 0, 1), float3(0, 0, -1),
};

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

	const uint dirCount = push.octaSide * push.octaSide;

	[loop]
	for (uint face = 0; face < 6; ++face) {
		float3 sum = float3(0, 0, 0);
		float wsum = 0.0f;
		[loop]
		for (uint d = 0; d < dirCount; ++d) {
			const float3 dir =
					octaDecode(d % push.octaSide, d / push.octaSide, push.octaSide);
			const float w = max(0.0f, dot(kAxes[face], dir));
			sum += w * cascades[tid.x * dirCount + d].rgb;
			wsum += w;
		}
		cubes[tid.x * 6 + face] = float4(sum / max(wsum, 1e-6f), 1.0f);
	}
}
