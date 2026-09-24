#include "Vulkan2dRender.h"
#include "ShaderProgram.h"
#include <regex>

Vulkan2dRender::Vulkan2dRender()
{
	m_init = false;
	prev_pipeline = -1;
}

Vulkan2dRender::Vulkan2dRender(std::shared_ptr<VVulkan> vulkan)
{
	init(vulkan);
}

void Vulkan2dRender::init(std::shared_ptr<VVulkan> vulkan)
{
	m_vulkan = vulkan;

	generateQuad();
	setupVertexDescriptions();

	prev_pipeline = -1;
}

Vulkan2dRender::~Vulkan2dRender()
{
	if (m_vulkan)
	{
		VkDevice dev = m_vulkan->vulkanDevice->logicalDevice;
		for (auto& p : m_pipelines)
			vkDestroyPipeline(dev, p.vkpipeline, nullptr);
		//render passes are shared between pipelines via the cache: destroy each exactly once
		for (auto& rp : m_renderpass_cache)
			vkDestroyRenderPass(dev, rp.second, nullptr);
		m_renderpass_cache.clear();

		m_vertexBuffer.destroy();
		m_indexBuffer.destroy();
	}
}

void Vulkan2dRender::generateQuad()
{
	// Setup vertices for a single uv-mapped quad made from two triangles
	std::vector<Vulkan2dRender::Vertex> quad =
	{
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }
	};

	// Setup indices
	std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };
	m_indexCount = static_cast<uint32_t>(indices.size());

	// Create buffers
	// For the sake of simplicity we won't stage the vertex data to the gpu memory
	// Vertex buffer
	VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		&m_vertexBuffer,
		quad.size() * sizeof(Vertex),
		quad.data()));
	// Index buffer
	VK_CHECK_RESULT(m_vulkan->vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		&m_indexBuffer,
		indices.size() * sizeof(uint32_t),
		indices.data()));
}

void Vulkan2dRender::setupVertexDescriptions()
{
	// Binding description
	m_vertices.inputBinding.resize(1);
	m_vertices.inputBinding[0] =
		vks::initializers::vertexInputBindingDescription(
		0, 
		sizeof(Vertex), 
		VK_VERTEX_INPUT_RATE_VERTEX);

	// Attribute descriptions
	// Describes memory layout and shader positions
	m_vertices.inputAttributes.resize(2);
	// Location 0 : Position
	m_vertices.inputAttributes[0] =
		vks::initializers::vertexInputAttributeDescription(
		0,
		0,
		VK_FORMAT_R32G32B32_SFLOAT,
		offsetof(Vertex, pos));			
	// Location 1 : Texture coordinates
	m_vertices.inputAttributes[1] =
		vks::initializers::vertexInputAttributeDescription(
		0,
		1,
		VK_FORMAT_R32G32B32_SFLOAT,
		offsetof(Vertex, uv));

	m_vertices.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	m_vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices.inputBinding.size());
	m_vertices.inputState.pVertexBindingDescriptions = m_vertices.inputBinding.data();
	m_vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices.inputAttributes.size());
	m_vertices.inputState.pVertexAttributeDescriptions = m_vertices.inputAttributes.data();

	
	// Binding description
	m_vertices34.inputBinding.resize(1);
	m_vertices34.inputBinding[0] =
		vks::initializers::vertexInputBindingDescription(
			0,
			sizeof(Vertex34),
			VK_VERTEX_INPUT_RATE_VERTEX);

	// Attribute descriptions
	// Describes memory layout and shader positions
	m_vertices34.inputAttributes.resize(2);
	// Location 0 : Position
	m_vertices34.inputAttributes[0] =
		vks::initializers::vertexInputAttributeDescription(
			0,
			0,
			VK_FORMAT_R32G32B32_SFLOAT,
			offsetof(Vertex34, pos));
	// Location 1 : Texture coordinates
	m_vertices34.inputAttributes[1] =
		vks::initializers::vertexInputAttributeDescription(
			0,
			1,
			VK_FORMAT_R32G32B32A32_SFLOAT,
			offsetof(Vertex34, uv));

	m_vertices34.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	m_vertices34.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices34.inputBinding.size());
	m_vertices34.inputState.pVertexBindingDescriptions = m_vertices34.inputBinding.data();
	m_vertices34.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices34.inputAttributes.size());
	m_vertices34.inputState.pVertexAttributeDescriptions = m_vertices34.inputAttributes.data();
}

VkRenderPass Vulkan2dRender::prepareRenderPass(VkFormat framebuf_format, int attachment_num, bool isSwapChainImage)
{
	auto key = std::make_tuple(framebuf_format, attachment_num, isSwapChainImage);
	auto cached = m_renderpass_cache.find(key);
	if (cached != m_renderpass_cache.end())
		return cached->second;

	VkRenderPass pass = VK_NULL_HANDLE;

	VkPhysicalDevice physicalDevice = m_vulkan->getPhysicalDevice();
	VkDevice device = m_vulkan->getDevice();

	// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering
	std::vector<VkAttachmentDescription> attchmentDescriptions = {};
	std::vector<VkAttachmentReference> colorReferences;
	for (int i = 0; i < attachment_num; i++)
	{
		// Color attachment
		VkAttachmentDescription attd = {};
		attd.format = framebuf_format;
		attd.samples = VK_SAMPLE_COUNT_1_BIT;
		attd.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attd.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attd.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attd.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attd.initialLayout = isSwapChainImage ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attd.finalLayout = isSwapChainImage ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attchmentDescriptions.push_back(attd);

		VkAttachmentReference colref = { (uint32_t)i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		colorReferences.push_back(colref);
	}
	
	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescription.pColorAttachments = colorReferences.data();

	// Use subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Create the actual renderpass
	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
	renderPassInfo.pAttachments = attchmentDescriptions.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &pass));

	m_renderpass_cache[key] = pass;

	return pass;
}

Vulkan2dRender::V2dPipeline Vulkan2dRender::preparePipeline(
	int shader,
	int blend_mode,
	VkFormat framebuf_format,
	int attachment_num,
	int colormap,
	bool isSwapChainImage,
	VkPrimitiveTopology topology,
	VkPolygonMode polymode,
	VkCullModeFlags cullmode,
	VkFrontFace frontface
)
{
	if (prev_pipeline >= 0) {
		if (m_pipelines[prev_pipeline].shader == shader &&
			m_pipelines[prev_pipeline].blend == blend_mode &&
			m_pipelines[prev_pipeline].colormap == colormap &&
			m_pipelines[prev_pipeline].framebuf_format == framebuf_format &&
			m_pipelines[prev_pipeline].attachment_num == attachment_num &&
			m_pipelines[prev_pipeline].isSwapChainImage == isSwapChainImage &&
			m_pipelines[prev_pipeline].topology == topology &&
			m_pipelines[prev_pipeline].polymode == polymode &&
			m_pipelines[prev_pipeline].cullmode == cullmode &&
			m_pipelines[prev_pipeline].frontface == frontface)
			return m_pipelines[prev_pipeline];
	}
	for (int i = 0; i < m_pipelines.size(); i++) {
		if (m_pipelines[i].shader == shader &&
			m_pipelines[i].blend == blend_mode &&
			m_pipelines[i].colormap == colormap &&
			m_pipelines[i].framebuf_format == framebuf_format &&
			m_pipelines[i].attachment_num == attachment_num &&
			m_pipelines[i].isSwapChainImage == isSwapChainImage &&
			m_pipelines[i].topology == topology &&
			m_pipelines[i].polymode == polymode &&
			m_pipelines[i].cullmode == cullmode &&
			m_pipelines[i].frontface == frontface)
		{
			prev_pipeline = i;
			return m_pipelines[i];
		}
	}

	float linewidth = 1.0f;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(
			topology,
			0,
			VK_FALSE
		);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			polymode,
			cullmode,
			frontface,
			0
		);
	rasterizationState.lineWidth = linewidth;

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(
		VK_FALSE,
		VK_FALSE,
		VK_COMPARE_OP_NEVER);

	VkPipelineViewportStateCreateInfo viewportState =
		vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(
		VK_SAMPLE_COUNT_1_BIT,
		0);

	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState =
		vks::initializers::pipelineDynamicStateCreateInfo(
		dynamicStateEnables.data(),
		static_cast<uint32_t>(dynamicStateEnables.size()),
		0);

	VkRenderPass pass = prepareRenderPass(framebuf_format, attachment_num, isSwapChainImage);

	m_img_pipeline_settings = m_vulkan->img_shader_factory_->pipeline_settings_[m_vulkan->vulkanDevice];
	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(
		m_img_pipeline_settings.pipelineLayout,
		pass,
		0);

	VkPipelineVertexInputStateCreateInfo *inputState = nullptr;
	switch (shader)
	{
	case IMG_SHDR_DRAW_GEOMETRY_COLOR4:
		inputState = &m_vertices34.inputState;
		break;
	default:
		inputState = &m_vertices.inputState;
	}

	pipelineCreateInfo.pVertexInputState = inputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pDynamicState = &dynamicState;
	pipelineCreateInfo.stageCount = 2;
	
	//blend mode
	VkBool32 enable_blend;
	VkBlendOp blend_op;
	VkBlendFactor src_blend, dst_blend;
	switch(blend_mode)
	{
	case V2DRENDER_BLEND_OVER:
		enable_blend = VK_TRUE;
		blend_op = VK_BLEND_OP_ADD;
		src_blend = VK_BLEND_FACTOR_ONE;
		dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case V2DRENDER_BLEND_OVER_INV:
		enable_blend = VK_TRUE;
		blend_op = VK_BLEND_OP_ADD;
		src_blend = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		dst_blend = VK_BLEND_FACTOR_ONE;
		break;
	case V2DRENDER_BLEND_OVER_UI:
		enable_blend = VK_TRUE;
		blend_op = VK_BLEND_OP_ADD;
		src_blend = VK_BLEND_FACTOR_SRC_ALPHA;
		dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case V2DRENDER_BLEND_ADD:
		enable_blend = VK_TRUE;
		blend_op = VK_BLEND_OP_ADD;
		src_blend = VK_BLEND_FACTOR_ONE;
		dst_blend = VK_BLEND_FACTOR_ONE;
		break;
	case V2DRENDER_BLEND_SHADE_SHADOW:
		enable_blend = VK_TRUE;
		blend_op = VK_BLEND_OP_ADD;
		src_blend = VK_BLEND_FACTOR_ZERO;
		dst_blend = VK_BLEND_FACTOR_SRC_COLOR;
		break;
	case V2DRENDER_BLEND_MAX:
		enable_blend = VK_TRUE;
		blend_op = VK_BLEND_OP_MAX;
		src_blend = VK_BLEND_FACTOR_ONE;
		dst_blend = VK_BLEND_FACTOR_ONE;
		break;
	default:
		enable_blend = VK_FALSE;
		blend_op = VK_BLEND_OP_MAX;
		src_blend = VK_BLEND_FACTOR_ONE;
		dst_blend = VK_BLEND_FACTOR_ZERO;
	}

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(
		enable_blend,
		blend_op,
		src_blend,
		dst_blend,
		0xf);
	
	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(
		1, 
		&blendAttachmentState);
	pipelineCreateInfo.pColorBlendState = &colorBlendState;

	// Load shaders
	std::array<VkPipelineShaderStageCreateInfo,2> shaderStages;
	FLIVR::ShaderProgram *sh = m_vulkan->img_shader_factory_->shader(m_vulkan->getDevice(), shader, colormap);
	shaderStages[0] = sh->get_vertex_shader();
	shaderStages[1] = sh->get_fragment_shader();
	pipelineCreateInfo.pStages = shaderStages.data();

	V2dPipeline v2d_pipeline;
	v2d_pipeline.shader = shader;
	v2d_pipeline.blend = blend_mode;
	v2d_pipeline.colormap = colormap;
	v2d_pipeline.pass = pass;
	v2d_pipeline.framebuf_format = framebuf_format;
	v2d_pipeline.attachment_num = attachment_num;
	v2d_pipeline.isSwapChainImage = isSwapChainImage;
	v2d_pipeline.topology = topology;
	v2d_pipeline.polymode = polymode;
	v2d_pipeline.cullmode = cullmode;
	v2d_pipeline.frontface = frontface;
	getEnabledUniforms(v2d_pipeline, sh->get_fragment_shader_code());

	VK_CHECK_RESULT(
		vkCreateGraphicsPipelines(m_vulkan->getDevice(), m_vulkan->getPipelineCache(), 1, &pipelineCreateInfo, nullptr, &v2d_pipeline.vkpipeline)
	);

	m_pipelines.push_back(v2d_pipeline);
	prev_pipeline = m_pipelines.size() - 1;

	return v2d_pipeline;
}

void Vulkan2dRender::getEnabledUniforms(V2dPipeline &pipeline, const std::string &code)
{
	std::vector<int> loc;
	std::vector<int> mat;
	std::vector<int> sampler;

	if (V2DRENDER_UNIFORM_VEC_NUM > 0) {
		std::vector<std::string> v = {};
		std::regex pt{ "vec4 loc[0-9]+" };
		std::sregex_iterator end, ite{ code.begin(), code.end(), pt };
		for (; ite != end; ++ite) {
			v.push_back(ite->str());
		}

		for (auto t : v) {
			size_t len = t.size();
			if (len < 2) continue;
			char d = t[len - 2];
			if (isdigit(d)) {
				int id = atoi(t.substr(len - 2, 2).c_str());
				loc.push_back(id);
			}
			else {
				int id = t[len - 1] - '0';
				loc.push_back(id);
			}
		}
	}

	if (V2DRENDER_UNIFORM_MAT_NUM > 0) {
		std::vector<std::string> v = {};
		std::regex pt{ "mat4 matrix[0-9]+" };
		std::sregex_iterator end, ite{ code.begin(), code.end(), pt };
		for (; ite != end; ++ite) {
			v.push_back(ite->str());
		}

		for (auto t : v) {
			size_t len = t.size();
			if (len < 2) continue;
			char d = t[len - 2];
			if (isdigit(d)) {
				int id = atoi(t.substr(len - 2, 2).c_str());
				mat.push_back(id);
			}
			else {
				int id = t[len - 1] - '0';
				mat.push_back(id);
			}
		}
	}

	if (IMG_SHDR_SAMPLER_NUM > 0) {
		std::vector<std::string> v = {};
		std::regex pt{ "sampler[2-3]D tex[0-9]+" };
		std::sregex_iterator end, ite{ code.begin(), code.end(), pt };
		for (; ite != end; ++ite) {
			v.push_back(ite->str());
		}

		for (auto t : v) {
			size_t len = t.size();
			if (len < 2) continue;
			char d = t[len - 2];
			if (isdigit(d)) {
				int id = atoi(t.substr(len - 2, 2).c_str());
				sampler.push_back(id);
			}
			else {
				int id = t[len - 1] - '0';
				sampler.push_back(id);
			}
		}
	}

	for (auto i : loc) {
		if ( i < V2DRENDER_UNIFORM_VEC_NUM )
			pipeline.uniforms[i] = VK_TRUE;
	}
	for (auto i : mat) {
		if ( i < V2DRENDER_UNIFORM_MAT_NUM )
			pipeline.uniforms[i+V2DRENDER_UNIFORM_VEC_NUM] = VK_TRUE;
	}
	for (auto i : sampler) {
		if ( i < IMG_SHDR_SAMPLER_NUM )
			pipeline.samplers[i] = VK_TRUE;
	}
}

void Vulkan2dRender::setupDescriptorSetWrites(const V2DRenderParams& params, const V2dPipeline& pipeline, std::vector<VkWriteDescriptorSet>& descriptorWrites)
{
	for (int i = 0; i < IMG_SHDR_SAMPLER_NUM; i++) {
		if (params.tex[i] && pipeline.samplers[i]) {
			descriptorWrites.push_back(
				vks::initializers::writeDescriptorSet(
					VK_NULL_HANDLE,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					i,
					&params.tex[i]->descriptor,
					1
				)
			);
		}
	}
}

void Vulkan2dRender::buildCommandBuffer(
	VkCommandBuffer commandbufs[],
	int commandbuf_num,
	const std::unique_ptr<vks::VFrameBuffer> &framebuf,
	const V2DRenderParams &params)
{
	if (!commandbufs || commandbuf_num <= 0)
		return;

	std::vector<VkWriteDescriptorSet> descriptorWrites;
	setupDescriptorSetWrites(params, params.pipeline, descriptorWrites);

	uint64_t constsize = 0;
	for (int32_t i = 0; i < V2DRENDER_UNIFORM_VEC_NUM; i++) {
		if (params.pipeline.uniforms[i]) {
			memcpy(constant_buf+constsize, &params.loc[i], sizeof(glm::vec4));
			constsize += sizeof(glm::vec4);
		}
	}
	for (int32_t i = V2DRENDER_UNIFORM_VEC_NUM; i < V2DRENDER_UNIFORM_NUM; i++) {
		if (params.pipeline.uniforms[i]) {
			memcpy(constant_buf+constsize, &params.matrix[i-V2DRENDER_UNIFORM_VEC_NUM], sizeof(glm::mat4));
			constsize += sizeof(glm::mat4);
		}
	}

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkClearValue clearValues[1];
	clearValues[0].color = params.clearColor;

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = params.pipeline.pass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = framebuf->w;
	renderPassBeginInfo.renderArea.extent.height = framebuf->h;

	VkImageLayout layout[IMG_SHDR_SAMPLER_NUM];

	for (int32_t i = 0; i < commandbuf_num; ++i)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandbufs[i], &cmdBufInfo));

		for (int j = 0; j < IMG_SHDR_SAMPLER_NUM; j++)
		{
			if (params.tex[j] && params.pipeline.samplers[j])
			{
				VkImageLayout src = params.tex[j]->descriptor.imageLayout;
				VkImageLayout dst = VK_IMAGE_LAYOUT_UNDEFINED;
				layout[j] = params.tex[j]->descriptor.imageLayout;
				switch (params.tex[j]->descriptor.imageLayout) {
				case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
					dst = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
					break;
				case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
					dst = VK_IMAGE_LAYOUT_GENERAL;
					break;
				case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
					dst = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					break;
				}

				if (dst != VK_IMAGE_LAYOUT_UNDEFINED)
				{
					params.tex[j]->descriptor.imageLayout = dst;
					//attachment write -> sampled read: explicit stages instead of the
					//ALL_COMMANDS default, so unrelated GPU work can overlap the barrier
					vks::tools::setImageLayout(
						commandbufs[i],
						params.tex[j]->image,
						src,
						dst,
						params.tex[j]->subresourceRange,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
				}
			}
			else
			{
				layout[j] = VK_IMAGE_LAYOUT_UNDEFINED;
			}
		}
		
		vkCmdBeginRenderPass(commandbufs[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(commandbufs[i], 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(commandbufs[i], 0, 1, &scissor);

		if (!descriptorWrites.empty())
		{
			m_vulkan->vulkanDevice->vkCmdPushDescriptorSetKHR(
				commandbufs[i],
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				m_img_pipeline_settings.pipelineLayout,
				0,
				descriptorWrites.size(),
				descriptorWrites.data());
		}

		if (params.clear)
		{
			VkClearAttachment clearAttachments[1] = {};
			clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			clearAttachments[0].clearValue = clearValues[0];
			clearAttachments[0].colorAttachment = 0;

			VkClearRect clearRect = {};
			clearRect.layerCount = 1;
			clearRect.rect.offset = { 0, 0 };
			clearRect.rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				commandbufs[i],
				1,
				clearAttachments,
				1,
				&clearRect);
		}

		vkCmdBindPipeline(commandbufs[i], VK_PIPELINE_BIND_POINT_GRAPHICS, params.pipeline.vkpipeline);

		//push constants
		if (constsize > 0)
		{
			vkCmdPushConstants(
				commandbufs[i],
				m_img_pipeline_settings.pipelineLayout,
				VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_VERTEX_BIT,
				0,
				constsize,
				constant_buf);
		}

		if (!params.obj)
		{
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(commandbufs[i], 0, 1, &m_vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(commandbufs[i], m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(commandbufs[i], m_indexCount, 1, 0, 0, 0);
		}	
		else
		{
			vkCmdBindVertexBuffers(commandbufs[i], 0, 1, &params.obj->vertBuf.buffer, &params.obj->vertOffset);
			vkCmdBindIndexBuffer(commandbufs[i], params.obj->idxBuf.buffer, params.obj->idxOffset, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(
				commandbufs[i],
				params.render_idxCount > 0 ? params.render_idxCount : params.obj->idxCount,
				1, 
				params.render_idxBase,
				0,
				0
			);
		}

		vkCmdEndRenderPass(commandbufs[i]);

		for (int j = 0; j < IMG_SHDR_SAMPLER_NUM; j++)
		{
			if (params.tex[j] && params.pipeline.samplers[j])
			{
				if (layout[j] != params.tex[j]->descriptor.imageLayout)
				{
					//sampled read -> attachment write: explicit stages (see pre-pass note)
					vks::tools::setImageLayout(
						commandbufs[i],
						params.tex[j]->image,
						params.tex[j]->descriptor.imageLayout,
						layout[j],
						params.tex[j]->subresourceRange,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
					params.tex[j]->descriptor.imageLayout = layout[j];
				}
			}
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(commandbufs[i]));
	}
}

void Vulkan2dRender::render(const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams& params)
{
	VkCommandBuffer default_cmdbuf = m_vulkan->vulkanDevice->GetNextCommandBuffer();
	buildCommandBuffer(&default_cmdbuf, 1, framebuf, params);
	
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	std::vector<VkPipelineStageFlags> waitStages;
	for (uint32_t i = 0; i < params.waitSemaphoreCount; i++)
		waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &default_cmdbuf;
	submitInfo.waitSemaphoreCount = params.waitSemaphoreCount;
	submitInfo.pWaitSemaphores = params.waitSemaphores;
	if (!waitStages.empty())
		submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.signalSemaphoreCount = params.signalSemaphoreCount;
	submitInfo.pSignalSemaphores = params.signalSemaphores;

	// Submit to the queue
	VK_CHECK_RESULT(vkQueueSubmit(m_vulkan->vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE));
}

void Vulkan2dRender::seq_buildCommandBuffer(VkCommandBuffer commandbufs[], int commandbuf_num, const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams* params, int num)
{
	if (!commandbufs || commandbuf_num <= 0)
		return;

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkClearValue clearValues[1];
	clearValues[0].color = params[0].clearColor;

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = params[0].pipeline.pass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = framebuf->w;
	renderPassBeginInfo.renderArea.extent.height = framebuf->h;

	for (int32_t i = 0; i < commandbuf_num; ++i)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandbufs[i], &cmdBufInfo));

		vkCmdBeginRenderPass(commandbufs[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(commandbufs[i], 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(commandbufs[i], 0, 1, &scissor);

		if (params[0].clear)
		{
			VkClearAttachment clearAttachments[1] = {};
			clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			clearAttachments[0].clearValue = clearValues[0];
			clearAttachments[0].colorAttachment = 0;

			VkClearRect clearRect = {};
			clearRect.layerCount = 1;
			clearRect.rect.offset = { 0, 0 };
			clearRect.rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				commandbufs[i],
				1,
				clearAttachments,
				1,
				&clearRect);
		}

		VkPipeline prev_pl = VK_NULL_HANDLE;
		for (int s = 0; s < num; s++)
		{
			VkImageLayout layout[IMG_SHDR_SAMPLER_NUM];
			for (int j = 0; j < IMG_SHDR_SAMPLER_NUM; j++)
			{
				if (params[s].tex[j] && params[s].pipeline.samplers[j])
				{
					VkImageLayout src = params[s].tex[j]->descriptor.imageLayout;
					VkImageLayout dst = VK_IMAGE_LAYOUT_UNDEFINED;
					layout[j] = params[s].tex[j]->descriptor.imageLayout;
					switch (params[s].tex[j]->descriptor.imageLayout) {
					case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
						dst = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
						break;
					case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
						dst = VK_IMAGE_LAYOUT_GENERAL;
						break;
					case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
						dst = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						break;
					}

					if (dst != VK_IMAGE_LAYOUT_UNDEFINED)
					{
						params[s].tex[j]->descriptor.imageLayout = dst;
						vks::tools::setImageLayout(
							commandbufs[i],
							params[s].tex[j]->image,
							src,
							dst,
							params[s].tex[j]->subresourceRange);
					}
				}
				else
				{
					layout[j] = VK_IMAGE_LAYOUT_UNDEFINED;
				}
			}

			std::vector<VkWriteDescriptorSet> descriptorWrites;
			setupDescriptorSetWrites(params[s], params[s].pipeline, descriptorWrites);

			uint64_t constsize = 0;
			for (int32_t i = 0; i < V2DRENDER_UNIFORM_VEC_NUM; i++) {
				if (params[s].pipeline.uniforms[i]) {
					memcpy(constant_buf + constsize, &params[s].loc[i], sizeof(glm::vec4));
					constsize += sizeof(glm::vec4);
				}
			}
			for (int32_t i = V2DRENDER_UNIFORM_VEC_NUM; i < V2DRENDER_UNIFORM_NUM; i++) {
				if (params[s].pipeline.uniforms[i]) {
					memcpy(constant_buf + constsize, &params[s].matrix[i - V2DRENDER_UNIFORM_VEC_NUM], sizeof(glm::mat4));
					constsize += sizeof(glm::mat4);
				}
			}

			if (!descriptorWrites.empty())
			{
				m_vulkan->vulkanDevice->vkCmdPushDescriptorSetKHR(
					commandbufs[i],
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					m_img_pipeline_settings.pipelineLayout,
					0,
					descriptorWrites.size(),
					descriptorWrites.data());
			}

			if (prev_pl != params[s].pipeline.vkpipeline)
				vkCmdBindPipeline(commandbufs[i], VK_PIPELINE_BIND_POINT_GRAPHICS, params[s].pipeline.vkpipeline);
			prev_pl = params[s].pipeline.vkpipeline;

			//push constants
			if (constsize > 0)
			{
				vkCmdPushConstants(
					commandbufs[i],
					m_img_pipeline_settings.pipelineLayout,
					VK_SHADER_STAGE_FRAGMENT_BIT| VK_SHADER_STAGE_VERTEX_BIT,
					0,
					constsize,
					constant_buf);
			}

			if (!params[s].obj)
			{
				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(commandbufs[i], 0, 1, &m_vertexBuffer.buffer, offsets);
				vkCmdBindIndexBuffer(commandbufs[i], m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(commandbufs[i], m_indexCount, 1, 0, 0, 0);
			}
			else
			{
				vkCmdBindVertexBuffers(commandbufs[i], 0, 1, &params[s].obj->vertBuf.buffer, &params[s].obj->vertOffset);
				vkCmdBindIndexBuffer(commandbufs[i], params[s].obj->idxBuf.buffer, params[s].obj->idxOffset, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(
					commandbufs[i], 
					params[s].render_idxCount > 0 ? params[s].render_idxCount : params[s].obj->idxCount,
					1,
					params[s].render_idxBase,
					0,
					0
				);
			}
			
			for (int j = 0; j < IMG_SHDR_SAMPLER_NUM; j++)
			{
				if (params[s].tex[j] && params[s].pipeline.samplers[j])
				{
					if (layout[j] != params[s].tex[j]->descriptor.imageLayout)
					{
						vks::tools::setImageLayout(
							commandbufs[i],
							params[s].tex[j]->image,
							params[s].tex[j]->descriptor.imageLayout,
							layout[j],
							params[s].tex[j]->subresourceRange);
						params[s].tex[j]->descriptor.imageLayout = layout[j];
					}
				}
			}
		}

		vkCmdEndRenderPass(commandbufs[i]);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandbufs[i]));
	}
}

void Vulkan2dRender::seq_render(const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams* params, int num)
{
	VkCommandBuffer default_cmdbuf = m_vulkan->vulkanDevice->GetNextCommandBuffer();
	seq_buildCommandBuffer(&default_cmdbuf, 1, framebuf, params, num);

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	std::vector<VkPipelineStageFlags> waitStages;
	for (uint32_t i = 0; i < params[0].waitSemaphoreCount; i++)
		waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &default_cmdbuf;
	submitInfo.waitSemaphoreCount = params[0].waitSemaphoreCount;
	submitInfo.pWaitSemaphores = params[0].waitSemaphores;
	if (!waitStages.empty())
		submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.signalSemaphoreCount = params[0].signalSemaphoreCount;
	submitInfo.pSignalSemaphores = params[0].signalSemaphores;

	// Submit to the queue
	VK_CHECK_RESULT(vkQueueSubmit(m_vulkan->vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE));
}

void Vulkan2dRender::buildCommandBufferClear(
	VkCommandBuffer commandbufs[],
	int commandbuf_num,
	const std::unique_ptr<vks::VFrameBuffer>& framebuf,
	const V2DRenderParams& params)
{
	if (!commandbufs || commandbuf_num <= 0)
		return;

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
	cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VkClearValue clearValues[1];
	clearValues[0].color = params.clearColor;

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = framebuf->renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = framebuf->w;
	renderPassBeginInfo.renderArea.extent.height = framebuf->h;

	for (int32_t i = 0; i < commandbuf_num; ++i)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandbufs[i], &cmdBufInfo));

		vkCmdBeginRenderPass(commandbufs[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(commandbufs[i], 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(commandbufs[i], 0, 1, &scissor);

		VkClearAttachment clearAttachments[1] = {};
		clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clearAttachments[0].clearValue = clearValues[0];
		clearAttachments[0].colorAttachment = 0;

		VkClearRect clearRect = {};
		clearRect.layerCount = 1;
		clearRect.rect.offset = { 0, 0 };
		clearRect.rect.extent = { framebuf->w, framebuf->h };

		vkCmdClearAttachments(
			commandbufs[i],
			1,
			clearAttachments,
			1,
			&clearRect);

		vkCmdEndRenderPass(commandbufs[i]);

		VK_CHECK_RESULT(vkEndCommandBuffer(commandbufs[i]));
	}
}

void Vulkan2dRender::clear(const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams& params)
{
	VkCommandBuffer default_cmdbuf = m_vulkan->vulkanDevice->GetNextCommandBuffer();
	buildCommandBufferClear(&default_cmdbuf, 1, framebuf, params);

	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	std::vector<VkPipelineStageFlags> waitStages;
	for (uint32_t i = 0; i < params.waitSemaphoreCount; i++)
		waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &default_cmdbuf;
	submitInfo.waitSemaphoreCount = params.waitSemaphoreCount;
	submitInfo.pWaitSemaphores = params.waitSemaphores;
	if (!waitStages.empty())
		submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.signalSemaphoreCount = params.signalSemaphoreCount;
	submitInfo.pSignalSemaphores = params.signalSemaphores;

	// Submit to the queue
	VK_CHECK_RESULT(vkQueueSubmit(m_vulkan->vulkanDevice->queue, 1, &submitInfo, VK_NULL_HANDLE));
}

Vulkan2dRender::V2DRenderParams Vulkan2dRender::GetNextV2dRenderSemaphoreSettings()
{
	Vulkan2dRender::V2DRenderParams ret;

	VkSemaphore* cur = m_vulkan->vulkanDevice->GetCurrentRenderSemaphore();
	if (cur)
	{
		ret.waitSemaphores = cur;
		ret.waitSemaphoreCount = 1;
	}

	VkSemaphore* next = m_vulkan->vulkanDevice->GetNextRenderSemaphore();
	if (next)
	{
		ret.signalSemaphores = next;
		ret.signalSemaphoreCount = 1;
	}

	return ret;
}

void Vulkan2dRender::GetNextV2dRenderSemaphoreSettings(Vulkan2dRender::V2DRenderParams& params)
{
	VkSemaphore* cur = m_vulkan->vulkanDevice->GetCurrentRenderSemaphore();
	if (cur)
	{
		params.waitSemaphores = cur;
		params.waitSemaphoreCount = 1;
	}

	VkSemaphore* next = m_vulkan->vulkanDevice->GetNextRenderSemaphore();
	if (next)
	{
		params.signalSemaphores = next;
		params.signalSemaphoreCount = 1;
	}
}
