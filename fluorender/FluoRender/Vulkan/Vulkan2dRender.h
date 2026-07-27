#include <VVulkan.h>
#include <FLIVR/ImgShader.h>
#include <vector>
#include <memory>
#include <map>
#include <tuple>

#ifndef _Vulkan2dRender_H_
#define _Vulkan2dRender_H_

class Vulkan2dRender
{
	#define V2DRENDER_BLEND_DISABLE			0
	#define V2DRENDER_BLEND_OVER			1
	#define V2DRENDER_BLEND_OVER_INV		2
	#define V2DRENDER_BLEND_OVER_UI			3
	#define V2DRENDER_BLEND_ADD				4
	#define V2DRENDER_BLEND_SHADE_SHADOW	5
	#define V2DRENDER_BLEND_MAX				6

	#define V2DRENDER_UNIFORM_VEC_NUM	3
	#define V2DRENDER_UNIFORM_MAT_NUM	1
	#define V2DRENDER_UNIFORM_NUM	(V2DRENDER_UNIFORM_VEC_NUM+V2DRENDER_UNIFORM_MAT_NUM)

public:
	vks::Buffer m_vertexBuffer;
	vks::Buffer m_vertexBuffer34;
	vks::Buffer m_indexBuffer;
	uint32_t m_indexCount;

	struct Vertex {
		float pos[3];
		float uv[3];
	};
	struct Vertex34 {
		float pos[3];
		float uv[4];
	};

	struct V2dVertexSettings{
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> inputBinding;
		std::vector<VkVertexInputAttributeDescription> inputAttributes;
	} m_vertices, m_vertices34;

	struct V2dPipeline {
		VkPipeline vkpipeline;
		VkRenderPass pass;
		int shader;
		int blend;
		int colormap;
		VkFormat framebuf_format;
		int attachment_num;
		bool isSwapChainImage;
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		VkPolygonMode polymode = VK_POLYGON_MODE_FILL;
		VkCullModeFlags cullmode = VK_CULL_MODE_NONE;
		VkFrontFace frontface = VK_FRONT_FACE_COUNTER_CLOCKWISE;

		VkBool32 uniforms[V2DRENDER_UNIFORM_NUM] = { VK_FALSE };
		VkBool32 samplers[IMG_SHDR_SAMPLER_NUM] = { VK_FALSE };
	};

	unsigned char constant_buf[V2DRENDER_UNIFORM_VEC_NUM*sizeof(glm::vec4) + V2DRENDER_UNIFORM_MAT_NUM*sizeof(glm::mat4)];
	
	FLIVR::ImgShaderFactory::ImgPipelineSettings m_img_pipeline_settings;
	std::vector<V2dPipeline> m_pipelines;
	int prev_pipeline;

	//render passes cached on their true key (format, attachment count, swapchain flag):
	//identical keys return the identical handle, so framebuffers keyed on the render
	//pass handle (replaceRenderPass) are no longer destroyed and recreated mid-frame
	std::map<std::tuple<VkFormat, int, bool>, VkRenderPass> m_renderpass_cache;

	bool m_init;
	std::shared_ptr<VVulkan> m_vulkan;

	VkCommandBuffer m_default_cmdbuf;

	Vulkan2dRender();
	Vulkan2dRender(std::shared_ptr<VVulkan> vulkan);
	~Vulkan2dRender();

	void init(std::shared_ptr<VVulkan> vulkan);
	void generateQuad();
	void setupVertexDescriptions();
	VkRenderPass prepareRenderPass(VkFormat framebuf_format, int attachment_num, bool isSwapChainImage=false);
	V2dPipeline preparePipeline(
		int shader,
		int blend_mode,
		VkFormat framebuf_format,
		int attachment_num,
		int colormap=0,
		bool isSwapChainImage=false,
		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		VkPolygonMode polymode = VK_POLYGON_MODE_FILL,
		VkCullModeFlags cullmode = VK_CULL_MODE_NONE,
		VkFrontFace frontface = VK_FRONT_FACE_COUNTER_CLOCKWISE);

	void getEnabledUniforms(V2dPipeline &pipeline, const std::string &code);

	struct V2dObject {
		vks::Buffer vertBuf;
		vks::Buffer idxBuf;
		uint64_t vertCount = 0;
		uint64_t idxCount = 0;
		uint64_t vertOffset = 0;
		uint64_t idxOffset = 0;
	};

	struct V2DRenderParams {
		Vulkan2dRender::V2dPipeline pipeline = {};
		bool clear = false;
		VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		vks::VTexture *tex[IMG_SHDR_SAMPLER_NUM] = { NULL };
		glm::vec4 loc[V2DRENDER_UNIFORM_VEC_NUM] = { glm::vec4(0.0f) };
		glm::mat4 matrix[V2DRENDER_UNIFORM_MAT_NUM] = { glm::mat4(1.0f) };
		
		Vulkan2dRender::V2dObject* obj = nullptr;
		uint64_t render_idxCount = 0;
		uint32_t render_idxBase = 0;

		uint32_t waitSemaphoreCount = 0;
		uint32_t signalSemaphoreCount = 0;
		VkSemaphore *waitSemaphores = nullptr;
		VkSemaphore *signalSemaphores = nullptr;
	};
	
	void setupDescriptorSetWrites(const V2DRenderParams &params, const V2dPipeline &pipeline, std::vector<VkWriteDescriptorSet> &descriptorWrites);
	
	void buildCommandBuffer(VkCommandBuffer commandbufs[], int commandbuf_num, const std::unique_ptr<vks::VFrameBuffer> &framebuf, const V2DRenderParams &params);
	void render(const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams& params);
	
	void seq_buildCommandBuffer(VkCommandBuffer commandbufs[], int commandbuf_num, const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams *params, int num);
	void seq_render(const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams *params, int num);

	void buildCommandBufferClear(VkCommandBuffer commandbufs[], int commandbuf_num, const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams& params);
	void clear(const std::unique_ptr<vks::VFrameBuffer>& framebuf, const V2DRenderParams& params);

	Vulkan2dRender::V2DRenderParams GetNextV2dRenderSemaphoreSettings();

	void GetNextV2dRenderSemaphoreSettings(Vulkan2dRender::V2DRenderParams &params);
};

#endif