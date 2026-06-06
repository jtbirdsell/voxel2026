// VCT pass 2 — mip downsample: each destination texel is the plain average
// of its 2x2x2 source block (radiance pre-multiplied semantics are out of
// spike scope; the cost driver is the memory traffic, which this matches).
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc vct_mip.slang -target spirv -o vct_mip.spv

[[vk::image_format("rgba8")]]
RWTexture3D<float4> src; // set 0, binding 0: level N   (read via loads)
[[vk::image_format("rgba8")]]
RWTexture3D<float4> dst; // set 0, binding 1: level N+1 (write)

struct Push {
	uint dstExtent;
};
[[vk::push_constant]] Push push;

[shader("compute")]
[numthreads(4, 4, 4)]
void main(uint3 tid: SV_DispatchThreadID)
{
	if (any(tid >= uint3(push.dstExtent, push.dstExtent, push.dstExtent)))
		return;
	const uint3 s = tid * 2;
	float4 sum = float4(0, 0, 0, 0);
	[unroll]
	for (uint dz = 0; dz < 2; ++dz)
		[unroll]
		for (uint dy = 0; dy < 2; ++dy)
			[unroll]
			for (uint dx = 0; dx < 2; ++dx)
				sum += src[s + uint3(dx, dy, dz)];
	dst[tid] = sum * 0.125f;
}
