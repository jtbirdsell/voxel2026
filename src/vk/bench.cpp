#include "vk/bench.hpp"

#include <algorithm>
#include <cstring>

#include "gen/minigen.hpp"

namespace vk {

// Embedded kernels (configure-time, see CMakeLists.txt / spirv_embed.cpp.in).
extern const std::uint32_t kVctInjectSpirv[];
extern const std::size_t kVctInjectSpirvWords;
extern const std::uint32_t kVctMipSpirv[];
extern const std::size_t kVctMipSpirvWords;
extern const std::uint32_t kVctTraceSpirv[];
extern const std::size_t kVctTraceSpirvWords;

namespace {

struct Fn {
#define VK_FN(name) PFN_##name name = nullptr
	VK_FN(vkCreateBuffer);
	VK_FN(vkGetBufferMemoryRequirements);
	VK_FN(vkAllocateMemory);
	VK_FN(vkBindBufferMemory);
	VK_FN(vkMapMemory);
	VK_FN(vkUnmapMemory);
	VK_FN(vkCreateImage);
	VK_FN(vkGetImageMemoryRequirements);
	VK_FN(vkBindImageMemory);
	VK_FN(vkCreateImageView);
	VK_FN(vkCreateSampler);
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
	VK_FN(vkCmdPushConstants);
	VK_FN(vkCmdDispatch);
	VK_FN(vkCmdPipelineBarrier);
	VK_FN(vkCmdCopyBuffer);
	VK_FN(vkCmdCopyImageToBuffer);
	VK_FN(vkCmdFillBuffer);
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
	VK_FN(vkDestroySampler);
	VK_FN(vkDestroyImageView);
	VK_FN(vkDestroyImage);
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
		VK_LOAD(vkCreateImage);
		VK_LOAD(vkGetImageMemoryRequirements);
		VK_LOAD(vkBindImageMemory);
		VK_LOAD(vkCreateImageView);
		VK_LOAD(vkCreateSampler);
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
		VK_LOAD(vkCmdPushConstants);
		VK_LOAD(vkCmdDispatch);
		VK_LOAD(vkCmdPipelineBarrier);
		VK_LOAD(vkCmdCopyBuffer);
		VK_LOAD(vkCmdCopyImageToBuffer);
		VK_LOAD(vkCmdFillBuffer);
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
		VK_LOAD(vkDestroySampler);
		VK_LOAD(vkDestroyImageView);
		VK_LOAD(vkDestroyImage);
		VK_LOAD(vkDestroyBuffer);
		VK_LOAD(vkFreeMemory);
#undef VK_LOAD
		return ok;
	}
};

std::uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties &props,
		std::uint32_t typeBits, VkMemoryPropertyFlags wanted)
{
	for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
		if ((typeBits & (1u << i)) &&
				(props.memoryTypes[i].propertyFlags & wanted) == wanted)
			return i;
	return ~0u;
}

// Every owned handle; single teardown ladder (same pattern as compute.cpp).
struct Resources {
	const Fn &fn;
	VkDevice d;

	VkBuffer staging = VK_NULL_HANDLE;        // host-visible: upload + readback
	VkDeviceMemory stagingMem = VK_NULL_HANDLE;
	bool stagingMapped = false;
	void *stagingPtr = nullptr;
	VkBuffer scene = VK_NULL_HANDLE;          // device-local
	VkDeviceMemory sceneMem = VK_NULL_HANDLE;
	VkBuffer gather = VK_NULL_HANDLE;         // device-local output
	VkDeviceMemory gatherMem = VK_NULL_HANDLE;
	VkBuffer stats = VK_NULL_HANDLE;          // device-local step counter
	VkDeviceMemory statsMem = VK_NULL_HANDLE;
	VkImage radiance = VK_NULL_HANDLE;        // device-local, full mip chain
	VkDeviceMemory radianceMem = VK_NULL_HANDLE;
	// Explicit default initializer: without it, GCC/Clang fire
	// -Wmissing-field-initializers (-Werror) on the two-member aggregate
	// init `Resources r{fn, device}` even though value-init is correct.
	std::vector<VkImageView> levelViews{};    // per-mip storage views
	VkImageView fullView = VK_NULL_HANDLE;    // sampled, all mips
	VkSampler sampler = VK_NULL_HANDLE;
	VkShaderModule modInject = VK_NULL_HANDLE, modMip = VK_NULL_HANDLE,
				   modTrace = VK_NULL_HANDLE;
	VkDescriptorSetLayout dslInject = VK_NULL_HANDLE, dslMip = VK_NULL_HANDLE,
						  dslTrace = VK_NULL_HANDLE;
	VkPipelineLayout plInject = VK_NULL_HANDLE, plMip = VK_NULL_HANDLE,
					 plTrace = VK_NULL_HANDLE;
	VkPipeline pipeInject = VK_NULL_HANDLE, pipeMip = VK_NULL_HANDLE,
			   pipeTrace = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	VkQueryPool queryPool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~Resources()
	{
		if (stagingMapped)
			fn.vkUnmapMemory(d, stagingMem);
		if (fence)
			fn.vkDestroyFence(d, fence, nullptr);
		if (queryPool)
			fn.vkDestroyQueryPool(d, queryPool, nullptr);
		if (cmdPool)
			fn.vkDestroyCommandPool(d, cmdPool, nullptr);
		if (pool)
			fn.vkDestroyDescriptorPool(d, pool, nullptr);
		for (VkPipeline p : {pipeInject, pipeMip, pipeTrace})
			if (p)
				fn.vkDestroyPipeline(d, p, nullptr);
		for (VkPipelineLayout p : {plInject, plMip, plTrace})
			if (p)
				fn.vkDestroyPipelineLayout(d, p, nullptr);
		for (VkDescriptorSetLayout p : {dslInject, dslMip, dslTrace})
			if (p)
				fn.vkDestroyDescriptorSetLayout(d, p, nullptr);
		for (VkShaderModule m : {modInject, modMip, modTrace})
			if (m)
				fn.vkDestroyShaderModule(d, m, nullptr);
		if (sampler)
			fn.vkDestroySampler(d, sampler, nullptr);
		if (fullView)
			fn.vkDestroyImageView(d, fullView, nullptr);
		for (VkImageView v : levelViews)
			if (v)
				fn.vkDestroyImageView(d, v, nullptr);
		if (radiance)
			fn.vkDestroyImage(d, radiance, nullptr);
		if (radianceMem)
			fn.vkFreeMemory(d, radianceMem, nullptr);
		for (VkBuffer b : {staging, scene, gather, stats})
			if (b)
				fn.vkDestroyBuffer(d, b, nullptr);
		for (VkDeviceMemory m : {stagingMem, sceneMem, gatherMem, statsMem})
			if (m)
				fn.vkFreeMemory(d, m, nullptr);
	}

	bool makeBuffer(const VkPhysicalDeviceMemoryProperties &memProps, VkDeviceSize size,
			VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags, VkBuffer &buf,
			VkDeviceMemory &mem)
	{
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = size;
		bci.usage = usage;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (fn.vkCreateBuffer(d, &bci, nullptr, &buf) != VK_SUCCESS)
			return false;
		VkMemoryRequirements req{};
		fn.vkGetBufferMemoryRequirements(d, buf, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits, memFlags);
		if (type == ~0u)
			return false;
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &mem) != VK_SUCCESS)
			return false;
		return fn.vkBindBufferMemory(d, buf, mem, 0) == VK_SUCCESS;
	}
};

bool makeComputePipeline(const Fn &fn, VkDevice d, const std::uint32_t *spirv,
		std::size_t words, std::span<const VkDescriptorSetLayoutBinding> bindings,
		std::uint32_t pushBytes, VkShaderModule &mod, VkDescriptorSetLayout &dsl,
		VkPipelineLayout &pl, VkPipeline &pipe)
{
	VkShaderModuleCreateInfo smci{};
	smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	smci.codeSize = words * sizeof(std::uint32_t);
	smci.pCode = spirv;
	if (fn.vkCreateShaderModule(d, &smci, nullptr, &mod) != VK_SUCCESS)
		return false;

	VkDescriptorSetLayoutCreateInfo dslci{};
	dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslci.bindingCount = static_cast<std::uint32_t>(bindings.size());
	dslci.pBindings = bindings.data();
	if (fn.vkCreateDescriptorSetLayout(d, &dslci, nullptr, &dsl) != VK_SUCCESS)
		return false;

	VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes};
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &dsl;
	plci.pushConstantRangeCount = pushBytes ? 1u : 0u;
	plci.pPushConstantRanges = pushBytes ? &range : nullptr;
	if (fn.vkCreatePipelineLayout(d, &plci, nullptr, &pl) != VK_SUCCESS)
		return false;

	VkComputePipelineCreateInfo cpci{};
	cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	cpci.stage.module = mod;
	cpci.stage.pName = "main";
	cpci.layout = pl;
	return fn.vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipe) ==
			VK_SUCCESS;
}

constexpr VkDescriptorSetLayoutBinding storageBufferBinding(std::uint32_t b)
{
	return {b, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
}
constexpr VkDescriptorSetLayoutBinding storageImageBinding(std::uint32_t b)
{
	return {b, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
}

} // namespace

std::vector<std::uint32_t> fillSceneFromMinigen(std::uint32_t extent)
{
	// Content -> (solid, packed RGB emissive). Emissive is what the GI passes
	// gather; values chosen so the world has sparse bright emitters (flowers,
	// wood) over a faint ambient field (grass/leaves).
	const auto pack = [](bool solid, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
		return (solid ? 0x80000000u : 0u) | (r << 16) | (g << 8) | b;
	};
	std::vector<std::uint32_t> palette(16, 0);
	palette[gen::kAir] = pack(false, 0, 0, 0);
	palette[gen::kStone] = pack(true, 0, 0, 0);
	palette[gen::kDirt] = pack(true, 0, 0, 0);
	palette[gen::kGrass] = pack(true, 8, 24, 8);
	palette[gen::kWood] = pack(true, 120, 60, 20);
	palette[gen::kLeaves] = pack(true, 10, 40, 10);
	palette[gen::kFlower] = pack(true, 200, 160, 40);

	const std::uint32_t chunks = extent / static_cast<std::uint32_t>(gen::kGenChunk);
	std::vector<std::uint32_t> scene(
			static_cast<std::size_t>(extent) * extent * extent, 0);

	gen::GenChunk chunk;
	for (std::uint32_t cz = 0; cz < chunks; ++cz)
		for (std::uint32_t cy = 0; cy < chunks; ++cy)
			for (std::uint32_t cx = 0; cx < chunks; ++cx) {
				gen::generateChunk(gen::kStandardSeed,
						{static_cast<int>(cx), static_cast<int>(cy),
								static_cast<int>(cz)},
						chunk);
				for (int z = 0; z < gen::kGenChunk; ++z)
					for (int y = 0; y < gen::kGenChunk; ++y)
						for (int x = 0; x < gen::kGenChunk; ++x) {
							const gen::Content c = chunk.at(x, y, z);
							const std::uint32_t wx = cx * 16 + static_cast<std::uint32_t>(x);
							const std::uint32_t wy = cy * 16 + static_cast<std::uint32_t>(y);
							const std::uint32_t wz = cz * 16 + static_cast<std::uint32_t>(z);
							scene[wx + static_cast<std::size_t>(extent) *
									(wy + static_cast<std::size_t>(extent) * wz)] =
									palette[c < 16 ? c : 0];
						}
			}
	return scene;
}

VctBenchResult runVctBench(Device &dev, const VctBenchParams &params)
{
	VctBenchResult result;
	result.extent = params.extent;
	result.conesPerTrace = 6;
	result.traceCount = params.traceWidth * params.traceHeight;
	if (!dev.available()) {
		result.failureReason = "device unavailable: " + dev.report().failureReason;
		return result;
	}
	if (params.scene.size() !=
			static_cast<std::size_t>(params.extent) * params.extent * params.extent) {
		result.failureReason = "scene size != extent^3";
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
	VkPhysicalDeviceMemoryProperties memProps{};
	getMemProps(dev.physicalDevice(), &memProps);

	Resources r{fn, dev.device()};
	const VkDevice d = r.d;
	const auto fail = [&result](const char *why) {
		result.failureReason = why;
		return result;
	};

	std::uint32_t mips = 1;
	for (std::uint32_t e = params.extent; e > 1; e >>= 1)
		++mips;
	result.mipLevels = mips;

	// ---- Buffers.
	const VkDeviceSize sceneBytes = params.scene.size() * sizeof(std::uint32_t);
	const VkDeviceSize gatherBytes =
			static_cast<VkDeviceSize>(result.traceCount) * 4 * sizeof(float);
	const VkDeviceSize stagingBytes = sceneBytes > 4096 ? sceneBytes : 4096;
	if (!r.makeBuffer(memProps, stagingBytes,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				r.staging, r.stagingMem))
		return fail("staging buffer creation failed");
	if (fn.vkMapMemory(d, r.stagingMem, 0, stagingBytes, 0, &r.stagingPtr) != VK_SUCCESS)
		return fail("staging map failed");
	r.stagingMapped = true;
	if (!r.makeBuffer(memProps, sceneBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, r.scene, r.sceneMem))
		return fail("scene buffer creation failed");
	if (!r.makeBuffer(memProps, gatherBytes,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, r.gather, r.gatherMem))
		return fail("gather buffer creation failed");
	if (!r.makeBuffer(memProps, 4,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
						VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, r.stats, r.statsMem))
		return fail("stats buffer creation failed");

	std::memcpy(r.stagingPtr, params.scene.data(), sceneBytes);

	// ---- Radiance image (RGBA8 cube, full mip chain, storage+sampled).
	VkImageCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_3D;
	ici.format = VK_FORMAT_R8G8B8A8_UNORM;
	ici.extent = {params.extent, params.extent, params.extent};
	ici.mipLevels = mips;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (fn.vkCreateImage(d, &ici, nullptr, &r.radiance) != VK_SUCCESS)
		return fail("radiance image creation failed");
	VkMemoryRequirements ireq{};
	fn.vkGetImageMemoryRequirements(d, r.radiance, &ireq);
	const std::uint32_t itype =
			findMemoryType(memProps, ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (itype == ~0u)
		return fail("no device-local memory type for image");
	VkMemoryAllocateInfo imai{};
	imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imai.allocationSize = ireq.size;
	imai.memoryTypeIndex = itype;
	if (fn.vkAllocateMemory(d, &imai, nullptr, &r.radianceMem) != VK_SUCCESS)
		return fail("radiance memory allocation failed");
	if (fn.vkBindImageMemory(d, r.radiance, r.radianceMem, 0) != VK_SUCCESS)
		return fail("radiance memory bind failed");

	r.levelViews.resize(mips, VK_NULL_HANDLE);
	for (std::uint32_t i = 0; i < mips; ++i) {
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = r.radiance;
		vci.viewType = VK_IMAGE_VIEW_TYPE_3D;
		vci.format = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};
		if (fn.vkCreateImageView(d, &vci, nullptr, &r.levelViews[i]) != VK_SUCCESS)
			return fail("level view creation failed");
	}
	{
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = r.radiance;
		vci.viewType = VK_IMAGE_VIEW_TYPE_3D;
		vci.format = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
		if (fn.vkCreateImageView(d, &vci, nullptr, &r.fullView) != VK_SUCCESS)
			return fail("full view creation failed");
	}
	{
		VkSamplerCreateInfo sci{};
		sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		sci.magFilter = VK_FILTER_LINEAR;
		sci.minFilter = VK_FILTER_LINEAR;
		sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		// REPEAT (review blocker fix): cones march through the infinitely
		// tiled scene so the step budget is genuinely executed; with CLAMP +
		// a bounds break the original kernel ran ~8 of 64 nominal steps.
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sci.maxLod = VK_LOD_CLAMP_NONE;
		if (fn.vkCreateSampler(d, &sci, nullptr, &r.sampler) != VK_SUCCESS)
			return fail("sampler creation failed");
	}

	// ---- Pipelines.
	{
		const VkDescriptorSetLayoutBinding injectB[] = {storageBufferBinding(0),
				storageImageBinding(1)};
		if (!makeComputePipeline(fn, d, kVctInjectSpirv, kVctInjectSpirvWords, injectB, 4,
					r.modInject, r.dslInject, r.plInject, r.pipeInject))
			return fail("inject pipeline creation failed");
		const VkDescriptorSetLayoutBinding mipB[] = {storageImageBinding(0),
				storageImageBinding(1)};
		if (!makeComputePipeline(fn, d, kVctMipSpirv, kVctMipSpirvWords, mipB, 4, r.modMip,
					r.dslMip, r.plMip, r.pipeMip))
			return fail("mip pipeline creation failed");
		const VkDescriptorSetLayoutBinding traceB[] = {
				{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
				{1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
				storageBufferBinding(2), storageBufferBinding(3)};
		if (!makeComputePipeline(fn, d, kVctTraceSpirv, kVctTraceSpirvWords, traceB, 24,
					r.modTrace, r.dslTrace, r.plTrace, r.pipeTrace))
			return fail("trace pipeline creation failed");
	}

	// ---- Descriptor sets: 1 inject + (mips-1) mip pairs + 1 trace.
	{
		const VkDescriptorPoolSize sizes[] = {
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
				{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 + 2 * (mips - 1)},
				{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
				{VK_DESCRIPTOR_TYPE_SAMPLER, 1},
		};
		VkDescriptorPoolCreateInfo dpci{};
		dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		dpci.maxSets = 1 + (mips - 1) + 1;
		dpci.poolSizeCount = 4;
		dpci.pPoolSizes = sizes;
		if (fn.vkCreateDescriptorPool(d, &dpci, nullptr, &r.pool) != VK_SUCCESS)
			return fail("descriptor pool creation failed");
	}

	const auto allocSet = [&](VkDescriptorSetLayout layout, VkDescriptorSet &set) {
		VkDescriptorSetAllocateInfo dsai{};
		dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		dsai.descriptorPool = r.pool;
		dsai.descriptorSetCount = 1;
		dsai.pSetLayouts = &layout;
		return fn.vkAllocateDescriptorSets(d, &dsai, &set) == VK_SUCCESS;
	};
	const auto writeBuffer = [&](VkDescriptorSet set, std::uint32_t binding, VkBuffer buf,
								 VkDeviceSize bytes) {
		VkDescriptorBufferInfo info{buf, 0, bytes};
		VkWriteDescriptorSet w{};
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = set;
		w.dstBinding = binding;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		w.pBufferInfo = &info;
		fn.vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
	};
	const auto writeImage = [&](VkDescriptorSet set, std::uint32_t binding,
								VkDescriptorType type, VkImageView view, VkSampler smp) {
		VkDescriptorImageInfo info{smp, view, VK_IMAGE_LAYOUT_GENERAL};
		VkWriteDescriptorSet w{};
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = set;
		w.dstBinding = binding;
		w.descriptorCount = 1;
		w.descriptorType = type;
		w.pImageInfo = &info;
		fn.vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
	};

	VkDescriptorSet setInject = VK_NULL_HANDLE, setTrace = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> setMips(mips - 1, VK_NULL_HANDLE);
	if (!allocSet(r.dslInject, setInject) || !allocSet(r.dslTrace, setTrace))
		return fail("descriptor set allocation failed");
	for (std::uint32_t i = 0; i + 1 < mips; ++i)
		if (!allocSet(r.dslMip, setMips[i]))
			return fail("mip descriptor set allocation failed");

	writeBuffer(setInject, 0, r.scene, sceneBytes);
	writeImage(setInject, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, r.levelViews[0],
			VK_NULL_HANDLE);
	for (std::uint32_t i = 0; i + 1 < mips; ++i) {
		writeImage(setMips[i], 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, r.levelViews[i],
				VK_NULL_HANDLE);
		writeImage(setMips[i], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, r.levelViews[i + 1],
				VK_NULL_HANDLE);
	}
	writeImage(setTrace, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, r.fullView, VK_NULL_HANDLE);
	writeImage(setTrace, 1, VK_DESCRIPTOR_TYPE_SAMPLER, VK_NULL_HANDLE, r.sampler);
	writeBuffer(setTrace, 2, r.gather, gatherBytes);
	writeBuffer(setTrace, 3, r.stats, 4);

	// ---- Commands.
	VkCommandPoolCreateInfo cpci{};
	cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cpci.queueFamilyIndex = dev.computeQueueFamily();
	if (fn.vkCreateCommandPool(d, &cpci, nullptr, &r.cmdPool) != VK_SUCCESS)
		return fail("command pool creation failed");
	VkCommandBufferAllocateInfo cbai{};
	cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cbai.commandPool = r.cmdPool;
	cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cbai.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	if (fn.vkAllocateCommandBuffers(d, &cbai, &cmd) != VK_SUCCESS)
		return fail("command buffer allocation failed");

	const std::uint32_t passCount = 1 /*inject*/ + (mips - 1) /*mip*/ + 1 /*trace*/;
	// iterations + 1: iteration 0 is an untimed-in-spirit warmup whose stamps
	// are recorded but EXCLUDED from the medians (review finding: single cold
	// submissions carried first-touch bias).
	const std::uint32_t reps = params.iterations + 1;
	const bool timestamps = dev.report().timestampPeriodNs > 0.0f;
	if (timestamps) {
		VkQueryPoolCreateInfo qpci{};
		qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
		qpci.queryCount = 2 * passCount * reps;
		if (fn.vkCreateQueryPool(d, &qpci, nullptr, &r.queryPool) != VK_SUCCESS)
			return fail("query pool creation failed");
	}

	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (fn.vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
		return fail("command buffer begin failed");
	if (timestamps)
		fn.vkCmdResetQueryPool(cmd, r.queryPool, 0, 2 * passCount * reps);

	// Upload scene (untimed: one-time setup, not a per-frame cost).
	{
		VkBufferCopy copy{0, 0, sceneBytes};
		fn.vkCmdCopyBuffer(cmd, r.staging, r.scene, 1, &copy);
		VkMemoryBarrier mb{};
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
	}
	// Image UNDEFINED -> GENERAL (all levels), once.
	{
		VkImageMemoryBarrier ib{};
		ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		ib.srcAccessMask = 0;
		ib.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		ib.image = r.radiance;
		ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
		fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
	}

	std::uint32_t query = 0;
	// Timing note (review-verified, recorded so nobody "fixes" a non-bug):
	// the closing stamp at COMPUTE_SHADER stage latches once all prior
	// commands have completed that stage, so it measures dispatch completion
	// by itself; the trailing barrier exists to ISOLATE the next pass, making
	// per-pass deltas non-overlapping. The serialization this imposes means
	// totals are a conservative upper bound a real frame graph can partially
	// overlap away.
	const auto stampedDispatch = [&](VkPipeline pipe, VkPipelineLayout layout,
									 VkDescriptorSet set, const void *push,
									 std::uint32_t pushBytes, std::uint32_t gx,
									 std::uint32_t gy, std::uint32_t gz) {
		if (timestamps)
			fn.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, r.queryPool,
					query++);
		fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
		fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set,
				0, nullptr);
		if (pushBytes)
			fn.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes,
					push);
		fn.vkCmdDispatch(cmd, gx, gy, gz);
		VkMemoryBarrier mb{};
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
		if (timestamps)
			fn.vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, r.queryPool,
					query++);
	};

	struct TracePush {
		std::uint32_t w, h, steps, extent;
		float originY;
		std::uint32_t flags;
	};
	const float originY =
			static_cast<float>(params.originVoxelY) / static_cast<float>(params.extent);
	const std::uint32_t earlyOutFlag = params.occlusionEarlyOut ? 1u : 0u;

	const auto recordSequence = [&](std::uint32_t traceFlags) {
		{
			const std::uint32_t push = params.extent;
			const std::uint32_t groups = (params.extent + 3) / 4;
			stampedDispatch(r.pipeInject, r.plInject, setInject, &push, 4, groups, groups,
					groups);
		}
		for (std::uint32_t i = 0; i + 1 < mips; ++i) {
			const std::uint32_t dstExtent = params.extent >> (i + 1);
			const std::uint32_t push = dstExtent ? dstExtent : 1;
			const std::uint32_t groups = (push + 3) / 4;
			stampedDispatch(r.pipeMip, r.plMip, setMips[i], &push, 4, groups, groups,
					groups);
		}
		const TracePush push{params.traceWidth, params.traceHeight, params.coneSteps,
				params.extent, originY, traceFlags};
		stampedDispatch(r.pipeTrace, r.plTrace, setTrace, &push, sizeof(TracePush),
				(params.traceWidth + 7) / 8, (params.traceHeight + 7) / 8, 1);
	};

	result.passes.push_back({"inject", -1.0});
	for (std::uint32_t i = 0; i + 1 < mips; ++i)
		result.passes.push_back({"mip" + std::to_string(i + 1), -1.0});
	result.passes.push_back({"coneGather", -1.0});
	result.iterations = params.iterations;

	for (std::uint32_t rep = 0; rep < reps; ++rep)
		recordSequence(earlyOutFlag);

	// Untimed instrumented gather: clear the step counter, then one gather
	// with stats enabled (the atomic adds distort timing, so this dispatch is
	// outside every timed bracket).
	{
		fn.vkCmdFillBuffer(cmd, r.stats, 0, 4, 0);
		VkMemoryBarrier mb{};
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
		const TracePush push{params.traceWidth, params.traceHeight, params.coneSteps,
				params.extent, originY, earlyOutFlag | 2u};
		fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.pipeTrace);
		fn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r.plTrace, 0, 1,
				&setTrace, 0, nullptr);
		fn.vkCmdPushConstants(cmd, r.plTrace, VK_SHADER_STAGE_COMPUTE_BIT, 0,
				sizeof(TracePush), &push);
		fn.vkCmdDispatch(cmd, (params.traceWidth + 7) / 8, (params.traceHeight + 7) / 8, 1);
		VkMemoryBarrier mb2{};
		mb2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb2.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb2, 0, nullptr, 0, nullptr);
		VkBufferCopy scopy{0, 8, 4};
		fn.vkCmdCopyBuffer(cmd, r.stats, r.staging, 1, &scopy);
	}

	// Optional readbacks: top-mip texel + first gather quads -> staging.
	if (params.readback) {
		VkMemoryBarrier mb{};
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		fn.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 0, 1};
		region.imageExtent = {1, 1, 1};
		fn.vkCmdCopyImageToBuffer(cmd, r.radiance, VK_IMAGE_LAYOUT_GENERAL, r.staging, 1,
				&region);
		VkBufferCopy gcopy{0, 16, kGatherSampleCount * 4 * sizeof(float)};
		fn.vkCmdCopyBuffer(cmd, r.gather, r.staging, 1, &gcopy);
	}

	if (fn.vkEndCommandBuffer(cmd) != VK_SUCCESS)
		return fail("command buffer end failed");

	VkFenceCreateInfo fci{};
	fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (fn.vkCreateFence(d, &fci, nullptr, &r.fence) != VK_SUCCESS)
		return fail("fence creation failed");
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;
	if (fn.vkQueueSubmit(dev.computeQueue(), 1, &submit, r.fence) != VK_SUCCESS)
		return fail("queue submit failed");
	if (fn.vkWaitForFences(d, 1, &r.fence, VK_TRUE, ~0ull) != VK_SUCCESS)
		return fail("fence wait failed (device lost?)");

	if (timestamps) {
		std::vector<std::uint64_t> ticks(2 * passCount * reps, 0);
		// Mask to the family's timestampValidBits (review finding: bits above
		// validBits are undefined; masked subtraction also handles wrap).
		const std::uint32_t validBits = dev.report().timestampValidBits;
		const std::uint64_t mask =
				validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);
		if (fn.vkGetQueryPoolResults(d, r.queryPool, 0, 2 * passCount * reps,
					ticks.size() * sizeof(std::uint64_t), ticks.data(),
					sizeof(std::uint64_t),
					VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
			// Median per pass over iterations 1..reps-1 (iteration 0 = warmup).
			std::vector<double> samples;
			for (std::uint32_t pass = 0; pass < passCount; ++pass) {
				samples.clear();
				for (std::uint32_t rep = 1; rep < reps; ++rep) {
					const std::size_t base =
							2 * (static_cast<std::size_t>(rep) * passCount + pass);
					const std::uint64_t delta = (ticks[base + 1] - ticks[base]) & mask;
					samples.push_back(static_cast<double>(delta) *
							static_cast<double>(dev.report().timestampPeriodNs) * 1e-6);
				}
				if (!samples.empty()) {
					std::sort(samples.begin(), samples.end());
					result.passes[pass].millis = samples[samples.size() / 2];
				}
			}
		}
	}

	// Executed-step statistics (always read: it is 4 bytes).
	{
		const auto *bytes = static_cast<const std::uint8_t *>(r.stagingPtr);
		std::uint32_t totalSteps = 0;
		std::memcpy(&totalSteps, bytes + 8, 4);
		const double cones = static_cast<double>(result.traceCount) * 6.0;
		if (cones > 0.0)
			result.meanStepsPerCone = static_cast<double>(totalSteps) / cones;
	}

	if (params.readback) {
		const auto *bytes = static_cast<const std::uint8_t *>(r.stagingPtr);
		for (int i = 0; i < 4; ++i)
			result.topMipAverage[i] = static_cast<float>(bytes[i]);
		const auto *floats = reinterpret_cast<const float *>(bytes + 16);
		result.gatherSamples.assign(floats, floats + kGatherSampleCount * 4);
	}

	result.ran = true;
	return result;
}

} // namespace vk
