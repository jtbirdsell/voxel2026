// Vulkan bring-up tests. CI-graceful by design: hosts without a Vulkan
// runtime or physical device SKIP (a documented, visible outcome — not a
// silent pass and not a failure). The real hardware verdicts come from
// developer machines / future GPU runners, per the Part VII golden-image
// policy precedent.

#include <catch2/catch_test_macros.hpp>

#include "vk/compute.hpp"
#include "vk/device.hpp"

TEST_CASE("Vulkan device bring-up reports coherently or skips without a driver")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	const vk::CapabilityReport &r = dev.report();
	REQUIRE_FALSE(r.deviceName.empty());
	REQUIRE(r.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0));
	REQUIRE(r.computeQueueFamily != ~0u);
	// Ray query implies acceleration structures on every conforming driver.
	if (r.rayQuery)
		REQUIRE(r.accelerationStructure);
}

TEST_CASE("First compute dispatch: GPU output bit-equals the CPU mirror")
{
	vk::Device dev;
	if (!dev.available())
		SKIP("Vulkan unavailable on this host: " << dev.report().failureReason);

	const vk::ParityResult p = vk::runParityDispatch(dev);
	CAPTURE(p.failureReason);
	REQUIRE(p.ran);
	REQUIRE(p.match);
	if (dev.report().timestampPeriodNs > 0.0f) {
		// Timestamps must be present and sane (forward, sub-second for 4 MiB).
		REQUIRE(p.gpuMillis >= 0.0);
		REQUIRE(p.gpuMillis < 1000.0);
	}
}

TEST_CASE("parityMix CPU reference is the documented constant mix")
{
	REQUIRE(vk::parityMix(0u) == 0u);
	REQUIRE(vk::parityMix(1u) == ((0x9E3779B9u) ^ 0u));
	REQUIRE(vk::parityMix(0xFFFFFFFFu) ==
			((0xFFFFFFFFu * 0x9E3779B9u) ^ (0xFFFFFFFFu >> 13)));
}
