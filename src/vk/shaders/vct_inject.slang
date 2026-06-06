// VCT pass 1 — radiance injection: unpack the scene buffer (bit 31 = solid,
// low 24 bits = RGB emissive) into radiance level 0. Alpha carries occupancy
// for the cone gather's occlusion accumulation.
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc vct_inject.slang -target spirv -o vct_inject.spv

StructuredBuffer<uint> scene; // set 0, binding 0: extent^3 packed voxels
[[vk::image_format("rgba8")]]
RWTexture3D<float4> radianceL0; // set 0, binding 1

struct Push {
	uint extent;
};
[[vk::push_constant]] Push push;

[shader("compute")]
[numthreads(4, 4, 4)]
void main(uint3 tid: SV_DispatchThreadID)
{
	if (any(tid >= uint3(push.extent, push.extent, push.extent)))
		return;
	const uint idx = tid.x + push.extent * (tid.y + push.extent * tid.z);
	const uint v = scene[idx];
	const bool solid = (v & 0x80000000u) != 0;
	const float3 emissive = float3(
			float((v >> 16) & 0xFFu),
			float((v >> 8) & 0xFFu),
			float(v & 0xFFu)) / 255.0f;
	radianceL0[tid] = float4(emissive, solid ? 1.0f : 0.0f);
}
