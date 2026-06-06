#include "vk/worldrenderer.hpp"

#include <cstring>
#include <map>
#include <vector>

namespace vk {

extern const std::uint32_t kChunkSpirv[];
extern const std::size_t kChunkSpirvWords;

namespace {

struct Fn {
#define VK_FN(name) PFN_##name name = nullptr
	VK_FN(vkCreateShaderModule);
	VK_FN(vkCreateImage);
	VK_FN(vkGetImageMemoryRequirements);
	VK_FN(vkAllocateMemory);
	VK_FN(vkBindImageMemory);
	VK_FN(vkCreateImageView);
	VK_FN(vkCreateRenderPass);
	VK_FN(vkCreateFramebuffer);
	VK_FN(vkCreatePipelineLayout);
	VK_FN(vkCreateGraphicsPipelines);
	VK_FN(vkCreateBuffer);
	VK_FN(vkGetBufferMemoryRequirements);
	VK_FN(vkBindBufferMemory);
	VK_FN(vkMapMemory);
	VK_FN(vkUnmapMemory);
	VK_FN(vkCreateCommandPool);
	VK_FN(vkAllocateCommandBuffers);
	VK_FN(vkResetCommandBuffer);
	VK_FN(vkBeginCommandBuffer);
	VK_FN(vkEndCommandBuffer);
	VK_FN(vkCmdBeginRenderPass);
	VK_FN(vkCmdBindPipeline);
	VK_FN(vkCmdBindVertexBuffers);
	VK_FN(vkCmdBindIndexBuffer);
	VK_FN(vkCmdPushConstants);
	VK_FN(vkCmdDrawIndexed);
	VK_FN(vkCmdEndRenderPass);
	VK_FN(vkCmdCopyBuffer);
	VK_FN(vkCmdCopyImageToBuffer);
	VK_FN(vkCmdPipelineBarrier);
	VK_FN(vkCmdResetQueryPool);
	VK_FN(vkCmdWriteTimestamp);
	VK_FN(vkGetQueryPoolResults);
	VK_FN(vkCreateQueryPool);
	VK_FN(vkQueueSubmit);
	VK_FN(vkCreateFence);
	VK_FN(vkWaitForFences);
	VK_FN(vkResetFences);
	VK_FN(vkDestroyFence);
	VK_FN(vkDestroyQueryPool);
	VK_FN(vkDestroyCommandPool);
	VK_FN(vkDestroyPipeline);
	VK_FN(vkDestroyPipelineLayout);
	VK_FN(vkDestroyFramebuffer);
	VK_FN(vkDestroyRenderPass);
	VK_FN(vkDestroyShaderModule);
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
		VK_LOAD(vkCreateShaderModule);
		VK_LOAD(vkCreateImage);
		VK_LOAD(vkGetImageMemoryRequirements);
		VK_LOAD(vkAllocateMemory);
		VK_LOAD(vkBindImageMemory);
		VK_LOAD(vkCreateImageView);
		VK_LOAD(vkCreateRenderPass);
		VK_LOAD(vkCreateFramebuffer);
		VK_LOAD(vkCreatePipelineLayout);
		VK_LOAD(vkCreateGraphicsPipelines);
		VK_LOAD(vkCreateBuffer);
		VK_LOAD(vkGetBufferMemoryRequirements);
		VK_LOAD(vkBindBufferMemory);
		VK_LOAD(vkMapMemory);
		VK_LOAD(vkUnmapMemory);
		VK_LOAD(vkCreateCommandPool);
		VK_LOAD(vkAllocateCommandBuffers);
		VK_LOAD(vkResetCommandBuffer);
		VK_LOAD(vkBeginCommandBuffer);
		VK_LOAD(vkEndCommandBuffer);
		VK_LOAD(vkCmdBeginRenderPass);
		VK_LOAD(vkCmdBindPipeline);
		VK_LOAD(vkCmdBindVertexBuffers);
		VK_LOAD(vkCmdBindIndexBuffer);
		VK_LOAD(vkCmdPushConstants);
		VK_LOAD(vkCmdDrawIndexed);
		VK_LOAD(vkCmdEndRenderPass);
		VK_LOAD(vkCmdCopyBuffer);
		VK_LOAD(vkCmdCopyImageToBuffer);
		VK_LOAD(vkCmdPipelineBarrier);
		VK_LOAD(vkCmdResetQueryPool);
		VK_LOAD(vkCmdWriteTimestamp);
		VK_LOAD(vkGetQueryPoolResults);
		VK_LOAD(vkCreateQueryPool);
		VK_LOAD(vkQueueSubmit);
		VK_LOAD(vkCreateFence);
		VK_LOAD(vkWaitForFences);
		VK_LOAD(vkResetFences);
		VK_LOAD(vkDestroyFence);
		VK_LOAD(vkDestroyQueryPool);
		VK_LOAD(vkDestroyCommandPool);
		VK_LOAD(vkDestroyPipeline);
		VK_LOAD(vkDestroyPipelineLayout);
		VK_LOAD(vkDestroyFramebuffer);
		VK_LOAD(vkDestroyRenderPass);
		VK_LOAD(vkDestroyShaderModule);
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

struct PushBlock {
	float viewProj[16];
	float origin[3];
	float pad;
};
static_assert(sizeof(PushBlock) == 80);

struct DrawRange {
	std::uint32_t indexCount = 0, firstIndex = 0;
	std::int32_t vertexOffset = 0;
	Float3 origin;
};

} // namespace

struct WorldRenderer::Impl {
	Device &dev;
	Fn fn{};
	VkPhysicalDeviceMemoryProperties memProps{};
	std::string err;
	bool valid = false;

	VkFormat format = VK_FORMAT_UNDEFINED;
	std::uint32_t width = 0, height = 0;

	// Created once.
	VkImage depth = VK_NULL_HANDLE;
	VkDeviceMemory depthMem = VK_NULL_HANDLE;
	VkImageView depthView = VK_NULL_HANDLE;
	VkRenderPass rpPresent = VK_NULL_HANDLE;  // final layout PRESENT_SRC_KHR
	VkRenderPass rpTransfer = VK_NULL_HANDLE; // final layout TRANSFER_SRC
	VkShaderModule module = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE; // compatible with both passes
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	VkQueryPool queryPool = VK_NULL_HANDLE;
	float timestampPeriodNs = 0.0f;
	std::uint32_t timestampValidBits = 0;

	// Per command slot.
	VkCommandBuffer cmd[kFramesInFlight] = {};
	VkFence fence[kFramesInFlight] = {};
	bool slotUsed[kFramesInFlight] = {};
	std::uint64_t frameIndex = 0;
	double lastGpuMs = -1.0;

	// Framebuffers per target view (swapchain views + the offscreen view).
	std::map<VkImageView, VkFramebuffer> framebuffers;

	// Scene geometry (device-local).
	VkBuffer vertices = VK_NULL_HANDLE;
	VkDeviceMemory verticesMem = VK_NULL_HANDLE;
	VkBuffer indices = VK_NULL_HANDLE;
	VkDeviceMemory indicesMem = VK_NULL_HANDLE;
	std::vector<DrawRange> ranges;

	// Lazy offscreen target + readback staging (the equivalence-gate path).
	VkImage offColor = VK_NULL_HANDLE;
	VkDeviceMemory offColorMem = VK_NULL_HANDLE;
	VkImageView offView = VK_NULL_HANDLE;
	VkBuffer offStaging = VK_NULL_HANDLE;
	VkDeviceMemory offStagingMem = VK_NULL_HANDLE;
	void *offPtr = nullptr;

	explicit Impl(Device &d) : dev(d) {}

	~Impl()
	{
		// Wait every in-flight slot before tearing anything down.
		for (std::uint32_t s = 0; s < kFramesInFlight; ++s)
			if (slotUsed[s] && fence[s])
				fn.vkWaitForFences(dev.device(), 1, &fence[s], VK_TRUE, ~0ull);
		const VkDevice d = dev.device();
		if (offPtr)
			fn.vkUnmapMemory(d, offStagingMem);
		for (VkBuffer b : {offStaging, vertices, indices})
			if (b)
				fn.vkDestroyBuffer(d, b, nullptr);
		for (VkDeviceMemory m : {offStagingMem, verticesMem, indicesMem, offColorMem})
			if (m)
				fn.vkFreeMemory(d, m, nullptr);
		if (offView)
			fn.vkDestroyImageView(d, offView, nullptr);
		if (offColor)
			fn.vkDestroyImage(d, offColor, nullptr);
		for (auto &[view, fb] : framebuffers)
			if (fb)
				fn.vkDestroyFramebuffer(d, fb, nullptr);
		for (std::uint32_t s = 0; s < kFramesInFlight; ++s)
			if (fence[s])
				fn.vkDestroyFence(d, fence[s], nullptr);
		if (queryPool)
			fn.vkDestroyQueryPool(d, queryPool, nullptr);
		if (cmdPool)
			fn.vkDestroyCommandPool(d, cmdPool, nullptr);
		if (pipeline)
			fn.vkDestroyPipeline(d, pipeline, nullptr);
		if (layout)
			fn.vkDestroyPipelineLayout(d, layout, nullptr);
		if (module)
			fn.vkDestroyShaderModule(d, module, nullptr);
		for (VkRenderPass rp : {rpPresent, rpTransfer})
			if (rp)
				fn.vkDestroyRenderPass(d, rp, nullptr);
		if (depthView)
			fn.vkDestroyImageView(d, depthView, nullptr);
		if (depth)
			fn.vkDestroyImage(d, depth, nullptr);
		if (depthMem)
			fn.vkFreeMemory(d, depthMem, nullptr);
	}

	bool fail(const char *why)
	{
		err = why;
		return false;
	}

	bool makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags memFlags, VkBuffer &buf, VkDeviceMemory &mem)
	{
		const VkDevice d = dev.device();
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

	bool makeImage2D(VkFormat fmt, VkImageUsageFlags usage, VkImageAspectFlags aspect,
			VkImage &img, VkDeviceMemory &mem, VkImageView &view)
	{
		const VkDevice d = dev.device();
		VkImageCreateInfo ici{};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = fmt;
		ici.extent = {width, height, 1};
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = usage;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (fn.vkCreateImage(d, &ici, nullptr, &img) != VK_SUCCESS)
			return false;
		VkMemoryRequirements req{};
		fn.vkGetImageMemoryRequirements(d, img, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (type == ~0u)
			return false;
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &mem) != VK_SUCCESS)
			return false;
		if (fn.vkBindImageMemory(d, img, mem, 0) != VK_SUCCESS)
			return false;
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = img;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = fmt;
		vci.subresourceRange = {aspect, 0, 1, 0, 1};
		return fn.vkCreateImageView(d, &vci, nullptr, &view) == VK_SUCCESS;
	}

	VkRenderPass makeRenderPass(VkImageLayout finalLayout)
	{
		// Shared-depth note — these dependencies are DELIBERATELY a superset
		// of the one-shot path's (review-adjudicated; do not "simplify" them
		// back): one depth target serves both frames in flight, and ADJACENT
		// frames overlap (slot N's fence is waited at N+2, not N+1), so the
		// ONLY thing ordering frame N+1's depth load/clear after frame N's
		// depth writes is this external dependency. srcStageMask naming
		// LATE_FRAGMENT_TESTS with depth-write access refers to the PRIOR
		// submission's stages — the canonical single-depth idiom. Subpass
		// dependencies affect ordering, never rasterization output, which
		// is why the pixel-equivalence gate against the one-shot path is
		// the correct equivalence definition.
		VkAttachmentDescription atts[2] = {};
		atts[0].format = format;
		atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
		atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		atts[0].finalLayout = finalLayout;
		atts[1].format = VK_FORMAT_D32_SFLOAT;
		atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
		atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentReference depthRef{1,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		VkSubpassDescription sub{};
		sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		sub.colorAttachmentCount = 1;
		sub.pColorAttachments = &colorRef;
		sub.pDepthStencilAttachment = &depthRef;
		const VkPipelineStageFlags fragStages =
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		VkSubpassDependency deps[2] = {};
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstSubpass = 0;
		deps[0].srcStageMask = fragStages;
		deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[0].dstStageMask = fragStages;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[1].srcSubpass = 0;
		deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcStageMask = fragStages;
		deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		VkRenderPassCreateInfo rpci{};
		rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpci.attachmentCount = 2;
		rpci.pAttachments = atts;
		rpci.subpassCount = 1;
		rpci.pSubpasses = &sub;
		rpci.dependencyCount = 2;
		rpci.pDependencies = deps;
		VkRenderPass rp = VK_NULL_HANDLE;
		if (fn.vkCreateRenderPass(dev.device(), &rpci, nullptr, &rp) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		return rp;
	}

	bool init(VkFormat fmt, std::uint32_t w, std::uint32_t h)
	{
		format = fmt;
		width = w;
		height = h;
		if (!dev.available())
			return fail("device unavailable");
		if (!dev.report().meshPipelineReady && !dev.report().presentReady)
			return fail("no graphics tier");
		if (!fn.load(dev))
			return fail("device-level function resolution failed");
		auto getMemProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
				dev.instanceLevelProc("vkGetPhysicalDeviceMemoryProperties"));
		if (!getMemProps)
			return fail("memory properties not resolvable");
		getMemProps(dev.physicalDevice(), &memProps);
		timestampPeriodNs = dev.report().timestampPeriodNs;
		timestampValidBits = dev.report().timestampValidBits;
		const VkDevice d = dev.device();

		if (!makeImage2D(VK_FORMAT_D32_SFLOAT,
					VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
					VK_IMAGE_ASPECT_DEPTH_BIT, depth, depthMem, depthView))
			return fail("depth target creation failed");

		rpPresent = makeRenderPass(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
		rpTransfer = makeRenderPass(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		if (!rpPresent || !rpTransfer)
			return fail("render pass creation failed");

		// Pipeline — STATE IDENTICAL to the one-shot path (the equivalence
		// gate depends on it); compatible with both render passes (layouts
		// do not participate in render-pass compatibility).
		{
			VkShaderModuleCreateInfo smci{};
			smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			smci.codeSize = kChunkSpirvWords * sizeof(std::uint32_t);
			smci.pCode = kChunkSpirv;
			if (fn.vkCreateShaderModule(d, &smci, nullptr, &module) != VK_SUCCESS)
				return fail("shader module creation failed");
			VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushBlock)};
			VkPipelineLayoutCreateInfo plci{};
			plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
			plci.pushConstantRangeCount = 1;
			plci.pPushConstantRanges = &range;
			if (fn.vkCreatePipelineLayout(d, &plci, nullptr, &layout) != VK_SUCCESS)
				return fail("pipeline layout creation failed");

			VkPipelineShaderStageCreateInfo stages[2] = {};
			stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = module;
			stages[0].pName = "vsMain";
			stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stages[1].module = module;
			stages[1].pName = "fsMain";

			VkVertexInputBindingDescription binding{0, sizeof(mesh::PackedVertex),
					VK_VERTEX_INPUT_RATE_VERTEX};
			VkVertexInputAttributeDescription attr{0, 0, VK_FORMAT_R32G32_UINT, 0};
			VkPipelineVertexInputStateCreateInfo vin{};
			vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vin.vertexBindingDescriptionCount = 1;
			vin.pVertexBindingDescriptions = &binding;
			vin.vertexAttributeDescriptionCount = 1;
			vin.pVertexAttributeDescriptions = &attr;

			VkPipelineInputAssemblyStateCreateInfo ia{};
			ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
			ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

			const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width),
					static_cast<float>(height), 0.0f, 1.0f};
			const VkRect2D scissor{{0, 0}, {width, height}};
			VkPipelineViewportStateCreateInfo vp{};
			vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			vp.viewportCount = 1;
			vp.pViewports = &viewport;
			vp.scissorCount = 1;
			vp.pScissors = &scissor;

			VkPipelineRasterizationStateCreateInfo raster{};
			raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
			raster.polygonMode = VK_POLYGON_MODE_FILL;
			raster.cullMode = VK_CULL_MODE_NONE;
			raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
			raster.lineWidth = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{};
			ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo ds{};
			ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
			ds.depthTestEnable = VK_TRUE;
			ds.depthWriteEnable = VK_TRUE;
			ds.depthCompareOp = VK_COMPARE_OP_LESS;

			VkPipelineColorBlendAttachmentState blendAtt{};
			blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
					VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
					VK_COLOR_COMPONENT_A_BIT;
			VkPipelineColorBlendStateCreateInfo blend{};
			blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			blend.attachmentCount = 1;
			blend.pAttachments = &blendAtt;

			VkGraphicsPipelineCreateInfo gpci{};
			gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			gpci.stageCount = 2;
			gpci.pStages = stages;
			gpci.pVertexInputState = &vin;
			gpci.pInputAssemblyState = &ia;
			gpci.pViewportState = &vp;
			gpci.pRasterizationState = &raster;
			gpci.pMultisampleState = &ms;
			gpci.pDepthStencilState = &ds;
			gpci.pColorBlendState = &blend;
			gpci.layout = layout;
			gpci.renderPass = rpPresent;
			gpci.subpass = 0;
			if (fn.vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gpci, nullptr,
						&pipeline) != VK_SUCCESS)
				return fail("pipeline creation failed");
		}

		// Command slots + per-slot timestamp pairs.
		{
			VkCommandPoolCreateInfo cpci{};
			cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			cpci.queueFamilyIndex = dev.graphicsQueueFamily();
			if (fn.vkCreateCommandPool(d, &cpci, nullptr, &cmdPool) != VK_SUCCESS)
				return fail("command pool creation failed");
			VkCommandBufferAllocateInfo cbai{};
			cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			cbai.commandPool = cmdPool;
			cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cbai.commandBufferCount = kFramesInFlight;
			if (fn.vkAllocateCommandBuffers(d, &cbai, cmd) != VK_SUCCESS)
				return fail("command buffer allocation failed");
			for (std::uint32_t s = 0; s < kFramesInFlight; ++s) {
				VkFenceCreateInfo fci{};
				fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				if (fn.vkCreateFence(d, &fci, nullptr, &fence[s]) != VK_SUCCESS)
					return fail("slot fence creation failed");
			}
			if (timestampPeriodNs > 0.0f) {
				VkQueryPoolCreateInfo qpci{};
				qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
				qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
				qpci.queryCount = 2 * kFramesInFlight;
				if (fn.vkCreateQueryPool(d, &qpci, nullptr, &queryPool) != VK_SUCCESS)
					return fail("query pool creation failed");
			}
		}
		valid = true;
		return true;
	}

	VkFramebuffer framebufferFor(VkImageView view, VkRenderPass rp)
	{
		if (const auto it = framebuffers.find(view); it != framebuffers.end())
			return it->second;
		const VkImageView views[2] = {view, depthView};
		VkFramebufferCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = rp; // compatible across both passes
		fci.attachmentCount = 2;
		fci.pAttachments = views;
		fci.width = width;
		fci.height = height;
		fci.layers = 1;
		VkFramebuffer fb = VK_NULL_HANDLE;
		if (fn.vkCreateFramebuffer(dev.device(), &fci, nullptr, &fb) != VK_SUCCESS)
			return VK_NULL_HANDLE;
		framebuffers.emplace(view, fb);
		return fb;
	}

	// Wait the slot's prior frame (collecting its GPU time), record, submit.
	bool submitFrame(const WorldCamera &camera, VkImageView target, VkRenderPass rp,
			bool withOffscreenCopy, VkSemaphore waitSem, VkPipelineStageFlags waitStage,
			VkSemaphore signalSem, std::string &error)
	{
		const VkDevice d = dev.device();
		const std::uint32_t slot =
				static_cast<std::uint32_t>(frameIndex % kFramesInFlight);
		++frameIndex;

		if (slotUsed[slot]) {
			if (fn.vkWaitForFences(d, 1, &fence[slot], VK_TRUE, ~0ull) != VK_SUCCESS) {
				error = "slot fence wait failed";
				return false;
			}
			collectGpuTime(slot);
		}
		fn.vkResetFences(d, 1, &fence[slot]);

		VkCommandBuffer c = cmd[slot];
		if (fn.vkResetCommandBuffer(c, 0) != VK_SUCCESS) {
			error = "command buffer reset failed";
			return false;
		}
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (fn.vkBeginCommandBuffer(c, &begin) != VK_SUCCESS) {
			error = "command buffer begin failed";
			return false;
		}

		if (queryPool) {
			fn.vkCmdResetQueryPool(c, queryPool, slot * 2, 2);
			fn.vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool,
					slot * 2);
		}

		const VkFramebuffer fb = framebufferFor(target, rp);
		if (!fb) {
			error = "framebuffer creation failed";
			return false;
		}
		VkClearValue clears[2]{};
		clears[0].color.float32[0] = kWorldViewClearF[0];
		clears[0].color.float32[1] = kWorldViewClearF[1];
		clears[0].color.float32[2] = kWorldViewClearF[2];
		clears[0].color.float32[3] = kWorldViewClearF[3];
		clears[1].depthStencil = {1.0f, 0};
		VkRenderPassBeginInfo rpb{};
		rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpb.renderPass = rp;
		rpb.framebuffer = fb;
		rpb.renderArea = {{0, 0}, {width, height}};
		rpb.clearValueCount = 2;
		rpb.pClearValues = clears;
		fn.vkCmdBeginRenderPass(c, &rpb, VK_SUBPASS_CONTENTS_INLINE);

		if (!ranges.empty()) {
			fn.vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			const VkDeviceSize zero = 0;
			fn.vkCmdBindVertexBuffers(c, 0, 1, &vertices, &zero);
			fn.vkCmdBindIndexBuffer(c, indices, 0, VK_INDEX_TYPE_UINT32);
			const float aspect =
					static_cast<float>(width) / static_cast<float>(height);
			const Mat4 viewProj = mat4Mul(
					mat4Perspective(camera.fovYRadians, aspect, camera.nearZ,
							camera.farZ),
					mat4LookAt(camera.eye, camera.target, camera.up));
			PushBlock push{};
			std::memcpy(push.viewProj, viewProj.m, sizeof(push.viewProj));
			for (const DrawRange &dr : ranges) {
				push.origin[0] = dr.origin.x;
				push.origin[1] = dr.origin.y;
				push.origin[2] = dr.origin.z;
				fn.vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						sizeof(PushBlock), &push);
				fn.vkCmdDrawIndexed(c, dr.indexCount, 1, dr.firstIndex,
						dr.vertexOffset, 0);
			}
		}
		fn.vkCmdEndRenderPass(c);

		if (queryPool)
			fn.vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool,
					slot * 2 + 1);

		if (withOffscreenCopy) {
			VkBufferImageCopy region{};
			region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			region.imageExtent = {width, height, 1};
			fn.vkCmdCopyImageToBuffer(c, offColor,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, offStaging, 1, &region);
		}

		if (fn.vkEndCommandBuffer(c) != VK_SUCCESS) {
			error = "command buffer end failed";
			return false;
		}
		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &c;
		if (waitSem) {
			submit.waitSemaphoreCount = 1;
			submit.pWaitSemaphores = &waitSem;
			submit.pWaitDstStageMask = &waitStage;
		}
		if (signalSem) {
			submit.signalSemaphoreCount = 1;
			submit.pSignalSemaphores = &signalSem;
		}
		if (fn.vkQueueSubmit(dev.graphicsQueue(), 1, &submit, fence[slot]) !=
				VK_SUCCESS) {
			error = "queue submit failed";
			return false;
		}
		slotUsed[slot] = true;
		return true;
	}

	void collectGpuTime(std::uint32_t slot)
	{
		if (!queryPool)
			return;
		std::uint64_t ticks[2] = {};
		if (fn.vkGetQueryPoolResults(dev.device(), queryPool, slot * 2, 2,
					sizeof(ticks), ticks, sizeof(std::uint64_t),
					VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
			return;
		const std::uint64_t mask = timestampValidBits >= 64
				? ~0ull
				: ((1ull << timestampValidBits) - 1ull);
		const std::uint64_t delta = (ticks[1] - ticks[0]) & mask;
		lastGpuMs = static_cast<double>(delta) *
				static_cast<double>(timestampPeriodNs) * 1e-6;
	}

	std::uint32_t lastSubmittedSlot() const
	{
		return static_cast<std::uint32_t>((frameIndex + kFramesInFlight - 1) %
				kFramesInFlight);
	}
};

WorldRenderer::WorldRenderer(Device &dev, VkFormat colorFormat, std::uint32_t width,
		std::uint32_t height)
		: m_impl(std::make_unique<Impl>(dev))
{
	m_impl->init(colorFormat, width, height);
}

WorldRenderer::~WorldRenderer() = default;

bool WorldRenderer::ok() const
{
	return m_impl->valid;
}

const std::string &WorldRenderer::error() const
{
	return m_impl->err;
}

bool WorldRenderer::uploadScene(std::span<const ChunkDraw> draws)
{
	Impl &im = *m_impl;
	if (!im.valid)
		return false;
	const VkDevice d = im.dev.device();

	// Quiesce in-flight frames before touching geometry — and mark every
	// slot idle (the fences are consumed; stale slotUsed flags would cause
	// needless waits on the next frames).
	for (std::uint32_t s = 0; s < kFramesInFlight; ++s) {
		if (im.slotUsed[s])
			im.fn.vkWaitForFences(d, 1, &im.fence[s], VK_TRUE, ~0ull);
		im.slotUsed[s] = false;
	}

	for (VkBuffer b : {im.vertices, im.indices})
		if (b)
			im.fn.vkDestroyBuffer(d, b, nullptr);
	for (VkDeviceMemory m : {im.verticesMem, im.indicesMem})
		if (m)
			im.fn.vkFreeMemory(d, m, nullptr);
	im.vertices = im.indices = VK_NULL_HANDLE;
	im.verticesMem = im.indicesMem = VK_NULL_HANDLE;
	im.ranges.clear();

	std::uint64_t totalVerts = 0, totalIndices = 0;
	for (const ChunkDraw &cd : draws) {
		if (!cd.chunkMesh || cd.chunkMesh->quadCount == 0)
			continue;
		im.ranges.push_back({static_cast<std::uint32_t>(cd.chunkMesh->indices.size()),
				static_cast<std::uint32_t>(totalIndices),
				static_cast<std::int32_t>(totalVerts), cd.origin});
		totalVerts += cd.chunkMesh->vertices.size();
		totalIndices += cd.chunkMesh->indices.size();
	}
	if (totalVerts == 0)
		return true; // empty scene is a valid scene
	if (totalVerts > 0x7FFFFFFFull)
		return im.fail("scene exceeds 31-bit vertex offsets");

	const VkDeviceSize vBytes = totalVerts * sizeof(mesh::PackedVertex);
	const VkDeviceSize iBytes = totalIndices * sizeof(std::uint32_t);

	// Staging (host) -> device-local, one blocking transfer.
	VkBuffer staging = VK_NULL_HANDLE;
	VkDeviceMemory stagingMem = VK_NULL_HANDLE;
	const bool stagingOk = im.makeBuffer(vBytes + iBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			staging, stagingMem);
	const auto cleanupStaging = [&] {
		if (staging)
			im.fn.vkDestroyBuffer(d, staging, nullptr);
		if (stagingMem)
			im.fn.vkFreeMemory(d, stagingMem, nullptr);
	};
	if (!stagingOk) {
		cleanupStaging();
		return im.fail("staging buffer creation failed");
	}
	void *map = nullptr;
	if (im.fn.vkMapMemory(d, stagingMem, 0, vBytes + iBytes, 0, &map) != VK_SUCCESS) {
		cleanupStaging();
		return im.fail("staging map failed");
	}
	{
		std::uint64_t vAt = 0, iAt = 0;
		auto *bytes = static_cast<std::byte *>(map);
		for (const ChunkDraw &cd : draws) {
			if (!cd.chunkMesh || cd.chunkMesh->quadCount == 0)
				continue;
			std::memcpy(bytes + vAt * sizeof(mesh::PackedVertex),
					cd.chunkMesh->vertices.data(),
					cd.chunkMesh->vertices.size() * sizeof(mesh::PackedVertex));
			std::memcpy(bytes + vBytes + iAt * sizeof(std::uint32_t),
					cd.chunkMesh->indices.data(),
					cd.chunkMesh->indices.size() * sizeof(std::uint32_t));
			vAt += cd.chunkMesh->vertices.size();
			iAt += cd.chunkMesh->indices.size();
		}
		im.fn.vkUnmapMemory(d, stagingMem);
	}
	if (!im.makeBuffer(vBytes,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, im.vertices, im.verticesMem) ||
			!im.makeBuffer(iBytes,
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, im.indices, im.indicesMem)) {
		cleanupStaging();
		return im.fail("device-local geometry allocation failed");
	}

	// One-time transfer on slot 0's command buffer (slots are quiesced).
	VkCommandBuffer c = im.cmd[0];
	im.fn.vkResetCommandBuffer(c, 0);
	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	im.fn.vkBeginCommandBuffer(c, &begin);
	VkBufferCopy vcopy{0, 0, vBytes};
	im.fn.vkCmdCopyBuffer(c, staging, im.vertices, 1, &vcopy);
	VkBufferCopy icopy{vBytes, 0, iBytes};
	im.fn.vkCmdCopyBuffer(c, staging, im.indices, 1, &icopy);
	VkMemoryBarrier mb{};
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
	im.fn.vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
	im.fn.vkEndCommandBuffer(c);
	im.fn.vkResetFences(d, 1, &im.fence[0]);
	VkSubmitInfo submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &c;
	if (im.fn.vkQueueSubmit(im.dev.graphicsQueue(), 1, &submit, im.fence[0]) !=
			VK_SUCCESS) {
		cleanupStaging();
		return im.fail("upload submit failed");
	}
	im.fn.vkWaitForFences(d, 1, &im.fence[0], VK_TRUE, ~0ull);
	im.slotUsed[0] = false; // fence consumed; slot is idle again
	cleanupStaging();
	return true;
}

bool WorldRenderer::renderFrameAsync(const WorldCamera &camera, VkImageView target,
		VkSemaphore waitSemaphore, VkPipelineStageFlags waitStage,
		VkSemaphore signalSemaphore, std::string &error)
{
	if (!m_impl->valid) {
		error = m_impl->err;
		return false;
	}
	return m_impl->submitFrame(camera, target, m_impl->rpPresent,
			/*withOffscreenCopy=*/false, waitSemaphore, waitStage, signalSemaphore,
			error);
}

WorldViewResult WorldRenderer::renderOffscreen(const WorldCamera &camera)
{
	WorldViewResult result;
	Impl &im = *m_impl;
	result.width = im.width;
	result.height = im.height;
	if (!im.valid) {
		result.failureReason = im.err;
		return result;
	}
	const VkDevice d = im.dev.device();

	// Lazy offscreen target + staging (created once, reused; the format is
	// fixed to the constructor's — renderOffscreen has no format of its own).
	if (im.offColor == VK_NULL_HANDLE) {
		if (!im.makeImage2D(im.format,
					VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT, im.offColor, im.offColorMem,
					im.offView)) {
			result.failureReason = "offscreen target creation failed";
			return result;
		}
		const VkDeviceSize bytes = VkDeviceSize{im.width} * im.height * 4;
		if (!im.makeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					im.offStaging, im.offStagingMem) ||
				im.fn.vkMapMemory(d, im.offStagingMem, 0, bytes, 0, &im.offPtr) !=
						VK_SUCCESS) {
			result.failureReason = "offscreen staging creation failed";
			return result;
		}
	}

	std::string err;
	if (!im.submitFrame(camera, im.offView, im.rpTransfer, /*withOffscreenCopy=*/true,
				VK_NULL_HANDLE, 0, VK_NULL_HANDLE, err)) {
		result.failureReason = err;
		return result;
	}
	const std::uint32_t slot = im.lastSubmittedSlot();
	if (im.fn.vkWaitForFences(d, 1, &im.fence[slot], VK_TRUE, ~0ull) != VK_SUCCESS) {
		result.failureReason = "offscreen fence wait failed";
		return result;
	}
	im.collectGpuTime(slot);
	im.slotUsed[slot] = false;

	for (const DrawRange &dr : im.ranges)
		result.drawnQuads += dr.indexCount / 6;
	const std::size_t bytes = std::size_t{im.width} * im.height * 4;
	result.pixels.resize(bytes);
	std::memcpy(result.pixels.data(), im.offPtr, bytes);
	result.ran = true;
	return result;
}

double WorldRenderer::lastGpuMillis() const
{
	return m_impl->lastGpuMs;
}

} // namespace vk
