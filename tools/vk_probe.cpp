// Capability probe + first-dispatch report for the Vulkan bring-up.
// Run on a developer machine with a GPU; on driverless hosts it prints the
// failure reason and exits 0 (probing "unavailable" is a valid answer).

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <span>

#include "vk/compute.hpp"
#include "vk/device.hpp"

namespace {

const char *yn(bool v)
{
	return v ? "yes" : "no";
}

} // namespace

int main()
{
	vk::Device dev;
	const vk::CapabilityReport &r = dev.report();

	if (!r.available) {
		std::printf("Vulkan unavailable: %s\n", r.failureReason.c_str());
		return 0;
	}

	std::printf("device:                 %s%s\n", r.deviceName.c_str(),
			r.discreteGpu ? " (discrete)" : "");
	std::printf("apiVersion:             %u.%u.%u\n", VK_API_VERSION_MAJOR(r.apiVersion),
			VK_API_VERSION_MINOR(r.apiVersion), VK_API_VERSION_PATCH(r.apiVersion));
	std::printf("driverVersion (raw):    0x%08X\n", r.driverVersion);
	std::printf("compute queue family:   %u\n", r.computeQueueFamily);
	std::printf("timestampPeriod (ns):   %.3f\n",
			static_cast<double>(r.timestampPeriodNs));
	std::puts("--- architecture tier flags (docs/architecture.md III.1) ---");
	std::printf("descriptor_indexing:    %s\n", yn(r.descriptorIndexing));
	std::printf("descriptor_buffer:      %s\n", yn(r.descriptorBuffer));
	std::printf("descriptor_heap:        %s\n", yn(r.descriptorHeap));
	std::printf("mesh_shader:            %s\n", yn(r.meshShader));
	std::printf("shader_object:          %s\n", yn(r.shaderObject));
	std::printf("ray_query:              %s\n", yn(r.rayQuery));
	std::printf("acceleration_structure: %s\n", yn(r.accelerationStructure));
	std::printf("timeline_semaphore:     %s\n", yn(r.timelineSemaphore));

	const struct {
		const char *name;
		std::span<const std::uint32_t> spirv;
	} kernels[] = {
		{"glslang (parity.comp) ", vk::parityKernelGlslang()},
		{"slang   (parity.slang)", vk::parityKernelSlang()},
	};

	bool allMatch = true;
	for (const auto &k : kernels) {
		const vk::ParityResult p = vk::runParityDispatch(dev, k.spirv);
		std::printf("--- compute dispatch: %s ---\n", k.name);
		if (!p.ran) {
			std::printf("dispatch FAILED: %s\n", p.failureReason.c_str());
			return 1;
		}
		std::printf("elements:               %" PRIu64 "\n", p.elements);
		std::printf("GPU/CPU parity:         %s\n", p.match ? "MATCH" : "MISMATCH");
		if (p.gpuMillis >= 0.0)
			std::printf("GPU time:               %.4f ms\n", p.gpuMillis);
		allMatch = allMatch && p.match;
	}
	return allMatch ? 0 : 1;
}
