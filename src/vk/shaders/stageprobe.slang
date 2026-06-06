// Stage-emission probe for ADR-0002: vertex, fragment, and amplification
// (task) entry points in one module. Its committed compilation
// (stageprobe.spv, beside this file) is the verifiable compile-only evidence
// that slangc emits valid SPIR-V for every stage the renderer architecture
// uses, beyond the compute path that runs in tests. Not embedded in the
// build. Toolchain pin + regeneration: docs/tooling/slang.md.

struct VOut {
	float4 pos : SV_Position;
	float3 color : COLOR0;
};

[shader("vertex")]
VOut vmain(uint vid: SV_VertexID)
{
	VOut o;
	float2 p = float2(float((vid << 1) & 2), float(vid & 2));
	o.pos = float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
	o.color = float3(p, 0.5f);
	return o;
}

[shader("fragment")]
float4 fmain(VOut i) : SV_Target
{
	return float4(i.color, 1.0f);
}

struct TaskPayload {
	uint meshletIndex;
};

[shader("amplification")]
[numthreads(1, 1, 1)]
void tmain(uint3 tid: SV_DispatchThreadID)
{
	TaskPayload payload;
	payload.meshletIndex = tid.x;
	DispatchMesh(1, 1, 1, payload);
}
