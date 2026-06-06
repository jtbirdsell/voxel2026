// Mesh-pipeline EXECUTION kernel (issue #16) — closes ADR-0002's standing
// condition: mesh-stage emission was accepted compile-only at the toolchain
// gate; this kernel is the renderer gate, executing on hardware with
// exact-pixel validated output.
//
// Geometry is chosen for falsifiability, not beauty: one mesh workgroup
// emits an indexed quad (4 vertices, 2 triangles — exercising the separate
// vertex/index output arrays, not a degenerate 3-vertex path) covering
// exactly the LEFT HALF of clip space (x in [-1, 0]). At any even render
//-target width W, pixel centers x+0.5 for x < W/2 are strictly inside and
// x >= W/2 strictly outside — no edge-on-center ambiguity, so the readback
// asserts EXACT bytes: left half = kFillColor, right half = the clear color.
//
// Fragment color constants are exact under UNORM8 conversion
// (round(32/255 * 255) == 32, etc.).
//
// Pin + regeneration: docs/tooling/slang.md →
//   slangc meshexec.slang -target spirv -o meshexec.spv

struct MeshVertex {
	float4 pos : SV_Position;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain(out indices uint3 triangles[2], out vertices MeshVertex verts[4])
{
	SetMeshOutputCounts(4, 2);

	// Left half of clip space; full Y range. (Vulkan clip -> framebuffer
	// mapping flips Y, which is irrelevant here by symmetry.)
	verts[0].pos = float4(-1.0f, -1.0f, 0.0f, 1.0f);
	verts[1].pos = float4(0.0f, -1.0f, 0.0f, 1.0f);
	verts[2].pos = float4(-1.0f, 1.0f, 0.0f, 1.0f);
	verts[3].pos = float4(0.0f, 1.0f, 0.0f, 1.0f);

	triangles[0] = uint3(0, 1, 2);
	triangles[1] = uint3(2, 1, 3);
}

[shader("fragment")]
float4 fragMain() : SV_Target
{
	return float4(32.0f / 255.0f, 64.0f / 255.0f, 96.0f / 255.0f, 1.0f);
}
