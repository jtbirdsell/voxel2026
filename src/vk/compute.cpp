#include "vk/compute.hpp"

#include <cstring>

namespace vk {

// Generated at configure time from the committed .spv blobs
// (see src/vk/CMakeLists.txt); the .comp/.slang sources are the artifacts of
// record.
extern const std::uint32_t kParitySpirv[];
extern const std::size_t kParitySpirvWords;
extern const std::uint32_t kParitySlangSpirv[];
extern const std::size_t kParitySlangSpirvWords;

std::span<const std::uint32_t> parityKernelGlslang()
{
	return {kParitySpirv, kParitySpirvWords};
}

std::span<const std::uint32_t> parityKernelSlang()
{
	return {kParitySlangSpirv, kParitySlangSpirvWords};
}

namespace {

constexpr std::uint64_t kElements = 1u << 20; // multiple of local_size_x = 64
constexpr VkDeviceSize kBufferBytes = kElements * sizeof(std::uint32_t);

// Device-level dispatch table for exactly what this path needs.
struct Fn {
#define VK_FN(name) PFN_##name name = nullptr
	VK_FN(vkCreateBuffer);
	VK_FN(vkGetBufferMemoryRequirements);
	VK_FN(vkAllocateMemory);
	VK_FN(vkBindBufferMemory);
	VK_FN(vkMapMemory);
	VK_FN(vkUnmapMemory);
	VK_FN(vkCreateShaderModule);
	VK_FN(vkCreateDescriptorSetLayout);
	VK_FN(vkCreatePipelineLayout);
	VK_FN(vkCreateComputePipelines);
	VK_FN(vkCreateDescriptorPool);
	VK_FN(vkAllocateDescriptorSets);
	VK_FN(vkUpdateDescriptorSets);
	VK_FN(vkCreateCommandPool);
	VK_FN(vkAllocateCommandBuffers);
	VK_FN(vkBeginCommandBuffer);
	VK_FN(vkEndCommandBuffer);
	VK_FN(vkCmdBindPipeline);
	VK_FN(vkCmdBindDescriptorSets);
	VK_FN(vkCmdDispatch);
	VK_FN(vkCmdPipelineBarrier);
	VK_FN(vkCreateQueryPool);
	VK_FN(vkCmdResetQueryPool);
	VK_FN(vkCmdWriteTimestamp);
	VK_FN(vkGetQueryPoolResults);
	VK_FN(vkQueueSubmit);
	VK_FN(vkCreateFence);
	VK_FN(vkWaitForFences);
	VK_FN(vkDestroyFence);
	VK_FN(vkDestroyQueryPool);
	VK_FN(vkDestroyCommandPool);
	VK_FN(vkDestroyDescriptorPool);
	VK_FN(vkDestroyPipeline);
	VK_FN(vkDestroyPipelineLayout);
	VK_FN(vkDestroyDescriptorSetLayout);
	VK_FN(vkDestroyShaderModule);
	VK_FN(vkDestroyBuffer);
	VK_FN(vkFreeMemory);
#undef VK_FN

	bool load(Device &dev)
	{
		bool ok = true;
#define VK_LOAD(field) \
	field = reinterpret_cast<PFN_##field>(dev.deviceProc(#field)); \
	ok = ok && field != nullptr
		VK_LOAD(vkCreateBuffer);
		VK_LOAD(vkGetBufferMemoryRequirements);
		VK_LOAD(vkAllocateMemory);
		VK_LOAD(vkBindBufferMemory);
		VK_LOAD(vkMapMemory);
		VK_LOAD(vkUnmapMemory);
		VK_LOAD(vkCreateShaderModule);
		VK_LOAD(vkCreateDescriptorSetLayout);
		VK_LOAD(vkCreatePipelineLayout);
		VK_LOAD(vkCreateComputePipelines);
		VK_LOAD(vkCreateDescriptorPool);
		VK_LOAD(vkAllocateDescriptorSets);
		VK_LOAD(vkUpdateDescriptorSets);
		VK_LOAD(vkCreateCommandPool);
		VK_LOAD(vkAllocateCommandBuffers);
		VK_LOAD(vkBeginCommandBuffer);
		VK_LOAD(vkEndCommandBuffer);
		VK_LOAD(vkCmdBindPipeline);
		VK_LOAD(vkCmdBindDescriptorSets);
		VK_LOAD(vkCmdDispatch);
		VK_LOAD(vkCmdPipelineBarrier);
		VK_LOAD(vkCreateQueryPool);
		VK_LOAD(vkCmdResetQueryPool);
		VK_LOAD(vkCmdWriteTimestamp);
		VK_LOAD(vkGetQueryPoolResults);
		VK_LOAD(vkQueueSubmit);
		VK_LOAD(vkCreateFence);
		VK_LOAD(vkWaitForFences);
		VK_LOAD(vkDestroyFence);
		VK_LOAD(vkDestroyQueryPool);
		VK_LOAD(vkDestroyCommandPool);
		VK_LOAD(vkDestroyDescriptorPool);
		VK_LOAD(vkDestroyPipeline);
		VK_LOAD(vkDestroyPipelineLayout);
		VK_LOAD(vkDestroyDescriptorSetLayout);
		VK_LOAD(vkDestroyShaderModule);
		VK_LOAD(vkDestroyBuffer);
		VK_LOAD(vkFreeMemory);
#undef VK_LOAD
		return ok;
	}
};

// Every owned handle in one place with one teardown order, so EVERY failure
// path (review blocker: several creates were unchecked and could feed
// VK_NULL_HANDLE into later calls) exits through the same clean ladder.
struct Resources {
	const Fn &fn;
	VkDevice d;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	bool mapped = false;
	VkShaderModule shader = VK_NULL_HANDLE;
	VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	VkQueryPool queryPool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~Resources()
	{
		if (mapped)
			fn.vkUnmapMemory(d, memory);
		if (fence != VK_NULL_HANDLE)
			fn.vkDestroyFence(d, fence, nullptr);
		if (queryPool != VK_NULL_HANDLE)
			fn.vkDestroyQueryPool(d, queryPool, nullptr);
		if (cmdPool != VK_NULL_HANDLE)
			fn.vkDestroyCommandPool(d, cmdPool, nullptr);
		if (pool != VK_NULL_HANDLE)
			fn.vkDestroyDescriptorPool(d, pool, nullptr);
		if (pipeline != VK_NULL_HANDLE)
			fn.vkDestroyPipeline(d, pipeline, nullptr);
		if (layout != VK_NULL_HANDLE)
			fn.vkDestroyPipelineLayout(d, layout, nullptr);
		if (setLayout != VK_NULL_HANDLE)
			fn.vkDestroyDescriptorSetLayout(d, setLayout, nullptr);
		if (shader != VK_NULL_HANDLE)
			fn.vkDestroyShaderModule(d, shader, nullptr);
		if (buffer != VK_NULL_HANDLE)
			fn.vkDestroyBuffer(d, buffer, nullptr);
		if (memory != VK_NULL_HANDLE)
			fn.vkFreeMemory(d, memory, nullptr);
	}
};

std::uint32_t findHostVisibleMemoryType(const VkPhysicalDeviceMemoryProperties &props,
		std::uint32_t typeBits)
{
	const VkMemoryPropertyFlags wanted =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
		if ((typeBits & (1u << i)) &&
				(props.memoryTypes[i].propertyFlags & wanted) == wanted)
			return i;
	return ~0u;
}

} // namespace

ParityResult runParityDispatch(Device &dev)
{
	return runParityDispatch(dev, parityKernelGlslang());
}

ParityResult runParityDispatch(Device &dev, std::span<const std::uint32_t> spirv)
{
	ParityResult result;
	result.elements = kElements;
	if (!dev.available()) {
		result.failureReason = "device unavailable: " + dev.report().failureReason;
		return result;
	}

	Fn fn;
	if (!fn.load(dev)) {
		result.failureReason = "device-level function resolution failed";
		return result;
	}
	auto getMemProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
			dev.instanceLevelProc("vkGetPhysicalDeviceMemoryProperties"));
	if (!getMemProps) {
		result.failureReason = "vkGetPhysicalDeviceMemoryProperties not resolvable";
		return result;
	}

	Resources r{fn, dev.device()};
	const VkDevice d = r.d;
	const auto fail = [&result](const char *why) {
		result.failureReason = why;
		return result;
	};

	// ---- Buffer + memory (host-visible|coherent: no flushes needed; the
	// implicit host->device domain operation at vkQueueSubmit covers the
	// pre-submit host writes, and the COMPUTE->HOST barrier + fence wait
	// makes the GPU writes host-visible for the readback).
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = kBufferBytes;
	bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (fn.vkCreateBuffer(d, &bci, nullptr, &r.buffer) != VK_SUCCESS)
		return fail("vkCreateBuffer failed");

	VkMemoryRequirements req{};
	fn.vkGetBufferMemoryRequirements(d, r.buffer, &req);
	VkPhysicalDeviceMemoryProperties memProps{};
	getMemProps(dev.physicalDevice(), &memProps);
	const std::uint32_t memType = findHostVisibleMemoryType(memProps, req.memoryTypeBits);
	if (memType == ~0u)
		return fail("no host-visible coherent memory type");
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = memType;
	if (fn.vkAllocateMemory(d, &mai, nullptr, &r.memory) != VK_SUCCESS)
		return fail("vkAllocateMemory failed");
	if (fn.vkBindBufferMemory(d, r.buffer, r.memory, 0) != VK_SUCCESS)
		return fail("vkBindBufferMemory failed");

	void *mapped = nullptr;
	if (fn.vkMapMemory(d, r.memory, 0, kBufferBytes, 0, &mapped) != VK_SUCCESS)
		return fail("vkMapMemory failed");
	r.mapped = true;

	std::vector<std::uint32_t> expected(kElements);
	{
		auto *p = static_cast<std::uint32_t *>(mapped);
		for (std::uint64_t i = 0; i < kElements; ++i) {
			const auto v = static_cast<std::uint32_t>(i * 2654435761u + 0x563B);
			p[i] = v;
			expected[i] = parityMix(v);
		}
	}

	// ---- Pipeline.
	VkShaderModuleCreateInfo smci{};
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = spirv.size() * sizeof(std::uint32_t);
	smci.pCode = spirv.data();
	if (fn.vkCreateShaderModule(d, &smci, nullptr, &r.shader) != VK_SUCCESS)
		return fail("vkCreateShaderModule rejected the embedded SPIR-V");

	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo dslci{};
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = 1;
	dslci.pBindings = &binding;
	if (fn.vkCreateDescriptorSetLayout(d, &dslci, nullptr, &r.setLayout) != VK_SUCCESS)
		return fail("vkCreateDescriptorSetLayout failed");

	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &r.setLayout;
	if (fn.vkCreatePipelineLayout(d, &plci, nullptr, &r.layout) != VK_SUCCESS)
		return fail("vkCreatePipelineLayout failed");

	VkComputePipelineCreateInfo cpci{};
	cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	cpci.stage.module = r.shader;
	cpci.stage.pName = "main";
	cpci.layout = r.layout;
	if (fn.vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &cpci, nullptr, &r.pipeline) !=
			VK_SUCCESS)
		return fail("vkCreateComputePipelines failed");

	// ---- Descriptors.
	VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets = 1;
	dpci.poolSizeCount = 1;
	dpci.pPoolSizes = &poolSize;
	if (fn.vkCreateDescriptorPool(d, &dpci, nullptr, &r.pool) != VK_SUCCESS)
		return fail("vkCreateDescriptorPool failed");

	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool = r.pool;
	dsai.descriptorSetCount = 1;
	dsai.pSetLayouts = &r.setLayout;
	VkDescriptorSet set = VK_NULL_HANDLE;
	if (fn.vkAllocateDescriptorSets(d, &dsai, &set) != VK_SUCCESS)
		return fail("vkAllocateDescriptorSets failed");

	VkDescriptorBufferInfo dbi{r.buffer, 0, kBufferBytes};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.pBufferInfo = &dbi;
	fn.vkUpdateDescriptorSets(d, 1, &write, 0, nullptr);

	// ---- Commands: timestamps bracket the whole submission (TOP/BOTTOM of
	// pipe) — a smoke measurement of the submission, deliberately not a
	// shader-only benchmark.
	VkCommandPoolCreateInfo cmdPoolCi{};
	cmdPoolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolCi.queueFamilyIndex = dev.computeQueueFamily();
	if (fn.vkCreateCommandPool(d, &cmdPoolCi, nullptr, &r.cmdPool) != VK_SUCCESS)
		return fail("vkCreateCommandPool failed");

	VkCommandBufferAllocateInfo cbai{};
	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandPool = r.cmdPool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if (fn.vkAllocateCommandBuffers(d, &cbai, &cmd) != VK_SUCCESS)
		return fail("vkAllocateCommandBuffers failed");

	const bool timestamps = dev.report().timestampPeriodNs > 0.0f;
	if (timestamps) {
		VkQueryPoolCreateInfo qpci{};
		qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
		qpci.queryCount = 2;
		if (fn.vkCreateQueryPool(d, &qpci, nullptr, &r.queryPool) != VK_SUCCESS)
			return fail("vkCreateQueryPool failed");
	}

	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (fn.vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
		return fail("vkBeginCommandBuffer failed");
	if (timestamps) {
		fn.vkCmdResetQueryPool(cmd, r.queryPool, 0, 2);
		fn.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, r.queryPool, 0);
	}
	fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeline);
	fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.layout, 0, 1, &set, 0,
			nullptr);
	fn.vkCmdDispatch(cmd, static_cast<std::uint32_t>(kElements / 64), 1, 1);
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
	if (timestamps)
		fn.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, r.queryPool, 1);
	if (fn.vkEndCommandBuffer(cmd) != VK_SUCCESS)
		return fail("vkEndCommandBuffer failed");

	// ---- Submit + wait (checked: a device-lost here must be reported as a
	// non-executing device, never mistaken for a parity mismatch).
	VkFenceCreateInfo fci{};
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (fn.vkCreateFence(d, &fci, nullptr, &r.fence) != VK_SUCCESS)
		return fail("vkCreateFence failed");
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	if (fn.vkQueueSubmit(dev.computeQueue(), 1, &submit, r.fence) != VK_SUCCESS)
		return fail("vkQueueSubmit failed");
	if (fn.vkWaitForFences(d, 1, &r.fence, VK_TRUE, ~0ull) != VK_SUCCESS)
		return fail("vkWaitForFences failed (device lost?)");

	// ---- Timing + parity readback. `ran` is true only past a clean wait.
	if (timestamps) {
		std::uint64_t ticks[2] = {};
		if (fn.vkGetQueryPoolResults(d, r.queryPool, 0, 2, sizeof(ticks), ticks,
					sizeof(std::uint64_t),
					VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS &&
				ticks[1] >= ticks[0]) {
			result.gpuMillis = static_cast<double>(ticks[1] - ticks[0]) *
					static_cast<double>(dev.report().timestampPeriodNs) * 1e-6;
		}
	}

	result.ran = true;
	result.match = std::memcmp(mapped, expected.data(), kBufferBytes) == 0;
	if (!result.match)
		result.failureReason = "GPU output does not match CPU mirror";
	return result; // Resources dtor tears everything down
}

} // namespace vk
