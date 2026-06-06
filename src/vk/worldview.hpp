// World-view renderer (issue #21): draws meshed chunks with a depth-tested
// classic vertex pipeline — the belt's last station. Two entry points share
// one implementation: renderWorldViewInto targets a caller-owned image (the
// windowed tool points it at swapchain images), renderWorldView wraps it
// with an offscreen RGBA8 target + readback (the CI-facing, golden-testable
// path).
//
// Spike-shaped per the src/vk house style: per-call pipeline setup, full
// VkResult checking, RAII teardown. Per-frame reuse/caching is the
// follow-up perf issue, deliberately not smuggled into the correctness
// milestone.

#pragma once

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mesh/greedy.hpp"
#include "vk/device.hpp"

namespace vk {

// ---- Minimal column-major mat4 (Vulkan conventions: column vectors,
// clip = M * v, depth 0..1, Y flipped in the projection). Element (row r,
// col c) lives at m[c*4 + r]. Small enough to verify by hand; pinned by
// CPU-side projection tests.

struct Float3 {
	float x = 0, y = 0, z = 0;
};

struct Mat4 {
	float m[16] = {};
};

constexpr Mat4 mat4Mul(const Mat4 &a, const Mat4 &b)
{
	Mat4 out;
	for (int c = 0; c < 4; ++c)
		for (int r = 0; r < 4; ++r) {
			float sum = 0;
			for (int k = 0; k < 4; ++k)
				sum += a.m[k * 4 + r] * b.m[c * 4 + k];
			out.m[c * 4 + r] = sum;
		}
	return out;
}

inline Mat4 mat4Perspective(float fovYRadians, float aspect, float nearZ, float farZ)
{
	const float f = 1.0f / std::tan(fovYRadians * 0.5f);
	Mat4 out;
	out.m[0 * 4 + 0] = f / aspect;
	out.m[1 * 4 + 1] = -f; // Vulkan: framebuffer Y points down
	out.m[2 * 4 + 2] = farZ / (nearZ - farZ);          // depth 0..1, reversed-Z off
	out.m[2 * 4 + 3] = -1.0f;
	out.m[3 * 4 + 2] = (nearZ * farZ) / (nearZ - farZ);
	return out;
}

inline Mat4 mat4LookAt(Float3 eye, Float3 center, Float3 up)
{
	const auto sub = [](Float3 a, Float3 b) { return Float3{a.x - b.x, a.y - b.y, a.z - b.z}; };
	const auto cross = [](Float3 a, Float3 b) {
		return Float3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	};
	const auto dot = [](Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
	const auto norm = [&](Float3 v) {
		const float len = std::sqrt(dot(v, v));
		return Float3{v.x / len, v.y / len, v.z / len};
	};
	const Float3 fwd = norm(sub(center, eye));
	const Float3 side = norm(cross(fwd, up));
	const Float3 u = cross(side, fwd);
	Mat4 out;
	out.m[0] = side.x;  out.m[4] = side.y;  out.m[8] = side.z;   out.m[12] = -dot(side, eye);
	out.m[1] = u.x;     out.m[5] = u.y;     out.m[9] = u.z;      out.m[13] = -dot(u, eye);
	out.m[2] = -fwd.x;  out.m[6] = -fwd.y;  out.m[10] = -fwd.z;  out.m[14] = dot(fwd, eye);
	out.m[15] = 1.0f;
	return out;
}

// ---- The render API.

struct WorldCamera {
	Float3 eye;
	Float3 target;
	Float3 up{0, 1, 0};
	float fovYRadians = 1.1f;
	float nearZ = 0.1f;
	float farZ = 2000.0f;
};

// One meshed chunk placed in the world (mesh is non-owning; caller keeps it
// alive across the call).
struct ChunkDraw {
	Float3 origin; // world position of the chunk's local (0,0,0)
	const mesh::ChunkMesh *chunkMesh = nullptr;
};

// UNORM8-exact sky clear color (96, 144, 216, 255).
inline constexpr float kWorldViewClearF[4] = {96.0f / 255.0f, 144.0f / 255.0f,
		216.0f / 255.0f, 1.0f};
inline constexpr std::uint8_t kWorldViewClear[4] = {96, 144, 216, 255};

struct WorldViewResult {
	bool ran = false;
	std::string failureReason;
	std::uint32_t width = 0, height = 0;
	// RGBA8, LINEAR (UNORM target, no sRGB), straight alpha (= 255
	// everywhere; the shader writes opaque), tightly packed row-major:
	// pixel (x, y) at (y * width + x) * 4, byte order R,G,B,A.
	std::vector<std::uint8_t> pixels;
	std::uint64_t drawnQuads = 0;
};

// Render into a caller-owned color attachment view (its image is
// transitioned UNDEFINED -> finalLayout by the render pass; the view alone
// identifies the attachment). The depth buffer is created internally per
// call. Returns false with `error` set on any failure. Requires
// report().meshPipelineReady OR presentReady (either implies the graphics
// queue; the classic vertex path needs no mesh-shader feature — gating is
// on the graphics queue itself).
bool renderWorldViewInto(Device &dev, std::span<const ChunkDraw> draws,
		const WorldCamera &camera, VkImageView targetView, VkFormat targetFormat,
		std::uint32_t width, std::uint32_t height, VkImageLayout finalLayout,
		std::uint64_t &drawnQuads, std::string &error);

// Offscreen + readback wrapper — the testable path.
WorldViewResult renderWorldView(Device &dev, std::span<const ChunkDraw> draws,
		const WorldCamera &camera, std::uint32_t width, std::uint32_t height);

} // namespace vk
