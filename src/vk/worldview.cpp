#include "vk/worldview.hpp"

#include <cstring>

namespace vk {

// Embedded shaders (configure-time, see CMakeLists.txt / spirv_embed.cpp.in).
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
	VK_FN(vkBeginCommandBuffer);
	VK_FN(vkEndCommandBuffer);
	VK_FN(vkCmdBeginRenderPass);
	VK_FN(vkCmdBindPipeline);
	VK_FN(vkCmdBindVertexBuffers);
	VK_FN(vkCmdBindIndexBuffer);
	VK_FN(vkCmdPushConstants);
	VK_FN(vkCmdDrawIndexed);
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
		VK_LOAD(vkCmdBindVertexBuffers);
		VK_LOAD(vkCmdBindIndexBuffer);
		VK_LOAD(vkCmdPushConstants);
		VK_LOAD(vkCmdDrawIndexed);
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

struct Resources {
	const Fn &fn;
	VkDevice d;

	VkImage depth = VK_NULL_HANDLE;
	VkDeviceMemory depthMem = VK_NULL_HANDLE;
	VkImageView depthView = VK_NULL_HANDLE;
	VkBuffer vertices = VK_NULL_HANDLE;
	VkDeviceMemory verticesMem = VK_NULL_HANDLE;
	VkBuffer indices = VK_NULL_HANDLE;
	VkDeviceMemory indicesMem = VK_NULL_HANDLE;
	VkShaderModule module = VK_NULL_HANDLE;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VkPipeline pipeline = VK_NULL_HANDLE;
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;

	~Resources()
	{
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
		if (depthView)
			fn.vkDestroyImageView(d, depthView, nullptr);
		if (depth)
			fn.vkDestroyImage(d, depth, nullptr);
		if (depthMem)
			fn.vkFreeMemory(d, depthMem, nullptr);
		for (VkBuffer b : {vertices, indices})
			if (b)
				fn.vkDestroyBuffer(d, b, nullptr);
		for (VkDeviceMemory m : {verticesMem, indicesMem})
			if (m)
				fn.vkFreeMemory(d, m, nullptr);
	}

	bool makeHostBuffer(const VkPhysicalDeviceMemoryProperties &memProps,
			VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &buf,
			VkDeviceMemory &mem, void **mapped)
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
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (type == ~0u)
			return false;
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &mem) != VK_SUCCESS)
			return false;
		if (fn.vkBindBufferMemory(d, buf, mem, 0) != VK_SUCCESS)
			return false;
		return fn.vkMapMemory(d, mem, 0, size, 0, mapped) == VK_SUCCESS;
	}
};

struct PushBlock {
	float viewProj[16];
	float origin[3];
	float pad;
};
static_assert(sizeof(PushBlock) == 80);

} // namespace

bool renderWorldViewInto(Device &dev, std::span<const ChunkDraw> draws,
		const WorldCamera &camera, VkImageView targetView, VkFormat targetFormat,
		std::uint32_t width, std::uint32_t height, VkImageLayout finalLayout,
		std::uint64_t &drawnQuads, std::string &error)
{
	drawnQuads = 0;
	if (!dev.available()) {
		error = "device unavailable: " + dev.report().failureReason;
		return false;
	}
	if (!dev.report().meshPipelineReady && !dev.report().presentReady) {
		error = "no graphics tier: " + (dev.report().meshExecBlocked.empty()
												? dev.report().presentBlocked
												: dev.report().meshExecBlocked);
		return false;
	}
	if (width == 0 || height == 0) {
		error = "zero-extent target";
		return false;
	}

	Fn fn;
	if (!fn.load(dev)) {
		error = "device-level function resolution failed";
		return false;
	}
	auto getMemProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
			dev.instanceLevelProc("vkGetPhysicalDeviceMemoryProperties"));
	if (!getMemProps) {
		error = "vkGetPhysicalDeviceMemoryProperties not resolvable";
		return false;
	}
	VkPhysicalDeviceMemoryProperties memProps{};
	getMemProps(dev.physicalDevice(), &memProps);

	Resources r{fn, dev.device()};
	const VkDevice d = r.d;
	const auto fail = [&error](const char *why) {
		error = why;
		return false;
	};

	// ---- Geometry upload (host-visible; the perf pass owns device-local
	// staging later). One merged vertex + index buffer; per-draw offsets.
	struct DrawRange {
		std::uint32_t indexCount, firstIndex;
		std::int32_t vertexOffset;
		Float3 origin;
	};
	std::vector<DrawRange> ranges;
	std::uint64_t totalVerts = 0, totalIndices = 0;
	for (const ChunkDraw &cd : draws) {
		if (!cd.chunkMesh || cd.chunkMesh->quadCount == 0)
			continue;
		ranges.push_back({static_cast<std::uint32_t>(cd.chunkMesh->indices.size()),
				static_cast<std::uint32_t>(totalIndices),
				static_cast<std::int32_t>(totalVerts), cd.origin});
		totalVerts += cd.chunkMesh->vertices.size();
		totalIndices += cd.chunkMesh->indices.size();
		drawnQuads += cd.chunkMesh->quadCount;
	}
	if (totalVerts > 0x7FFFFFFFull)
		return fail("scene exceeds 31-bit vertex offsets");

	void *vmap = nullptr, *imap = nullptr;
	if (totalVerts > 0) {
		if (!r.makeHostBuffer(memProps, totalVerts * sizeof(mesh::PackedVertex),
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, r.vertices, r.verticesMem, &vmap))
			return fail("vertex buffer creation failed");
		if (!r.makeHostBuffer(memProps, totalIndices * sizeof(std::uint32_t),
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT, r.indices, r.indicesMem, &imap))
			return fail("index buffer creation failed");
		std::uint64_t vAt = 0, iAt = 0;
		for (const ChunkDraw &cd : draws) {
			if (!cd.chunkMesh || cd.chunkMesh->quadCount == 0)
				continue;
			std::memcpy(static_cast<std::byte *>(vmap) +
							vAt * sizeof(mesh::PackedVertex),
					cd.chunkMesh->vertices.data(),
					cd.chunkMesh->vertices.size() * sizeof(mesh::PackedVertex));
			std::memcpy(static_cast<std::byte *>(imap) + iAt * sizeof(std::uint32_t),
					cd.chunkMesh->indices.data(),
					cd.chunkMesh->indices.size() * sizeof(std::uint32_t));
			vAt += cd.chunkMesh->vertices.size();
			iAt += cd.chunkMesh->indices.size();
		}
		fn.vkUnmapMemory(d, r.verticesMem);
		fn.vkUnmapMemory(d, r.indicesMem);
	}

	// ---- Depth target.
	{
		VkImageCreateInfo ici{};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = VK_FORMAT_D32_SFLOAT;
		ici.extent = {width, height, 1};
		ici.mipLevels = 1;
		ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (fn.vkCreateImage(d, &ici, nullptr, &r.depth) != VK_SUCCESS)
			return fail("depth image creation failed");
		VkMemoryRequirements req{};
		fn.vkGetImageMemoryRequirements(d, r.depth, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (type == ~0u)
			return fail("no device-local memory for depth");
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &r.depthMem) != VK_SUCCESS)
			return fail("depth memory allocation failed");
		if (fn.vkBindImageMemory(d, r.depth, r.depthMem, 0) != VK_SUCCESS)
			return fail("depth memory bind failed");
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = r.depth;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = VK_FORMAT_D32_SFLOAT;
		vci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
		if (fn.vkCreateImageView(d, &vci, nullptr, &r.depthView) != VK_SUCCESS)
			return fail("depth view creation failed");
	}

	// ---- Render pass: color (CLEAR -> finalLayout) + depth (CLEAR, discard).
	{
		VkAttachmentDescription atts[2] = {};
		atts[0].format = targetFormat;
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
		VkSubpassDependency deps[2] = {};
		deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		deps[0].dstSubpass = 0;
		deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		deps[0].srcAccessMask = 0;
		deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		deps[1].srcSubpass = 0;
		deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
		if (fn.vkCreateRenderPass(d, &rpci, nullptr, &r.renderPass) != VK_SUCCESS)
			return fail("render pass creation failed");
		const VkImageView views[2] = {targetView, r.depthView};
		VkFramebufferCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = r.renderPass;
		fci.attachmentCount = 2;
		fci.pAttachments = views;
		fci.width = width;
		fci.height = height;
		fci.layers = 1;
		if (fn.vkCreateFramebuffer(d, &fci, nullptr, &r.framebuffer) != VK_SUCCESS)
			return fail("framebuffer creation failed");
	}

	// ---- Pipeline (classic vertex path over the 8-byte packed vertices).
	{
		VkShaderModuleCreateInfo smci{};
		smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		smci.codeSize = kChunkSpirvWords * sizeof(std::uint32_t);
		smci.pCode = kChunkSpirv;
		if (fn.vkCreateShaderModule(d, &smci, nullptr, &r.module) != VK_SUCCESS)
			return fail("shader module creation failed");
		VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushBlock)};
		VkPipelineLayoutCreateInfo plci{};
		plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		plci.pushConstantRangeCount = 1;
		plci.pPushConstantRanges = &range;
		if (fn.vkCreatePipelineLayout(d, &plci, nullptr, &r.layout) != VK_SUCCESS)
			return fail("pipeline layout creation failed");

		VkPipelineShaderStageCreateInfo stages[2] = {};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = r.module;
		stages[0].pName = "vsMain";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = r.module;
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

		// Cull NONE for the correctness milestone: the projection's Y flip
		// inverts screen-space winding, and depth testing already validates
		// the geometry; cull tuning belongs to the perf pass.
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
		gpci.pVertexInputState = &vin;
		gpci.pInputAssemblyState = &ia;
		gpci.pViewportState = &vp;
		gpci.pRasterizationState = &raster;
		gpci.pMultisampleState = &ms;
		gpci.pDepthStencilState = &ds;
		gpci.pColorBlendState = &blend;
		gpci.layout = r.layout;
		gpci.renderPass = r.renderPass;
		gpci.subpass = 0;
		if (fn.vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gpci, nullptr,
					&r.pipeline) != VK_SUCCESS)
			return fail("world pipeline creation failed");
	}

	// ---- Record + submit on the graphics queue.
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

		VkClearValue clears[2]{};
		clears[0].color.float32[0] = kWorldViewClearF[0];
		clears[0].color.float32[1] = kWorldViewClearF[1];
		clears[0].color.float32[2] = kWorldViewClearF[2];
		clears[0].color.float32[3] = kWorldViewClearF[3];
		clears[1].depthStencil = {1.0f, 0};
		VkRenderPassBeginInfo rpb{};
		rpb.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpb.renderPass = r.renderPass;
		rpb.framebuffer = r.framebuffer;
		rpb.renderArea = {{0, 0}, {width, height}};
		rpb.clearValueCount = 2;
		rpb.pClearValues = clears;
		fn.vkCmdBeginRenderPass(cmd, &rpb, VK_SUBPASS_CONTENTS_INLINE);

		if (!ranges.empty()) {
			fn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.pipeline);
			const VkDeviceSize zero = 0;
			fn.vkCmdBindVertexBuffers(cmd, 0, 1, &r.vertices, &zero);
			fn.vkCmdBindIndexBuffer(cmd, r.indices, 0, VK_INDEX_TYPE_UINT32);

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
				fn.vkCmdPushConstants(cmd, r.layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
						sizeof(PushBlock), &push);
				fn.vkCmdDrawIndexed(cmd, dr.indexCount, 1, dr.firstIndex,
						dr.vertexOffset, 0);
			}
		}
		fn.vkCmdEndRenderPass(cmd);

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
	return true;
}

WorldViewResult renderWorldView(Device &dev, std::span<const ChunkDraw> draws,
		const WorldCamera &camera, std::uint32_t width, std::uint32_t height)
{
	WorldViewResult result;
	result.width = width;
	result.height = height;
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
	VkPhysicalDeviceMemoryProperties memProps{};
	if (getMemProps)
		getMemProps(dev.physicalDevice(), &memProps);

	// Offscreen RGBA8 target + staging, then the shared render, then a
	// second tiny submission for the readback copy.
	struct Offscreen {
		const Fn &fn;
		VkDevice d;
		VkImage color = VK_NULL_HANDLE;
		VkDeviceMemory colorMem = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkBuffer staging = VK_NULL_HANDLE;
		VkDeviceMemory stagingMem = VK_NULL_HANDLE;
		bool mapped = false;
		void *ptr = nullptr;
		VkCommandPool pool = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		~Offscreen()
		{
			if (mapped)
				fn.vkUnmapMemory(d, stagingMem);
			if (fence)
				fn.vkDestroyFence(d, fence, nullptr);
			if (pool)
				fn.vkDestroyCommandPool(d, pool, nullptr);
			if (view)
				fn.vkDestroyImageView(d, view, nullptr);
			if (color)
				fn.vkDestroyImage(d, color, nullptr);
			if (colorMem)
				fn.vkFreeMemory(d, colorMem, nullptr);
			if (staging)
				fn.vkDestroyBuffer(d, staging, nullptr);
			if (stagingMem)
				fn.vkFreeMemory(d, stagingMem, nullptr);
		}
	} off{fn, dev.device()};
	const VkDevice d = off.d;
	const auto fail = [&result](const char *why) {
		result.failureReason = why;
		return result;
	};

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
		if (fn.vkCreateImage(d, &ici, nullptr, &off.color) != VK_SUCCESS)
			return fail("offscreen image creation failed");
		VkMemoryRequirements req{};
		fn.vkGetImageMemoryRequirements(d, off.color, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (type == ~0u)
			return fail("no device-local memory for offscreen image");
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &off.colorMem) != VK_SUCCESS)
			return fail("offscreen memory allocation failed");
		if (fn.vkBindImageMemory(d, off.color, off.colorMem, 0) != VK_SUCCESS)
			return fail("offscreen memory bind failed");
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = off.color;
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = VK_FORMAT_R8G8B8A8_UNORM;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		if (fn.vkCreateImageView(d, &vci, nullptr, &off.view) != VK_SUCCESS)
			return fail("offscreen view creation failed");
	}

	if (!renderWorldViewInto(dev, draws, camera, off.view, VK_FORMAT_R8G8B8A8_UNORM,
				width, height, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, result.drawnQuads,
				result.failureReason))
		return result;

	// Readback submission.
	{
		const VkDeviceSize bytes = VkDeviceSize{width} * height * 4;
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = bytes;
		bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (fn.vkCreateBuffer(d, &bci, nullptr, &off.staging) != VK_SUCCESS)
			return fail("staging buffer creation failed");
		VkMemoryRequirements req{};
		fn.vkGetBufferMemoryRequirements(d, off.staging, &req);
		const std::uint32_t type = findMemoryType(memProps, req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (type == ~0u)
			return fail("no host-visible memory for staging");
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = req.size;
		mai.memoryTypeIndex = type;
		if (fn.vkAllocateMemory(d, &mai, nullptr, &off.stagingMem) != VK_SUCCESS)
			return fail("staging memory allocation failed");
		if (fn.vkBindBufferMemory(d, off.staging, off.stagingMem, 0) != VK_SUCCESS)
			return fail("staging memory bind failed");
		if (fn.vkMapMemory(d, off.stagingMem, 0, bytes, 0, &off.ptr) != VK_SUCCESS)
			return fail("staging map failed");
		off.mapped = true;

		VkCommandPoolCreateInfo cpci{};
		cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpci.queueFamilyIndex = dev.graphicsQueueFamily();
		if (fn.vkCreateCommandPool(d, &cpci, nullptr, &off.pool) != VK_SUCCESS)
			return fail("readback pool creation failed");
		VkCommandBufferAllocateInfo cbai{};
		cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbai.commandPool = off.pool;
		cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbai.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (fn.vkAllocateCommandBuffers(d, &cbai, &cmd) != VK_SUCCESS)
			return fail("readback command buffer allocation failed");
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (fn.vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS)
			return fail("readback begin failed");
		VkBufferImageCopy region{};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {width, height, 1};
		fn.vkCmdCopyImageToBuffer(cmd, off.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				off.staging, 1, &region);
		if (fn.vkEndCommandBuffer(cmd) != VK_SUCCESS)
			return fail("readback end failed");
		VkFenceCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		if (fn.vkCreateFence(d, &fci, nullptr, &off.fence) != VK_SUCCESS)
			return fail("readback fence creation failed");
		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		if (fn.vkQueueSubmit(dev.graphicsQueue(), 1, &submit, off.fence) != VK_SUCCESS)
			return fail("readback submit failed");
		if (fn.vkWaitForFences(d, 1, &off.fence, VK_TRUE, ~0ull) != VK_SUCCESS)
			return fail("readback fence wait failed");

		result.pixels.resize(static_cast<std::size_t>(bytes));
		std::memcpy(result.pixels.data(), off.ptr, result.pixels.size());
	}

	result.ran = true;
	return result;
}

} // namespace vk
