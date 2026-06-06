// Mesh-STAGE emission probe for ADR-0002: not embedded in the build — its
// committed compilation (meshprobe.spv, beside this file) is the verifiable
// evidence that the pinned slangc emits valid SPIR-V for the mesh stage
// (SPV_EXT_mesh_shader), which the architecture's flagship chunk path depends
// on. Pipeline-level validation happens with the first real mesh kernel.
// Toolchain pin + regeneration: docs/tooling/slang.md.

struct V {
	float4 pos : SV_Position;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void main(out vertices V verts[3], out indices uint3 tris[1])
{
	SetMeshOutputCounts(3, 1);
	verts[0].pos = float4(0, 0, 0, 1);
	verts[1].pos = float4(1, 0, 0, 1);
	verts[2].pos = float4(0, 1, 0, 1);
	tris[0] = uint3(0, 1, 2);
}
