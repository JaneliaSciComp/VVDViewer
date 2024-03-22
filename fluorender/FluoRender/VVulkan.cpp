#include "VVulkan.h"

VVulkan::VVulkan() : VulkanExampleBase(ENABLE_VALIDATION)
{
	enabledInstanceExtensions.push_back("VK_KHR_get_physical_device_properties2");
	enabledDeviceExtensions.push_back("VK_KHR_push_descriptor");
	enabledDeviceExtensions.push_back("VK_KHR_maintenance3");
	enabledDeviceExtensions.push_back("VK_EXT_memory_budget");
#ifdef VK_USE_PLATFORM_MACOS_MVK
    enabledInstanceExtensions.push_back("VK_KHR_portability_enumeration");
    enabledDeviceExtensions.push_back("VK_KHR_portability_subset");
#endif
}

VVulkan::~VVulkan()
{
	vol_shader_factory_.reset();
	vray_shader_factory_.reset();
	cal_shader_factory_.reset();
	seg_shader_factory_.reset();
	paint_shader_factory_.reset();
	img_shader_factory_.reset();
	msh_shader_factory_.reset();

	DestroySubDevices();
}

void VVulkan::getEnabledFeatures()
{
    enabledFeatures.textureCompressionBC = VK_TRUE;
    enabledFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
    enabledFeatures.fillModeNonSolid = VK_TRUE;
    
    if (deviceFeatures.wideLines == VK_TRUE)
        enabledFeatures.wideLines = VK_TRUE;
}

void VVulkan::prepare()
{
	VulkanExampleBase::prepare();
	//#0 is a primary device
	devices.push_back(vulkanDevice);

	initSubDevices();
	
	vol_shader_factory_ = std::make_unique<FLIVR::VolShaderFactory>(devices);
	vray_shader_factory_ = std::make_unique<FLIVR::VRayShaderFactory>(devices);
	cal_shader_factory_ = std::make_unique<FLIVR::VolCalShaderFactory>(devices);
	seg_shader_factory_ = std::make_unique<FLIVR::SegShaderFactory>(devices);
	paint_shader_factory_ = std::make_unique<FLIVR::PaintShaderFactory>(devices);
	img_shader_factory_ = std::make_unique<FLIVR::ImgShaderFactory>(devices);
	vslice_shader_factory_ = std::make_unique<FLIVR::VolSliceShaderFactory>(devices);
	msh_shader_factory_ = std::make_unique<FLIVR::MshShaderFactory>(devices);
	
	prepared = true;
}

void VVulkan::initSubDevices()
{
}

void VVulkan::DestroySubDevices()
{
	if (devices.size() > 1)
	{
		for (int i = 0; i < devices.size(); i++)
		{
			if (devices[i])
				delete devices[i];
		}
	}
}

void VVulkan::clearTexPools()
{
	for (auto dev : devices)
	{
		dev->clear_tex_pool();
	}
}

void VVulkan::eraseBricksFromTexpools(const std::vector<FLIVR::TextureBrick*>* bricks, int c)
{
	for (auto dev : devices)
	{
		for (auto &e : dev->tex_pool)
		{
			for (auto b : *bricks)
			{
				if (b == e.brick && e.tex && (c == e.comp || c < 0))
                {
					e.delayed_del = true;
                    b->set_modified(c, true);
                }
			}
		}
		dev->update_texpool();
	}
}

bool VVulkan::findTexInPools(FLIVR::TextureBrick *b, int c, int w, int h, int d, int bytes, VkFormat format,
							 vks::VulkanDevice* &ret_dev, int &ret_id)
{
	for (auto dev : devices)
	{
		int count = 0;
		for (auto &e : dev->tex_pool)
		{
			if (e.tex && b == e.brick && c == e.comp &&
				w == e.tex->w && h == e.tex->h && d == e.tex->d && bytes == e.tex->bytes &&
				format == e.tex->format)
			{
				//found!
				ret_dev = dev;
				ret_id = count;
				return true;
			}
			count++;
		}
	}

	return false;
}

bool VVulkan::getCompatibleTexFromPool(int w, int h, int d, int bytes, VkFormat format,
									   vks::VulkanDevice* &ret_dev, int &ret_id)
{
	return true;
}

void VVulkan::GenTextures2DAllDevice(std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> &result,
									 VkFormat format, VkFilter filter, uint32_t w, uint32_t h, VkImageUsageFlags usage)
{
	if (!result.empty())
		result.clear();

	for (auto dev : devices)
		result[dev] = dev->GenTexture2D(format, filter, w, h, usage);
}

bool VVulkan::UploadTextures(std::map<vks::VulkanDevice*, std::shared_ptr<vks::VTexture>> &tex, void *data)
{
	for (auto dev : devices)
	{
		if (tex[dev])
			dev->UploadTexture(tex[dev], data);
	}

	return true;
}

void VVulkan::ResetRenderSemaphores()
{
	for (auto dev : devices)
	{
		dev->ResetRenderSemaphores();
	}
}
