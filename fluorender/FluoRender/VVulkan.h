#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "Vulkan/vulkanexamplebase.h"
#include "Vulkan/VulkanTexture.hpp"
#include "Vulkan/VulkanModel.hpp"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

#include <FLIVR/ShaderProgram.h>
#include <FLIVR/VolShader.h>
#include <FLIVR/VRayShader.h>
#include <FLIVR/VolCalShader.h>
#include <FLIVR/SegShader.h>
#include <FLIVR/PaintShader.h>
#include <FLIVR/ImgShader.h>
#include <FLIVR/VolSliceShader.h>
#include <FLIVR/VolWarpShader.h>
#include <FLIVR/MshShader.h>

#include <vector>
#include <memory>

#ifdef _DEBUG
#define ENABLE_VALIDATION true
#else
#define ENABLE_VALIDATION false
#endif

#ifndef _VVULKAN_H_
#define _VVULKAN_H_

class VVulkan : public VulkanExampleBase
{
public:

	std::vector<vks::VulkanDevice*> devices;
	
	VVulkan();

	~VVulkan();

	std::unique_ptr<FLIVR::VolShaderFactory> vol_shader_factory_;
	std::unique_ptr<FLIVR::VRayShaderFactory> vray_shader_factory_;
	std::unique_ptr<FLIVR::VolCalShaderFactory> cal_shader_factory_;
	std::unique_ptr<FLIVR::SegShaderFactory> seg_shader_factory_;
	std::unique_ptr<FLIVR::PaintShaderFactory> paint_shader_factory_;
	std::unique_ptr<FLIVR::ImgShaderFactory> img_shader_factory_;
	std::unique_ptr<FLIVR::VolSliceShaderFactory> vslice_shader_factory_;
	std::unique_ptr<FLIVR::VolWarpShaderFactory> warp_shader_factory_;
	std::unique_ptr<FLIVR::MshShaderFactory> msh_shader_factory_;

	void prepare();
	void initSubDevices();
	void DestroySubDevices();
	void clearTexPools();
    
    virtual void getEnabledFeatures();

	void eraseBricksFromTexpools(const std::vector<FLIVR::TextureBrick*>* bricks, int c=-1);
	bool findTexInPools(FLIVR::TextureBrick* b, int c, int w, int h, int d, int bytes, VkFormat format, vks::VulkanDevice* &ret_dev, int &ret_id);
	bool getCompatibleTexFromPool(int w, int h, int d, int bytes, VkFormat format, vks::VulkanDevice* &ret_dev, int &ret_id);

	void GenTextures2DAllDevice(std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> &result,
								VkFormat format, VkFilter filter, uint32_t w, uint32_t h,
								VkImageUsageFlags usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
	bool UploadTextures(std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> &tex, void *data);

	void ResetRenderSemaphores();
};

#endif
