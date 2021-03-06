//  
//  For more information, please see: http://software.sci.utah.edu
//  
//  The MIT License
//  
//  Copyright (c) 2004 Scientific Computing and Imaging Institute,
//  University of Utah.
//  
//  
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//  
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//  
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  

#include "MeshRenderer.h"
#include <iostream>
#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>

using namespace std;

namespace FLIVR
{
	std::vector<MeshRenderer::MeshPipeline> MeshRenderer::m_msh_pipelines;
	std::map<vks::VulkanDevice*, VkRenderPass> MeshRenderer::m_msh_draw_pass;
	std::map<vks::VulkanDevice*, VkRenderPass> MeshRenderer::m_msh_intdraw_pass;

	std::shared_ptr<VVulkan> MeshRenderer::m_vulkan;

	MeshRenderer::MeshRenderer(GLMmodel* data)
		: data_(data),
		depth_peel_(0),
		draw_clip_(false),
		limit_(-1),
		light_(true),
		fog_(false),
		alpha_(1.0),
		update_(true)
	{
		Plane* plane = 0;
		//x
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(1.0, 0.0, 0.0));
		planes_.push_back(plane);
		plane = new Plane(Point(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0));
		planes_.push_back(plane);
		//y
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0));
		planes_.push_back(plane);
		plane = new Plane(Point(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0));
		planes_.push_back(plane);
		//z
		plane = new Plane(Point(0.0, 0.0, 0.0), Vector(0.0, 0.0, 1.0));
		planes_.push_back(plane);
		plane = new Plane(Point(0.0, 0.0, 1.0), Vector(0.0, 0.0, -1.0));
		planes_.push_back(plane);
        
        planes_[0]->SetRange(planes_[0]->get_point(), planes_[0]->normal(), planes_[1]->get_point(), planes_[1]->normal());
        planes_[1]->SetRange(planes_[1]->get_point(), planes_[1]->normal(), planes_[0]->get_point(), planes_[0]->normal());
        planes_[2]->SetRange(planes_[2]->get_point(), planes_[2]->normal(), planes_[3]->get_point(), planes_[3]->normal());
        planes_[3]->SetRange(planes_[3]->get_point(), planes_[3]->normal(), planes_[2]->get_point(), planes_[2]->normal());
        planes_[4]->SetRange(planes_[4]->get_point(), planes_[4]->normal(), planes_[5]->get_point(), planes_[5]->normal());
        planes_[5]->SetRange(planes_[5]->get_point(), planes_[5]->normal(), planes_[4]->get_point(), planes_[4]->normal());

		setupVertexDescriptions();

		m_vin = nullptr;

		m_prev_msh_pipeline = -1;
	}

	MeshRenderer::MeshRenderer(MeshRenderer &copy)
		: data_(copy.data_),
		depth_peel_(copy.depth_peel_),
		draw_clip_(copy.draw_clip_),
		limit_(copy.limit_),
		light_(copy.light_),
		fog_(copy.fog_),
		alpha_(copy.alpha_),
		update_(true),
		bounds_(copy.bounds_),
		device_(copy.device_),
		depthformat_(copy.depthformat_)
	{
		//clipping planes
		for (int i=0; i<(int)copy.planes_.size(); i++)
		{
			Plane* plane = new Plane(*copy.planes_[i]);
			planes_.push_back(plane);
		}

		m_vertices4 = copy.m_vertices4;
		m_vertices44 = copy.m_vertices44;
		m_vertices42 = copy.m_vertices42;
		m_vertices442 = copy.m_vertices442;

		m_vin = copy.m_vin;

		m_prev_msh_pipeline = copy.m_prev_msh_pipeline;
	}

	MeshRenderer::~MeshRenderer()
	{
		//release clipping planes
		for (int i=0; i<(int)planes_.size(); i++)
		{
			if (planes_[i])
				delete planes_[i];
		}
		
		for (auto &vb : m_vertbufs)
		{
			vb.vertexBuffer.destroy();
			vb.indexBuffer.destroy();
		}

	}

	void MeshRenderer::init(std::shared_ptr<VVulkan> vulkan)
	{
		m_vulkan = vulkan;

		for (auto dev : m_vulkan->devices)
		{
			if (m_msh_draw_pass.count(dev) == 0)
				m_msh_draw_pass[dev] = prepareRenderPass(dev, VK_FORMAT_R32G32B32A32_SFLOAT, 1, false);
			if (m_msh_intdraw_pass.count(dev) == 0)
				m_msh_intdraw_pass[dev] = prepareRenderPass(dev, VK_FORMAT_R32_UINT, 1, false);
		}
	}

	void MeshRenderer::finalize()
	{
		if (m_vulkan)
		{
			for (auto dev : m_vulkan->devices)
			{
				vkDestroyRenderPass(dev->logicalDevice, m_msh_draw_pass[dev], nullptr);
				vkDestroyRenderPass(dev->logicalDevice, m_msh_intdraw_pass[dev], nullptr);
			}

			for (auto& p : m_msh_pipelines)
			{
				vkDestroyPipeline(p.device->logicalDevice, p.vkpipeline, nullptr);
			}
		}
	}

	void MeshRenderer::setupVertexDescriptions()
	{
		// Binding description
		m_vertices4.inputBinding.resize(1);
		m_vertices4.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex4),
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices4.inputAttributes.resize(1);
		// Location 0 : Position
		m_vertices4.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex4, pos));

		m_vertices4.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices4.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices4.inputBinding.size());
		m_vertices4.inputState.pVertexBindingDescriptions = m_vertices4.inputBinding.data();
		m_vertices4.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices4.inputAttributes.size());
		m_vertices4.inputState.pVertexAttributeDescriptions = m_vertices4.inputAttributes.data();

		// Binding description
		m_vertices42.inputBinding.resize(1);
		m_vertices42.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(float)*6,
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices42.inputAttributes.resize(2);
		// Location 0 : Position
		m_vertices42.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				0);
		// Location 1 : Texture
		m_vertices42.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float)*4);

		m_vertices42.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices42.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices42.inputBinding.size());
		m_vertices42.inputState.pVertexBindingDescriptions = m_vertices42.inputBinding.data();
		m_vertices42.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices42.inputAttributes.size());
		m_vertices42.inputState.pVertexAttributeDescriptions = m_vertices42.inputAttributes.data();

		// Binding description
		m_vertices44.inputBinding.resize(1);
		m_vertices44.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex44),
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices44.inputAttributes.resize(2);
		// Location 0 : Position
		m_vertices44.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex44, pos));
		// Location 1 : Normal
		m_vertices44.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex44, att));

		m_vertices44.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices44.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices44.inputBinding.size());
		m_vertices44.inputState.pVertexBindingDescriptions = m_vertices44.inputBinding.data();
		m_vertices44.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices44.inputAttributes.size());
		m_vertices44.inputState.pVertexAttributeDescriptions = m_vertices44.inputAttributes.data();

		// Binding description
		m_vertices442.inputBinding.resize(1);
		m_vertices442.inputBinding[0] =
			vks::initializers::vertexInputBindingDescription(
				0,
				sizeof(Vertex442),
				VK_VERTEX_INPUT_RATE_VERTEX);
		// Attribute descriptions
		// Describes memory layout and shader positions
		m_vertices442.inputAttributes.resize(3);
		// Location 0 : Position
		m_vertices442.inputAttributes[0] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				0,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex442, pos));
		m_vertices442.inputAttributes[1] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				1,
				VK_FORMAT_R32G32B32A32_SFLOAT,
				offsetof(Vertex442, att1));
		m_vertices442.inputAttributes[2] =
			vks::initializers::vertexInputAttributeDescription(
				0,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				offsetof(Vertex442, att2));

		m_vertices442.inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		m_vertices442.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(m_vertices442.inputBinding.size());
		m_vertices442.inputState.pVertexBindingDescriptions = m_vertices442.inputBinding.data();
		m_vertices442.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_vertices442.inputAttributes.size());
		m_vertices442.inputState.pVertexAttributeDescriptions = m_vertices442.inputAttributes.data();
	}

	VkRenderPass MeshRenderer::prepareRenderPass(vks::VulkanDevice* device, VkFormat framebuf_format, int attachment_num, bool isSwapChainImage)
	{
		VkRenderPass pass = VK_NULL_HANDLE;

		VkPhysicalDevice physicalDevice = device->physicalDevice;
		
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
		VkAttachmentDescription attd_d = {};
		attd_d.format = m_vulkan->depthFormat;
		attd_d.samples = VK_SAMPLE_COUNT_1_BIT;
		attd_d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attd_d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attd_d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attd_d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attd_d.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attd_d.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attchmentDescriptions.push_back(attd_d);
		
		VkAttachmentReference depthReference = {};
		depthReference.attachment = 1;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpassDescription.pColorAttachments = colorReferences.data();
		subpassDescription.pDepthStencilAttachment = &depthReference;

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

		VK_CHECK_RESULT(vkCreateRenderPass(device->logicalDevice, &renderPassInfo, nullptr, &pass));

		return pass;
	}

	MeshRenderer::MeshPipeline MeshRenderer::prepareMeshPipeline(vks::VulkanDevice* device, int type, int blend, bool tex, VkPrimitiveTopology topo, VkPolygonMode poly)
	{
		MeshPipeline ret_pipeline;

		ShaderProgram* shader = m_vulkan->msh_shader_factory_->shader(
			device->logicalDevice,
			type, depth_peel_, tex && data_->texcoords, fog_, light_ && (data_->normals || data_->facetnorms));

		if (m_prev_msh_pipeline >= 0) {
			if (m_msh_pipelines[m_prev_msh_pipeline].device == device &&
				m_msh_pipelines[m_prev_msh_pipeline].shader == shader &&
				m_msh_pipelines[m_prev_msh_pipeline].blend_mode == blend &&
				m_msh_pipelines[m_prev_msh_pipeline].topology == topo &&
				m_msh_pipelines[m_prev_msh_pipeline].polymode == poly)
				return m_msh_pipelines[m_prev_msh_pipeline];
		}
		for (int i = 0; i < m_msh_pipelines.size(); i++) {
			if (m_msh_pipelines[i].device == device &&
				m_msh_pipelines[i].shader == shader &&
				m_msh_pipelines[i].blend_mode == blend &&
				m_msh_pipelines[i].topology == topo &&
				m_msh_pipelines[i].polymode == poly)
			{
				m_prev_msh_pipeline = i;
				return m_msh_pipelines[i];
			}
		}

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::pipelineInputAssemblyStateCreateInfo(
				topo,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::pipelineRasterizationStateCreateInfo(
				poly,
				VK_CULL_MODE_NONE,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

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

		VkRenderPass renderpass = type != 1 ? m_msh_draw_pass[device] : m_msh_intdraw_pass[device];

		MshShaderFactory::MshPipelineSettings pipeline_settings = m_vulkan->msh_shader_factory_->pipeline_[device];
		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::pipelineCreateInfo(
				pipeline_settings.pipelineLayout,
				renderpass,
				0);

		pipelineCreateInfo.pVertexInputState = m_vin;
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

		enable_blend = VK_TRUE;
		switch (blend)
		{
		case MSHRENDER_BLEND_OVER:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE;
			dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case MSHRENDER_BLEND_OVER_INV:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
			dst_blend = VK_BLEND_FACTOR_ONE;
			break;
		case MSHRENDER_BLEND_OVER_UI:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_SRC_ALPHA;
			dst_blend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			break;
		case MSHRENDER_BLEND_ADD:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_ADD;
			src_blend = VK_BLEND_FACTOR_ONE;
			dst_blend = VK_BLEND_FACTOR_ONE;
			break;
		case MSHRENDER_BLEND_SHADE_SHADOW:
			enable_blend = VK_TRUE;
			blend_op = VK_BLEND_OP_MIN;
			src_blend = VK_BLEND_FACTOR_ZERO;
			dst_blend = VK_BLEND_FACTOR_SRC_COLOR;
			break;
		default:
			enable_blend = VK_FALSE;
			blend_op = VK_BLEND_OP_ADD;
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
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = shader->get_vertex_shader();
		shaderStages[1] = shader->get_fragment_shader();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(
			vkCreateGraphicsPipelines(
				device->logicalDevice,
				m_vulkan->getPipelineCache(),
				1,
				&pipelineCreateInfo,
				nullptr,
				&ret_pipeline.vkpipeline
			)
		);

		ret_pipeline.device = device;
		ret_pipeline.renderpass = renderpass;
		ret_pipeline.shader = shader;
		ret_pipeline.blend_mode = blend;
		ret_pipeline.topology = topo;
		ret_pipeline.polymode = poly;

		m_msh_pipelines.push_back(ret_pipeline);
		m_prev_msh_pipeline = m_msh_pipelines.size() - 1;

		return ret_pipeline;
	}

	//clipping planes
	void MeshRenderer::set_planes(vector<Plane*> *p)
	{
		int i;
		if (!planes_.empty())
		{
			for (i=0; i<(int)planes_.size(); i++)
			{
				if (planes_[i])
					delete planes_[i];
			}
			planes_.clear();
		}

		for (i=0; i<(int)p->size(); i++)
		{
			Plane *plane = new Plane(*(*p)[i]);
			planes_.push_back(plane);
		}
	}

	vector<Plane*>* MeshRenderer::get_planes()
	{
		return &planes_;
	}

	void MeshRenderer::update()
	{
		for (auto& vb : m_vertbufs)
		{
			vb.vertexBuffer.destroy();
			vb.indexBuffer.destroy();
			vb.indexCount = 0;
		}
		m_vertbufs.clear();
        
		bool bnormal = data_->normals || data_->facetnorms;
		bool btexcoord = data_->texcoords;

		if (bnormal && btexcoord)
			m_vin = &m_vertices442.inputState;
		else if (bnormal)
			m_vin = &m_vertices44.inputState;
		else if (btexcoord)
			m_vin = &m_vertices42.inputState;
		else
			m_vin = &m_vertices4.inputState;

		vector<float> verts;
		vector<uint32_t> ids;

		GLMgroup* group = data_->groups;
		GLMtriangle* triangle = 0;
		uint32_t count = 0;
		while (group)
		{
			verts.clear();
			ids.clear();
			for (size_t i=0; i<group->numtriangles; ++i)
			{
				triangle = &(data_->triangles[group->triangles[i]]);
				for (size_t j=0; j<3; ++j)
				{
					verts.push_back(data_->vertices[3*triangle->vindices[j]]);
					verts.push_back(data_->vertices[3*triangle->vindices[j]+1]);
					verts.push_back(data_->vertices[3*triangle->vindices[j]+2]);
					verts.push_back(1.0f);
					if (bnormal)
					{
                        if (data_->normals)
                        {
                            verts.push_back(data_->normals[3*triangle->nindices[j]]);
                            verts.push_back(data_->normals[3*triangle->nindices[j]+1]);
                            verts.push_back(data_->normals[3*triangle->nindices[j]+2]);
                            verts.push_back(0.0f);
                        }
                        else if (data_->facetnorms)
                        {
                            verts.push_back(data_->facetnorms[3*triangle->findex]);
                            verts.push_back(data_->facetnorms[3*triangle->findex+1]);
                            verts.push_back(data_->facetnorms[3*triangle->findex+2]);
                            verts.push_back(0.0f);
                        }
					}
					if (btexcoord)
					{
						verts.push_back(data_->texcoords[2*triangle->tindices[j]]);
						verts.push_back(data_->texcoords[2*triangle->tindices[j]+1]);
					}
					ids.push_back(ids.size());
				}
			}

			MeshVertexBuffers v;
			if (!verts.empty() && !ids.empty())
			{
				VK_CHECK_RESULT(device_->createBuffer(
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&v.vertexBuffer,
					verts.size() * sizeof(float)));
				device_->UploadData2Buffer(verts.data(), &v.vertexBuffer, 0, verts.size() * sizeof(float));

				VK_CHECK_RESULT(device_->createBuffer(
					VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&v.indexBuffer,
					ids.size() * sizeof(uint32_t)));
				device_->UploadData2Buffer(ids.data(), &v.indexBuffer, 0, ids.size() * sizeof(uint32_t));
			}

			v.indexCount = ids.size();

			m_vertbufs.push_back(v);

			group = group->next;
		}

	}

	void MeshRenderer::draw(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf)
	{
		if (!data_ || !data_->vertices || !data_->triangles)
			return;

		//set up vertex array object
		if (update_)
		{
			update();
			update_ = false;
		}

		bool tex = data_->hastexture;

		MeshPipeline pipeline = prepareMeshPipeline(device_, 0, MSHRENDER_BLEND_DISABLE, tex);
		VkPipelineLayout pipelineLayout = m_vulkan->msh_shader_factory_->pipeline_[device_].pipelineLayout;

		framebuf->replaceRenderPass(pipeline.renderpass);
		
		VkCommandBuffer cmdbuf = device_->GetNextCommandBuffer();

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = framebuf->w;
		renderPassBeginInfo.renderArea.extent.height = framebuf->h;
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

		size_t anum = framebuf->attachments.size();
		//layout transition
		if (depth_peel_)
		{
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				m_depth_tex->subresourceRange);
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

		if (clear_framebuf)
		{
			vector<VkClearAttachment> clearAttachments;
			clearAttachments.resize(anum);
			for (int i = 0; i < anum-1; i++)
			{
				clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[i].clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
				clearAttachments[i].colorAttachment = i;
			}
			clearAttachments[anum-1].aspectMask = framebuf->attachments[anum - 1]->subresourceRange.aspectMask;
			clearAttachments[anum-1].clearValue = { 1.0f, 0.0f, 0.0f, 0.0f };
			clearAttachments[anum-1].colorAttachment = 0;

			VkClearRect clearRect[1] = {};
			clearRect[0].layerCount = 1;
			clearRect[0].rect.offset = { 0, 0 };
			clearRect[0].rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				cmdbuf,
				anum,
				clearAttachments.data(),
				1,
				clearRect);
		}

		MshShaderFactory::MshVertShaderUBO vubo;
		MshShaderFactory::MshFragShaderUBO fubo;

		vubo.proj_mat = m_proj_mat;
		vubo.mv_mat = m_mv_mat;
		if (light_ && (data_->normals || data_->facetnorms))
			vubo.normal_mat = glm::mat4(glm::inverseTranspose(glm::mat3(m_mv_mat)));

		if (fog_)
			fubo.loc8 = { m_fog_intensity, m_fog_start, m_fog_end, 0.0f };

		if (depth_peel_)
			fubo.loc7 = { 1.0f / float(framebuf->w), 1.0f / float(framebuf->h), 0.0f, 0.0f };

		BBox dbox = bounds_;
		glm::mat4 gmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, float(dbox.min().x()),
			0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, float(dbox.min().y()),
			0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), float(dbox.max().z()),
			0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 invmat = glm::inverseTranspose(gmat);
		fubo.matrix3 = invmat;

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		fubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		fubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		fubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		fubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		fubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		fubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		GLMgroup* group = data_->groups;
		int count = 0;
		while (group)
		{
			if (m_vertbufs[count].indexCount == 0)
			{
				group = group->next;
				count++;
				continue;
			}

			vks::Buffer vert_ubuf, frag_ubuf;
			VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
			device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf, vert_ubuf_offset);
			device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf, frag_ubuf_offset);

			std::vector<VkWriteDescriptorSet> descriptorWritesBase;
			m_vulkan->msh_shader_factory_->getDescriptorSetWriteUniforms(device_, vert_ubuf, frag_ubuf, descriptorWritesBase);

			//uniforms
			if (light_ && (data_->normals || data_->facetnorms))
			{
				GLMmaterial* material = &data_->materials[group->material];
				if (material)
				{
					fubo.loc0 = { material->ambient[0], material->ambient[1], material->ambient[2], material->ambient[3] };
					fubo.loc1 = { material->diffuse[0], material->diffuse[1], material->diffuse[2], material->diffuse[3] };
					fubo.loc2 = { material->specular[0], material->specular[1], material->specular[2], material->specular[3] };
					fubo.loc3 = { material->shininess, alpha_, 0.0f, 0.0f };
				}
			}
			else
			{//color
				GLMmaterial* material = &data_->materials[group->material];
				if (material)
					fubo.loc0 = { material->diffuse[0], material->diffuse[1], material->diffuse[2], material->diffuse[3] };
				else
					fubo.loc0 = { 1.0f, 0.0f, 0.0f, 1.0f };//color
				fubo.loc3 = { 0.0f, alpha_, 0.0f, 0.0f };//alpha
			}
			//This won't happen now.
			if (tex && data_->texcoords)
			{
				GLMmaterial* material = &data_->materials[group->material];
				//descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, material->textureID))
			}
			if (depth_peel_)
			{
				descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 1, &m_depth_tex->descriptor));
			}

			vert_ubuf.copyTo(&vubo, sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf_offset);
			frag_ubuf.copyTo(&fubo, sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf_offset);
			if (vert_ubuf.buffer == frag_ubuf.buffer)
				vert_ubuf.flush(device_->GetCurrentUniformBufferOffset() - vert_ubuf_offset, vert_ubuf_offset);
			else
			{
				if (vert_ubuf_offset == 0)
					vert_ubuf.flush();
				else
					vert_ubuf.flush(VK_WHOLE_SIZE, vert_ubuf_offset);
				frag_ubuf.flush();
			}

			if (!descriptorWritesBase.empty())
			{
				device_->vkCmdPushDescriptorSetKHR(
					cmdbuf,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipelineLayout,
					0,
					descriptorWritesBase.size(),
					descriptorWritesBase.data()
				);
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[count].vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[count].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, m_vertbufs[count].indexCount, 1, 0, 0, 0);

			group = group->next;
			count++;
		}

		vkCmdEndRenderPass(cmdbuf);

		//layout transition
		if (depth_peel_)
		{
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				m_depth_tex->subresourceRange);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

		vks::VulkanSemaphoreSettings semaphores = device_->GetNextRenderSemaphoreSettings();

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdbuf;
		submitInfo.signalSemaphoreCount = semaphores.signalSemaphoreCount;
		submitInfo.pSignalSemaphores = semaphores.signalSemaphores;

		std::vector<VkPipelineStageFlags> waitStages;
		if (semaphores.waitSemaphoreCount > 0)
		{
			for (uint32_t i = 0; i < semaphores.waitSemaphoreCount; i++)
				waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			submitInfo.waitSemaphoreCount = semaphores.waitSemaphoreCount;
			submitInfo.pWaitSemaphores = semaphores.waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages.data();
		}

		// Submit to the queue
		VK_CHECK_RESULT(vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE));
		
	}

	void MeshRenderer::draw_wireframe(const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf)
	{
		if (!data_ || !data_->vertices || !data_->triangles)
			return;

		//set up vertex array object
		if (update_)
		{
			update();
			update_ = false;
		}

		bool tex = data_->hastexture;

		MeshPipeline pipeline = prepareMeshPipeline(device_, 0, MSHRENDER_BLEND_DISABLE, tex, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_POLYGON_MODE_LINE);
		VkPipelineLayout pipelineLayout = m_vulkan->msh_shader_factory_->pipeline_[device_].pipelineLayout;

		framebuf->replaceRenderPass(pipeline.renderpass);

		VkCommandBuffer cmdbuf = device_->GetNextCommandBuffer();

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = framebuf->w;
		renderPassBeginInfo.renderArea.extent.height = framebuf->h;
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

		//layout transition
		if (depth_peel_)
		{
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				m_depth_tex->subresourceRange);
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

		size_t anum = framebuf->attachments.size();
		if (clear_framebuf)
		{
			vector<VkClearAttachment> clearAttachments;
			clearAttachments.resize(anum);
			for (int i = 0; i < anum - 1; i++)
			{
				clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[i].clearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearAttachments[i].colorAttachment = i;
			}
			clearAttachments[anum - 1].aspectMask = framebuf->attachments[anum - 1]->subresourceRange.aspectMask;
			clearAttachments[anum - 1].clearValue = { 1.0f, 0.0f, 0.0f, 0.0f };
			clearAttachments[anum - 1].colorAttachment = 0;

			VkClearRect clearRect[1] = {};
			clearRect[0].layerCount = 1;
			clearRect[0].rect.offset = { 0, 0 };
			clearRect[0].rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				cmdbuf,
				anum,
				clearAttachments.data(),
				1,
				clearRect);
		}

		MshShaderFactory::MshVertShaderUBO vubo;
		MshShaderFactory::MshFragShaderUBO fubo;

		vubo.proj_mat = m_proj_mat;
		vubo.mv_mat = m_mv_mat;
		GLMmaterial* material = &data_->materials[0];
		if (material)
			fubo.loc0 = { material->diffuse[0], material->diffuse[1], material->diffuse[2], material->diffuse[3] };
		else
			fubo.loc0 = { 1.0f, 0.0f, 0.0f, 1.0f };//color
		fubo.loc3 = { 0.0f, alpha_, 0.0f, 0.0f };//alpha
		if (fog_)
			fubo.loc8 = { m_fog_intensity, m_fog_start, m_fog_end, 0.0f };

		if (depth_peel_)
			fubo.loc7 = { 1.0f / float(framebuf->w), 1.0f / float(framebuf->h), 0.0f, 0.0f };

		BBox dbox = bounds_;
        glm::mat4 gmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, float(dbox.min().x()),
                                   0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, float(dbox.min().y()),
                                   0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), float(dbox.max().z()),
                                   0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 invmat = glm::inverseTranspose(gmat);
		fubo.matrix3 = invmat;

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		fubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		fubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		fubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		fubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		fubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		fubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		vks::Buffer vert_ubuf, frag_ubuf;
		VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf, vert_ubuf_offset);
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf, frag_ubuf_offset);

		std::vector<VkWriteDescriptorSet> descriptorWritesBase;
		m_vulkan->msh_shader_factory_->getDescriptorSetWriteUniforms(device_, vert_ubuf, frag_ubuf, descriptorWritesBase);

		vert_ubuf.copyTo(&vubo, sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf_offset);
		frag_ubuf.copyTo(&fubo, sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf_offset);
		if (vert_ubuf.buffer == frag_ubuf.buffer)
			vert_ubuf.flush(device_->GetCurrentUniformBufferOffset() - vert_ubuf_offset, vert_ubuf_offset);
		else
		{
			if (vert_ubuf_offset == 0)
				vert_ubuf.flush();
			else
				vert_ubuf.flush(VK_WHOLE_SIZE, vert_ubuf_offset);
			frag_ubuf.flush();
		}
		if (depth_peel_)
			descriptorWritesBase.push_back(MshShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 1, &m_depth_tex->descriptor));

		if (!descriptorWritesBase.empty())
		{
			device_->vkCmdPushDescriptorSetKHR(
				cmdbuf,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				0,
				descriptorWritesBase.size(),
				descriptorWritesBase.data()
			);
		}

		GLMgroup* group = data_->groups;
		int count = 0;
		while (group)
		{
			if (m_vertbufs[count].indexCount == 0)
			{
				group = group->next;
				count++;
				continue;
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[count].vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[count].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, m_vertbufs[count].indexCount, 1, 0, 0, 0);

			group = group->next;
			count++;
		}

		vkCmdEndRenderPass(cmdbuf);

		//layout transition
		if (depth_peel_)
		{
			m_depth_tex->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			vks::tools::setImageLayout(
				cmdbuf,
				m_depth_tex->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				m_depth_tex->subresourceRange);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

		vks::VulkanSemaphoreSettings semaphores = device_->GetNextRenderSemaphoreSettings();

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdbuf;
		submitInfo.signalSemaphoreCount = semaphores.signalSemaphoreCount;
		submitInfo.pSignalSemaphores = semaphores.signalSemaphores;

		std::vector<VkPipelineStageFlags> waitStages;
		if (semaphores.waitSemaphoreCount > 0)
		{
			for (uint32_t i = 0; i < semaphores.waitSemaphoreCount; i++)
				waitStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
			submitInfo.waitSemaphoreCount = semaphores.waitSemaphoreCount;
			submitInfo.pWaitSemaphores = semaphores.waitSemaphores;
			submitInfo.pWaitDstStageMask = waitStages.data();
		}

		// Submit to the queue
		VK_CHECK_RESULT(vkQueueSubmit(device_->queue, 1, &submitInfo, VK_NULL_HANDLE));

	}

	void MeshRenderer::draw_integer(unsigned int name, const std::unique_ptr<vks::VFrameBuffer>& framebuf, bool clear_framebuf, VkRect2D scissor)
	{
		if (!data_ || !data_->vertices || !data_->triangles)
			return;

		//set up vertex array object
		if (update_)
		{
			update();
			update_ = false;
		}

		bool tex = data_->hastexture;

		MeshPipeline pipeline = prepareMeshPipeline(device_, 1, MSHRENDER_BLEND_DISABLE, false);
		VkPipelineLayout pipelineLayout = m_vulkan->msh_shader_factory_->pipeline_[device_].pipelineLayout;

		framebuf->replaceRenderPass(pipeline.renderpass);

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = framebuf->w;
		renderPassBeginInfo.renderArea.extent.height = framebuf->h;
		renderPassBeginInfo.framebuffer = framebuf->framebuffer;
		
		VkCommandBuffer cmdbuf = device_->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)framebuf->w, (float)framebuf->h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		if (scissor.extent.width == 0 || scissor.extent.height == 0)
			scissor = vks::initializers::rect2D(framebuf->w, framebuf->h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

		vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.vkpipeline);

		size_t anum = framebuf->attachments.size();
		if (clear_framebuf)
		{
			vector<VkClearAttachment> clearAttachments;
			clearAttachments.resize(anum);
			for (int i = 0; i < anum - 1; i++)
			{
				clearAttachments[i].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				clearAttachments[i].clearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
				clearAttachments[i].colorAttachment = i;
			}
			clearAttachments[anum - 1].aspectMask = framebuf->attachments[anum - 1]->subresourceRange.aspectMask;
			clearAttachments[anum - 1].clearValue = { 1.0f, 0.0f, 0.0f, 0.0f };
			clearAttachments[anum - 1].colorAttachment = 0;

			VkClearRect clearRect[1] = {};
			clearRect[0].layerCount = 1;
			clearRect[0].rect.offset = { 0, 0 };
			clearRect[0].rect.extent = { framebuf->w, framebuf->h };

			vkCmdClearAttachments(
				cmdbuf,
				anum,
				clearAttachments.data(),
				1,
				clearRect);
		}

		MshShaderFactory::MshVertShaderUBO vubo;
		MshShaderFactory::MshFragShaderUBO fubo;

		vubo.proj_mat = m_proj_mat;
		vubo.mv_mat = m_mv_mat;
		GLMmaterial* material = &data_->materials[0];
		fubo.loci0 = name;

		//set clipping planes
		double abcd[4];
		planes_[0]->get(abcd);
		fubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[1]->get(abcd);
		fubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[2]->get(abcd);
		fubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[3]->get(abcd);
		fubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[4]->get(abcd);
		fubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
		planes_[5]->get(abcd);
		fubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

		BBox dbox = bounds_;
        glm::mat4 gmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, float(dbox.min().x()),
                                   0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, float(dbox.min().y()),
                                   0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), float(dbox.max().z()),
                                   0.0f, 0.0f, 0.0f, 1.0f);
		glm::mat4 invmat = glm::inverseTranspose(gmat);
		fubo.matrix3 = invmat;

		vks::Buffer vert_ubuf, frag_ubuf;
		VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf, vert_ubuf_offset);
		device_->GetNextUniformBuffer(sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf, frag_ubuf_offset);

		std::vector<VkWriteDescriptorSet> descriptorWritesBase;
		m_vulkan->msh_shader_factory_->getDescriptorSetWriteUniforms(device_, vert_ubuf, frag_ubuf, descriptorWritesBase);

		vert_ubuf.copyTo(&vubo, sizeof(MshShaderFactory::MshVertShaderUBO), vert_ubuf_offset);
		frag_ubuf.copyTo(&fubo, sizeof(MshShaderFactory::MshFragShaderUBO), frag_ubuf_offset);
		if (vert_ubuf.buffer == frag_ubuf.buffer)
			vert_ubuf.flush(device_->GetCurrentUniformBufferOffset() - vert_ubuf_offset, vert_ubuf_offset);
		else
		{
			if (vert_ubuf_offset == 0)
				vert_ubuf.flush();
			else
				vert_ubuf.flush(VK_WHOLE_SIZE, vert_ubuf_offset);
			frag_ubuf.flush();
		}
		
		if (!descriptorWritesBase.empty())
		{
			device_->vkCmdPushDescriptorSetKHR(
				cmdbuf,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				0,
				descriptorWritesBase.size(),
				descriptorWritesBase.data()
			);
		}

		GLMgroup* group = data_->groups;
		int count = 0;
		while (group)
		{
			if (m_vertbufs[count].indexCount == 0)
			{
				group = group->next;
				count++;
				continue;
			}

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdbuf, 0, 1, &m_vertbufs[count].vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(cmdbuf, m_vertbufs[count].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmdbuf, m_vertbufs[count].indexCount, 1, 0, 0, 0);

			group = group->next;
			count++;
		}

		vkCmdEndRenderPass(cmdbuf);

		device_->flushCommandBuffer(cmdbuf, device_->queue, true);

	}

} // namespace FLIVR
