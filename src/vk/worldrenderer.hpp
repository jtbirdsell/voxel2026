// Persistent world renderer (issue #22) — the perf pass over issue #21's
// one-shot path. Everything expensive is created ONCE per (color format,
// extent): render pass, pipeline, depth target, command slots, timestamp
// queries; geometry uploads once to DEVICE-LOCAL memory through a staging
// transfer; frames record into reused command buffers and can pace through
// caller-provided semaphores (the swapchain loop) or block internally (the
// offscreen/test path).
//
// The load-bearing guarantee: renderOffscreen() must produce pixels
// BIT-IDENTICAL to issue #21's validated one-shot renderWorldView() for
// the same scene and camera — the equivalence gate that keeps "faster"
// from quietly becoming "different" (pinned by test).
//
// GPU time is measured with timestamp queries (masked to the family's
// timestampValidBits, the bench.cpp discipline); lastGpuMillis() reports
// the most recently COMPLETED frame's GPU span — application time and GPU
// time are different numbers and are reported as such.

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "vk/worldview.hpp"

namespace vk {

class WorldRenderer {
public:
	// Requires a graphics-tier device (meshPipelineReady or presentReady).
	// Check ok() before use; failure detail in error().
	WorldRenderer(Device &dev, VkFormat colorFormat, std::uint32_t width,
			std::uint32_t height);
	~WorldRenderer();

	WorldRenderer(const WorldRenderer &) = delete;
	WorldRenderer &operator=(const WorldRenderer &) = delete;

	bool ok() const;
	const std::string &error() const;

	// Replace the resident scene: merged device-local vertex/index buffers
	// built via a blocking staging transfer. Draw list metadata is copied;
	// the source meshes are not referenced afterwards.
	bool uploadScene(std::span<const ChunkDraw> draws);

	// Submit one frame into `target` (same format/extent as construction).
	// waitSemaphore (with waitStage) and signalSemaphore are passed through
	// to the submission — the swapchain loop's pacing hooks (either may be
	// null). Command-slot lifetime is internal: slots rotate, and at most
	// kFramesInFlight frames are unfinished at once (the next call blocks
	// on the oldest slot's internal fence before reusing it).
	bool renderFrameAsync(const WorldCamera &camera, VkImageView target,
			VkSemaphore waitSemaphore, VkPipelineStageFlags waitStage,
			VkSemaphore signalSemaphore, std::string &error);

	// The equivalence-gate path: render into an internally owned offscreen
	// image (created lazily, reused) and read the pixels back — everything
	// else (pipeline, geometry, depth) is the persistent fast path.
	WorldViewResult renderOffscreen(const WorldCamera &camera);

	// GPU span of the most recently completed timed frame, in milliseconds;
	// negative until a frame has completed (or timestamps are unsupported).
	double lastGpuMillis() const;

	static constexpr std::uint32_t kFramesInFlight = 2;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace vk
