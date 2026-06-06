#include "vk/meshexec.hpp"

#include <cstring>

namespace vk {

// Embedded kernel (configure-time, see CMakeLists.txt / spirv_embed.cpp.in).
extern const std::uint32_t kMeshexecSpirv[];
extern const std::size_t kMeshexecSpirvWords;

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
	VK_FN(vkBeginCommandBuffer);
	VK_FN(vkEndCommandBuffer);
	VK_FN(vkCmdBeginRenderPass);
	VK_FN(vkCmdBindPipeline);
	VK_FN(vkCmdEndRenderPass);
	VK_FN(vkCmdCopyImageToBuffer);
	VK_FN(vkQueueSubmit);
	VK_FN(vkCreateFence);
	VK_FN(vkWaitForFences);
	VK_FN(vkDestroyFence);
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
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT = nullptr; // extension

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
		VK_LOAD(vkBeginCommandBuffer);
		VK_LOAD(vkEndCommandBuffer);
		VK_LOAD(vkCmdBeginRenderPass);
		VK_LOAD(vkCmdBindPipeline);
		VK_LOAD(vkCmdEndRenderPass);
		VK_LOAD(vkCmdCopyImageToBuffer);
		VK_LOAD(vkQueueSubmit);
		VK_LOAD(vkCreateFence);
		VK_LOAD(vkWaitForFences);
		VK_LOAD(vkDestroyFence);
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
		VK_LOAD(vkCmdDrawMeshTasksEXT);
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

// Every owned handle; single teardown ladder (the src/vk house pattern).
struct Resources {
	const Fn &fn;
	VkDevice d;

	VkImage color = VK_NULL_HANDLE;
	VkDeviceMemory colorMem = VK_NULL_HANDLE;
	VkImageView colorView = VK_NULL_HANDLE;
	VkBuffer staging = VK_NULL_HANDLE;
	VkDeviceMemory stagingMem = VK_NULL_HANDLE;
	bool stagingMapped = false;
	void *stagingPtr = nullptr;
	VkShaderModule module = VK_NULL_HANDLE;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~Resources()
	{
		if (stagingMapped)
			fn.vkUnmapMemory(d, stagingMem);
		if (fence)
			fn.vkDestroyFence(d, fence, nullptr);
		if (cmdPool)
			fn.vkDestroyCommandPool(d, cmdPool, nullptr);
		if (pipeline)
			fn.vkDestroyPipeline(d, pipeline, nullptr);
		if (layout)
			fn.vkDestroyPipelineLayout(d, layout, nullptr);
		if (framebuffer)
			fn.vkDestroyFramebuffer(d, framebuffer, nullptr);
		if (renderPass)
			fn.vkDestroyRenderPass(d, renderPass, nullptr);
		if (module)
			fn.vkDestroyShaderModule(d, module, nullptr);
		if (colorView)
			fn.vkDestroyImageView(d, colorView, nullptr);
		if (color)
			fn.vkDestroyImage(d, color, nullptr);
		if (colorMem)
			fn.vkFreeMemory(d, colorMem, nullptr);
		if (staging)
			fn.vkDestroyBuffer(d, staging, nullptr);
		if (stagingMem)
			fn.vkFreeMemory(d, stagingMem, nullptr);
	}
};

} // namespace

MeshExecResult runMeshExec(Device &dev, std::uint32_t width, std::uint32_t height)
{
	MeshExecResult result;
	result.width = width;
	result.height = height;
	if (!dev.available()) {
		result.failureReason = "device unavailable: " + dev.report().failureReason;
		return result;
	}
	if (!dev.report().meshPipelineReady) {
		result.failureReason =
				"mesh tier unavailable: " + dev.report().meshExecBlocked;
		return result;
	}
	if (width == 0 || height == 0 || (width % 2) != 0) {
		result.failureReason = "width must be even and extent nonzero";
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

	// ---- Offscreen color target + readback staging.
	{
		VkImageCreateInfo ici{};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = VK_FORMAT_R8G8B8A8_UNORM;
		ici.extent = {width, height, 1};
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (fn.vkCreateImage(d, &ici, nullptr, &r.color) != VK_SUCCESS)
			return fail("color image creation failed");
		VkMemoryRequirements req{};
		fn.vkGetImageMemoryRequirements(d, r.color, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (type == ~0u)
			return fail("no device-local memory for color image");
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &r.colorMem) != VK_SUCCESS)
			return fail("color memory allocation failed");
		if (fn.vkBindImageMemory(d, r.color, r.colorMem, 0) != VK_SUCCESS)
			return fail("color memory bind failed");
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = r.color;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		if (fn.vkCreateImageView(d, &vci, nullptr, &r.colorView) != VK_SUCCESS)
			return fail("color view creation failed");
	}
	const VkDeviceSize stagingBytes = VkDeviceSize{width} * height * 4;
	{
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = stagingBytes;
		bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (fn.vkCreateBuffer(d, &bci, nullptr, &r.staging) != VK_SUCCESS)
			return fail("staging buffer creation failed");
		VkMemoryRequirements req{};
		fn.vkGetBufferMemoryRequirements(d, r.staging, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (type == ~0u)
			return fail("no host-visible memory for staging");
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &r.stagingMem) != VK_SUCCESS)
			return fail("staging memory allocation failed");
		if (fn.vkBindBufferMemory(d, r.staging, r.stagingMem, 0) != VK_SUCCESS)
			return fail("staging memory bind failed");
		if (fn.vkMapMemory(d, r.stagingMem, 0, stagingBytes, 0, &r.stagingPtr) !=
				VK_SUCCESS)
			return fail("staging map failed");
		r.stagingMapped = true;
	}

	// ---- Render pass: CLEAR -> STORE, ending in TRANSFER_SRC for readback;
	// explicit external dependencies on both sides so neither the clear nor
	// the copy races the rasterization.
	{
		VkAttachmentDescription att{};
		att.format = VK_FORMAT_R8G8B8A8_UNORM;
		att.samples = VK_SAMPLE_COUNT_1_BIT;
		att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkSubpassDescription sub{};
		sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		sub.colorAttachmentCount = 1;
		sub.pColorAttachments = &ref;
		VkSubpassDependency deps[2] = {};
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstSubpass = 0;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].srcAccessMask = 0;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[1].srcSubpass = 0;
		deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
		deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		VkRenderPassCreateInfo rpci{};
		rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpci.attachmentCount = 1;
		rpci.pAttachments = &att;
		rpci.subpassCount = 1;
		rpci.pSubpasses = &sub;
		rpci.dependencyCount = 2;
		rpci.pDependencies = deps;
		if (fn.vkCreateRenderPass(d, &rpci, nullptr, &r.renderPass) != VK_SUCCESS)
			return fail("render pass creation failed");
		VkFramebufferCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = r.renderPass;
		fci.attachmentCount = 1;
		fci.pAttachments = &r.colorView;
		fci.width = width;
		fci.height = height;
		fci.layers = 1;
		if (fn.vkCreateFramebuffer(d, &fci, nullptr, &r.framebuffer) != VK_SUCCESS)
			return fail("framebuffer creation failed");
	}

	// ---- Mesh + fragment pipeline (no vertex input — that is the point).
	{
		VkShaderModuleCreateInfo smci{};
		smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		smci.codeSize = kMeshexecSpirvWords * sizeof(std::uint32_t);
		smci.pCode = kMeshexecSpirv;
		if (fn.vkCreateShaderModule(d, &smci, nullptr, &r.module) != VK_SUCCESS)
			return fail("shader module creation failed");
		VkPipelineLayoutCreateInfo plci{};
		plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		if (fn.vkCreatePipelineLayout(d, &plci, nullptr, &r.layout) != VK_SUCCESS)
			return fail("pipeline layout creation failed");

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_MESH_BIT_EXT;
		stages[0].module = r.module;
		stages[0].pName = "meshMain";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = r.module;
		stages[1].pName = "fragMain";

		const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width),
				static_cast<float>(height), 0.0f, 1.0f};
		const VkRect2D scissor{{0, 0}, {width, height}};
		VkPipelineViewportStateCreateInfo vp{};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.viewportCount = 1;
		vp.pViewports = &viewport;
		vp.scissorCount = 1;
		vp.pScissors = &scissor;

		// Cull NONE: this gate validates mesh-stage execution and coverage,
		// not winding conventions (those are a renderer-contract concern).
		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_NONE;
		raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo ms{};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blendAtt{};
		blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo blend{};
		blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAtt;

		VkGraphicsPipelineCreateInfo gpci{};
		gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpci.stageCount = 2;
		gpci.pStages = stages;
		// Mesh pipelines: vertex-input and input-assembly state are ignored
		// and left null — there is no vertex fetch, by design.
		gpci.pViewportState = &vp;
		gpci.pRasterizationState = &raster;
		gpci.pMultisampleState = &ms;
		gpci.pColorBlendState = &blend;
		gpci.layout = r.layout;
		gpci.renderPass = r.renderPass;
		gpci.subpass = 0;
		if (fn.vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gpci, nullptr,
					&r.pipeline) != VK_SUCCESS)
			return fail("mesh pipeline creation failed");
	}

	// ---- Record, submit on the GRAPHICS queue, read back.
	{
		VkCommandPoolCreateInfo cpci{};
		cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpci.queueFamilyIndex = dev.graphicsQueueFamily();
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
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (fn.vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
			return fail("command buffer begin failed");

		VkClearValue clear{};
		clear.color.float32[0] = 0.0f;
		clear.color.float32[1] = 0.0f;
		clear.color.float32[2] = 0.0f;
		clear.color.float32[3] = 1.0f;
		VkRenderPassBeginInfo rpb{};
		rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpb.renderPass = r.renderPass;
		rpb.framebuffer = r.framebuffer;
		rpb.renderArea = {{0, 0}, {width, height}};
		rpb.clearValueCount = 1;
		rpb.pClearValues = &clear;
		fn.vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);
		fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipeline);
		fn.vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);
		fn.vkCmdEndRenderPass(cmd);

		VkBufferImageCopy region{};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {width, height, 1};
		fn.vkCmdCopyImageToBuffer(cmd, r.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				r.staging, 1, &region);

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
		if (fn.vkQueueSubmit(dev.graphicsQueue(), 1, &submit, r.fence) != VK_SUCCESS)
			return fail("queue submit failed");
		if (fn.vkWaitForFences(d, 1, &r.fence, VK_TRUE, ~0ull) != VK_SUCCESS)
			return fail("fence wait failed (device lost?)");
	}

	result.pixels.resize(static_cast<std::size_t>(stagingBytes));
	std::memcpy(result.pixels.data(), r.stagingPtr, result.pixels.size());
	result.ran = true;
	return result;
}

} // namespace vk
