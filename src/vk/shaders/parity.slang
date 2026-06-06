// The SAME parity kernel as parity.comp, authored in Slang — the ADR-0002
// validation artifact: one source, compiled by slangc to SPIR-V, must produce
// bit-identical results to the glslang-compiled GLSL kernel and the CPU
// mirror on real hardware.
//
// SOURCE OF TRUTH for parity.slang.spv (committed beside it). Regenerate with
// the PINNED toolchain (supply-chain policy):
//   slangc (v2026.10.2) parity.slang -target spirv -o parity.slang.spv
// Reflection evidence: add -reflection-json parity.slang.reflection.json
//
// Binding contract: first (only) resource -> descriptor set 0, binding 0,
// storage buffer — matching the pipeline layout in compute.cpp.

RWStructuredBuffer<uint> data;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid: SV_DispatchThreadID)
{
	uint v = data[tid.x];
	v = (v * 0x9E3779B9u) ^ (v >> 13);
	data[tid.x] = v;
}
