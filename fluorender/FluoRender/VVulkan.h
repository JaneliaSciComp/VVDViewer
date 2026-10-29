#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "Vulkan/vulkanexamplebase.h"
#include "Vulkan/VulkanBuffer.hpp"
#include "Vulkan/VulkanDevice.hpp"

#include <FLIVR/ShaderProgram.h>
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

	// ---- several presentation surfaces on one device (Qt front end: one
	// swapchain per render view, all views sharing this context, its
	// textures and its pipelines; the wx app never calls these). The
	// swapchain state of VulkanExampleBase (swap chain, its frame buffers,
	// current image, present semaphores, depth buffer, size, window handle)
	// lives in the members while its surface is active; the other surfaces
	// keep theirs in a SurfaceState. Single-threaded (GUI thread) use only;
	// a frame (prepareFrame .. submitFrame) must not switch surfaces.
	struct SurfaceState
	{
		VulkanSwapChain swapChain;
		std::vector<std::unique_ptr<vks::VFrameBuffer>> frameBuffers;
		uint32_t currentBuffer = 0;
		std::vector<VkSemaphore> presentSemaphores;
		std::shared_ptr<vks::VTexture> depthStencil;
		uint32_t width = 0, height = 0, destWidth = 0, destHeight = 0;
		bool prepared = false;
#if defined(_WIN32)
		HWND window = nullptr;
		HINSTANCE windowInstance = nullptr;
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
		void* view = nullptr;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
		wl_display* display = nullptr;
		wl_surface* surface = nullptr;
		xcb_connection_t* connection = nullptr;
		xcb_window_t window = 0;
#endif
	};
	// the surface prepare() was called for
	SurfaceState* primarySurface();
	SurfaceState* activeSurface() { return primarySurface(), m_active_surface; }
	// make s the surface whose state is in the members (no-op when it is)
	void activateSurface(SurfaceState* s);
	// a new surface: beginSurface() activates an empty state; set the window
	// with setWindow(...), then finishSurface(w, h) creates its swapchain,
	// depth buffer and frame buffers (same render pass / colour format as
	// the primary surface). The new surface stays active.
	SurfaceState* beginSurface();
	bool finishSurface(int w, int h);
	// destroy a surface made by beginSurface (waits for the device); the
	// primary surface becomes active
	void removeSurface(SurfaceState* s);

private:
	void swapSurfaceState(SurfaceState& s);
	std::vector<std::unique_ptr<SurfaceState>> m_surfaces;   // [0] = primary
	SurfaceState* m_active_surface = nullptr;
};

#endif
