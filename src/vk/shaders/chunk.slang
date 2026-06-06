// World-view chunk shaders (issue #21) — the classic vertex-path renderer
// for the first walkable world. Consumes the mesher's 8-byte PackedVertex
// AS-IS (two u32 vertex attributes; the unpack here mirrors
// mesh/greedy.hpp's field layout bit for bit — the test goldens break if
// either side drifts):
//
//   word 0: x | y<<5 | z<<10 | face<<15   (chunk-local corner coords 0..16)
//   word 1: palette slot / material id    (low 16 bits)
//
// v1 appearance, deterministic by construction (no textures yet): albedo is
// a hash of the material slot (stable across runs — the validation asserts
// on derived colors), shaded by a fixed-direction lambert off the face
// normal. Light/AO vertex bits land in Phase 2 per the mesher's reserved
// fields.
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc chunk.slang -target spirv -matrix-layout-column-major -o chunk.spv
// (explicit matrix layout: the CPU math in vk/worldview.hpp is column-major
// with column vectors; an implicit-default drift here transposes the view)

struct Push {
	float4x4 viewProj;    // column-vector convention: clip = viewProj * world
	float3 chunkOrigin;   // world position of the chunk's (0,0,0) corner
	float pad;
};
[[vk::push_constant]] Push push;

static const float3 kFaceNormals[6] = {
	float3(1, 0, 0), float3(-1, 0, 0),
	float3(0, 1, 0), float3(0, -1, 0),
	float3(0, 0, 1), float3(0, 0, -1),
};

struct VsOut {
	float4 pos : SV_Position;
	float3 color : COLOR0;
};

[shader("vertex")]
VsOut vsMain([[vk::location(0)]] uint2 packed: POSITION)
{
	const uint w0 = packed.x;
	const float3 local = float3(
			float(w0 & 0x1F), float((w0 >> 5) & 0x1F), float((w0 >> 10) & 0x1F));
	const uint face = (w0 >> 15) & 0x7;
	const uint slot = packed.y & 0xFFFF;

	// Deterministic slot albedo: integer hash -> three channels in
	// [0.25, 1.0) so no material ever shades to pure black.
	const uint h = slot * 2654435761u;
	const float3 albedo = float3(
			0.25f + 0.75f * (float((h >> 8) & 0xFF) / 255.0f),
			0.25f + 0.75f * (float((h >> 16) & 0xFF) / 255.0f),
			0.25f + 0.75f * (float((h >> 24) & 0xFF) / 255.0f));

	// Fixed sun-ish lambert: face normals only (greedy quads are flat).
	const float3 lightDir = normalize(float3(0.5f, 1.0f, 0.25f));
	const float lambert = 0.35f + 0.65f * max(0.0f, dot(kFaceNormals[face], lightDir));

	VsOut o;
	o.pos = mul(push.viewProj, float4(push.chunkOrigin + local, 1.0f));
	o.color = albedo * lambert;
	return o;
}

[shader("fragment")]
float4 fsMain(VsOut input) : SV_Target
{
	return float4(input.color, 1.0f);
}
