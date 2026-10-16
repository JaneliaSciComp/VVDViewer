#include "VVulkan.h"
#include <algorithm>
#include <iostream>

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
	// extra presentation surfaces (Qt render views) still open; the base
	// destructor cleans up the primary one's state in the members
	while (m_surfaces.size() > 1)
		removeSurface(m_surfaces.back().get());
	if (!m_surfaces.empty())
		activateSurface(m_surfaces[0].get());
	vray_shader_factory_.reset();
	cal_shader_factory_.reset();
	seg_shader_factory_.reset();
	paint_shader_factory_.reset();
	img_shader_factory_.reset();
	warp_shader_factory_.reset();
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
	
	vray_shader_factory_ = std::make_unique<FLIVR::VRayShaderFactory>(devices);
	cal_shader_factory_ = std::make_unique<FLIVR::VolCalShaderFactory>(devices);
	seg_shader_factory_ = std::make_unique<FLIVR::SegShaderFactory>(devices);
	paint_shader_factory_ = std::make_unique<FLIVR::PaintShaderFactory>(devices);
	img_shader_factory_ = std::make_unique<FLIVR::ImgShaderFactory>(devices);
	vslice_shader_factory_ = std::make_unique<FLIVR::VolSliceShaderFactory>(devices);
	warp_shader_factory_ = std::make_unique<FLIVR::VolWarpShaderFactory>(devices);
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
	//textures are dropped when their data changed: the cached brick maxima
	//of the data component (empty-brick skipping) are stale as well
	if (bricks && c <= 0)
		for (auto b : *bricks)
			if (b) b->invalidate_data_max();
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

// ---------------------------------------------------------------------------
// several presentation surfaces (see VVulkan.h)
// ---------------------------------------------------------------------------

VVulkan::SurfaceState* VVulkan::primarySurface()
{
	if (m_surfaces.empty())
	{
		m_surfaces.push_back(std::make_unique<SurfaceState>());
		m_active_surface = m_surfaces[0].get();
	}
	return m_surfaces[0].get();
}

void VVulkan::swapSurfaceState(SurfaceState& s)
{
	std::swap(swapChain, s.swapChain);
	std::swap(frameBuffers, s.frameBuffers);
	std::swap(currentBuffer, s.currentBuffer);
	std::swap(presentSemaphores, s.presentSemaphores);
	std::swap(depthStencil, s.depthStencil);
	std::swap(width, s.width);
	std::swap(height, s.height);
	std::swap(destWidth, s.destWidth);
	std::swap(destHeight, s.destHeight);
	std::swap(prepared, s.prepared);
#if defined(_WIN32)
	std::swap(window, s.window);
	std::swap(windowInstance, s.windowInstance);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	std::swap(view, s.view);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
	std::swap(display, s.display);
	std::swap(surface, s.surface);
	std::swap(connection, s.connection);
	std::swap(window, s.window);
#endif
}

// The active surface's record holds an empty placeholder state; every
// inactive record holds its live state.
void VVulkan::activateSurface(SurfaceState* s)
{
	primarySurface();
	if (!s || s == m_active_surface)
		return;
	swapSurfaceState(*m_active_surface);   // live state -> its record, placeholder -> members
	swapSurfaceState(*s);                  // s's live state -> members, placeholder -> s
	m_active_surface = s;
}

VVulkan::SurfaceState* VVulkan::beginSurface()
{
	primarySurface();
	m_surfaces.push_back(std::make_unique<SurfaceState>());
	SurfaceState* s = m_surfaces.back().get();
	// live state of the active surface -> its record; the members get the
	// active record's placeholder (default state), which the new surface uses
	swapSurfaceState(*m_active_surface);
	m_active_surface = s;
	swapChain = VulkanSwapChain();
	frameBuffers.clear();
	currentBuffer = 0;
	presentSemaphores.clear();
	depthStencil.reset();
	prepared = false;
	return s;
}

bool VVulkan::finishSurface(int w, int h)
{
	const VkFormat primary_format = m_surfaces[0]->swapChain.colorFormat;
	swapChain.connect(instance, physicalDevice, device);
	initSwapchain();
	if (swapChain.colorFormat != primary_format)
		std::cerr << "VVulkan: the new surface's colour format differs from the primary surface's" << std::endl;
	width = destWidth = (uint32_t)std::max(1, w);
	height = destHeight = (uint32_t)std::max(1, h);
	setupSwapChain();
	setupDepthStencil();
	setupFrameBuffer();
	createPresentSemaphores();
	prepared = true;
	return true;
}

void VVulkan::removeSurface(SurfaceState* s)
{
	if (!s || m_surfaces.empty() || s == m_surfaces[0].get())
		return;
	vkDeviceWaitIdle(device);
	vulkanDevice->ResetAllFrameSlots();
	activateSurface(s);
	destroyPresentSemaphores();
	frameBuffers.clear();
	if (depthStencil) depthStencil->destroy();
	depthStencil.reset();
	swapChain.cleanup();
	swapChain = VulkanSwapChain();
	prepared = false;
	// the destroyed state goes back into s's record, the primary's live state
	// into the members
	swapSurfaceState(*s);
	SurfaceState* primary = m_surfaces[0].get();
	swapSurfaceState(*primary);
	m_active_surface = primary;
	for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it)
		if (it->get() == s) { m_surfaces.erase(it); break; }
}
