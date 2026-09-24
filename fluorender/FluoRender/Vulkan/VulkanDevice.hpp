/*
* Vulkan device class
*
* Encapsulates a physical Vulkan device and it's logical representation
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <exception>
#include <assert.h>
#include <algorithm>
#include <memory>
#include <array>
#include <functional>
#include <cstdlib>
#include "vulkan/vulkan.h"
#include "VulkanTools.h"
#include "VulkanBuffer.hpp"
#include <FLIVR/TextureBrick.h>

namespace vks
{
	class VTexture;
	class VFrameBuffer;
	class VSemaphore;
	struct VulkanSemaphoreSettings;

	//divisor for the MiB-based memory accounting (mem_limit / available_mem)
	constexpr double MEM_MB = 1048576.0;

	// Full TexParam definition must precede VulkanDevice. libc++'s std::vector<T>
	// requires T to be complete at the point its destructor is instantiated, which
	// happens implicitly when VulkanDevice declares a std::vector<TexParam> member.
	struct TexParam
	{
		std::shared_ptr<VTexture> tex;
		FLIVR::TextureBrick *brick;
		int comp;
		bool delayed_del;
		TexParam() :
			tex(0), brick(0), comp(0),
			delayed_del(false)
		{}
		TexParam(int c, const std::shared_ptr<VTexture> &t) :
			tex(t), brick(0), comp(c), delayed_del(false)
		{}
	};

	struct VulkanDevice
	{
		/** @brief Physical device representation */
		VkPhysicalDevice physicalDevice;
		/** @brief Logical device representation (application's view of the device) */
		VkDevice logicalDevice;
		// Handle to the device graphics queue that command buffers are submitted to
		VkQueue queue;
		VkQueue compute_queue;
		VkQueue transfer_queue;
		/** @brief Properties of the physical device including limits that the application can check against */
		VkPhysicalDeviceProperties properties;
		/** @brief Features of the physical device that an application can use to check if a feature is supported */
		VkPhysicalDeviceFeatures features;
		/** @brief Features that have been enabled for use on the physical device */
		VkPhysicalDeviceFeatures enabledFeatures;
		/** @brief Memory types and heaps of the physical device */
		VkPhysicalDeviceMemoryProperties memoryProperties;
		/** @brief Queue family properties of the physical device */
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		/** @brief List of extensions supported by the device */
		std::vector<std::string> supportedExtensions;

		/** @brief Default command pool for the graphics queue family index */
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandPool compute_commandPool = VK_NULL_HANDLE;
		VkCommandPool transfer_commandPool = VK_NULL_HANDLE;

		/** @brief Set to true when the debug marker extension is detected */
		bool enableDebugMarkers = false;

		/** @brief BC4 optimal-tiling support, cached at device creation */
		bool bc4_available = false;

		/** @brief Contains queue family indices */
		struct
		{
			uint32_t graphics;
			uint32_t compute;
			uint32_t transfer;
		} queueFamilyIndices;
		uint32_t uniqueQueueFamilyIndices[3];
		uint32_t uniqueQueueFamilyCount;

		PFN_vkCmdPushDescriptorSetKHR vkCmdPushDescriptorSetKHR;

		VkCommandPool cmd_pool;

		//staging buffer for synchronous transfers (downloads, 2D sub-uploads, buffer uploads)
		vks::Buffer staging_buf;

		//ring of staging buffers for 3D texture uploads: while the GPU copies out of one
		//slot, the CPU can fill the next one. Each slot's fence guards its reuse.
		struct StagingRingSlot {
			vks::Buffer buf;
			VkFence fence = VK_NULL_HANDLE;
			bool pending = false;
		};
		static const uint32_t STAGING_RING_SIZE = 3;
		StagingRingSlot m_staging_ring[STAGING_RING_SIZE];
		uint32_t m_staging_ring_idx = 0;
		StagingRingSlot* acquireUploadStagingSlot(VkDeviceSize size);

		VkSampler linear_sampler = VK_NULL_HANDLE;
		VkSampler nearest_sampler = VK_NULL_HANDLE;

		bool mem_swap = true;
		bool use_mem_limit = false;
		double mem_limit = 0.0;
		double available_mem = 0.0;
		std::vector<TexParam> tex_pool;

		void setMemoryLimit(double limit=0);
		void clear_tex_pool();
		static bool return_brick(const TexParam &texp);
		void update_texpool();
		int check_swap_memory(FLIVR::TextureBrick* brick, int c, bool *swapped=nullptr);
		int findTexInPool(FLIVR::TextureBrick* b, int c, int w, int h, int d, int bytes, VkFormat format);
		int GenTexture3D_pool(VkFormat format, VkFilter filter, FLIVR::TextureBrick *b, int comp);

		//per-frame-slot resources: command buffers, transient buffer rings, the render
		//semaphore chain, deferred destruction lists and a frame fence. With more than
		//one slot the CPU can record frame N+1 while the GPU still executes frame N;
		//a slot is only reused after its fence has signaled.
		struct FrameSlot {
			std::vector<VkCommandBuffer> cmdbufs, trans_cmdbufs;
			size_t cur_cmdbuf_id = 0;
			size_t cur_trans_cmdbuf_id = 0;
			//transient rings (ubo: host-visible; vbuf/ibuf: device-local, filled by compute;
			//hvbuf/hibuf: host-visible vertex/index for CPU-generated overlay geometry)
			vks::Buffer ubo, vbuf, ibuf, hvbuf, hibuf;
			std::vector<vks::Buffer> ubos, vbufs, ibufs, hvbufs, hibufs;
			VkDeviceSize ubo_offset = 0;
			VkDeviceSize vbuf_offset = 0;
			VkDeviceSize ibuf_offset = 0;
			VkDeviceSize hvbuf_offset = 0;
			VkDeviceSize hibuf_offset = 0;
			//per-slot render semaphore chain
			std::vector<std::unique_ptr<vks::VSemaphore>> render_semaphore;
			int cur_semaphore_id = -1;
			//signaled by the frame-end submit; guards reuse of everything in this slot
			VkFence fence = VK_NULL_HANDLE;
			bool fence_pending = false;
			//links the first submit of the next frame after this frame's last submit
			std::unique_ptr<vks::VSemaphore> frame_link_sem;
			//true while frame_link_sem carries a signal no submit has waited on yet
			bool frame_link_signaled = false;
			//deferred destruction: objects released while this slot's frame may still
			//be executing on the GPU; run when the slot is reused
			std::vector<std::shared_ptr<VTexture>> retired_texs;
			std::vector<std::function<void()>> deferred_destroys;
		};
		static const uint32_t FRAME_SLOTS_MAX = 2;
		FrameSlot m_frames[FRAME_SLOTS_MAX];
		uint32_t m_frame_slots = 1;   //number of slots in use (1 = fully serialized frames)
		uint32_t m_cur_frame = 0;
		bool m_in_shutdown = false;
		FrameSlot& frame() { return m_frames[m_cur_frame]; }

		//defer a destruction until the current slot's frame has provably finished
		void retire(std::function<void()> fn);
		//retire a vks::Buffer's handles and null it out
		void retireBuffer(vks::Buffer& buf);
		//wait until every slot's in-flight frame has completed
		void WaitIdleAllFrameSlots();
		//reset every slot after a vkDeviceWaitIdle (e.g. on window resize): clears
		//fences, runs deferred destructions and recreates the semaphore chains
		//(binary semaphores can be left signaled with no waiter after an
		//OUT_OF_DATE acquire/present sequence)
		void ResetAllFrameSlots();

		//semaphores (operate on the current frame slot)
		void ResetRenderSemaphores();
		VulkanSemaphoreSettings GetNextRenderSemaphoreSettings();
		VkSemaphore* GetNextRenderSemaphore();
		VkSemaphore* GetCurrentRenderSemaphore();
		//undo the most recent GetNextRenderSemaphore (failed brick load rollback)
		void RollbackRenderSemaphore();

		void PrepareMainRenderBuffers();
		void ResetMainRenderBuffers();
		//frame-start link submit: orders this frame's GPU work after both the
		//swapchain acquire and the previous frame's frame-end submit (cross-frame
		//resources such as brick textures and masks are mutable, so successive
		//frames must not overlap on the GPU even with multiple slots in flight)
		void SubmitFrameLink();
		//frame-end submit: waits the tail of the semaphore chain, signals the
		//cross-frame link and the per-image present semaphore, and arms the slot
		//fence that guards reuse of this slot's resources
		void SubmitFrameEnd(VkSemaphore present_sem);
		void prepareFrameSlot(FrameSlot& slot);
		void consolidateRing(vks::Buffer& cur, std::vector<vks::Buffer>& spill,
			VkBufferUsageFlags usage, VkMemoryPropertyFlags mem, bool map_buf);
		void ringNext(vks::Buffer& cur, std::vector<vks::Buffer>& spill, VkDeviceSize& cursor,
			VkDeviceSize req_size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem, bool map_buf,
			vks::Buffer& buf, VkDeviceSize& offset);
		VkCommandBuffer GetNextCommandBuffer();
		VkCommandBuffer GetNextTransferCommandBuffer();
		void GetNextUniformBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize &offset);
		void GetNextVertexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset);
		void GetNextIndexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset);
		void GetNextHostVertexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset);
		void GetNextHostIndexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset);
		VkDeviceSize GetCurrentUniformBufferOffset();

		VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
		void setupDescriptorPool();

		VkPipelineCache pipelineCache = VK_NULL_HANDLE;
		std::string pipeline_cache_path;
		void createPipelineCache();
		void savePipelineCache();

		void prepareSamplers();
		void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
		void checkStagingBuffer(VkDeviceSize size);
		std::shared_ptr<VTexture> GenTexture2D(VkFormat format, VkFilter filter, uint32_t w, uint32_t h, VkImageUsageFlags usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_STORAGE_BIT);
		std::shared_ptr<VTexture> GenTexture3D(VkFormat format, VkFilter filter, uint32_t w, uint32_t h, uint32_t d);
		bool UploadTexture3D(const std::shared_ptr<VTexture> &tex, void *data, VkOffset3D offset, uint32_t ypitch, uint32_t zpitch, bool flush=true, vks::VulkanSemaphoreSettings* semaphore=nullptr, bool sync=true);
		bool UploadSubTexture2D(const std::shared_ptr<VTexture>& tex, void* data, VkOffset2D offset, VkExtent2D extent, bool sync=true);
		bool UploadTexture(const std::shared_ptr<VTexture> &tex, void *data, bool flush=true, vks::VulkanSemaphoreSettings* semaphore=nullptr, bool sync=true);
		void CopyDataStagingBuf2Tex(const std::shared_ptr<VTexture> &tex, bool flush, vks::VulkanSemaphoreSettings* semaphore, StagingRingSlot* slot = nullptr);
		void CopyDataStagingBuf2SubTex2D(const std::shared_ptr<VTexture>& tex, VkOffset2D offset, VkExtent2D extent);
		bool DownloadTexture3D(const std::shared_ptr<VTexture> &tex, void *data, VkOffset3D offset, uint32_t ypitch, uint32_t zpitch);
		bool DownloadTexture(const std::shared_ptr<VTexture> &tex, void *data);
		bool DownloadSubTexture2D(const std::shared_ptr<VTexture>& tex, void* data, VkOffset2D offset, VkExtent2D extent);
		void CopyDataTex2StagingBuf(const std::shared_ptr<VTexture> &tex);
		void CopyDataSubTex2StagingBuf2D(const std::shared_ptr<VTexture>& tex, VkOffset2D offset, VkExtent2D extent);
		
		void UploadData2Buffer(void* data, vks::Buffer* dst, VkDeviceSize offset, VkDeviceSize size);

		/**  @brief Typecast to VkDevice */
		operator VkDevice() { return logicalDevice; };

		/**
		* Default constructor
		*
		* @param physicalDevice Physical device that is to be used
		*/
		VulkanDevice(VkPhysicalDevice physicalDevice)
		{
			assert(physicalDevice);
			this->physicalDevice = physicalDevice;

			// Store Properties features, limits and properties of the physical device for later use
			// Device properties also contain limits and sparse properties
			vkGetPhysicalDeviceProperties(physicalDevice, &properties);
			// Features should be checked by the examples before using them
			vkGetPhysicalDeviceFeatures(physicalDevice, &features);
			// Cache BC4 support so per-brick loads do not query the driver every frame
			{
				VkFormatProperties fprops = {};
				vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_BC4_UNORM_BLOCK, &fprops);
				bc4_available = fprops.optimalTilingFeatures != 0;
			}
			// Memory properties are used regularly for creating all kinds of buffers
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
			// Queue family properties, used for setting up requested queues upon device creation
			uint32_t queueFamilyCount;
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
			assert(queueFamilyCount > 0);
			queueFamilyProperties.resize(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

			// Get list of supported extensions
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
			if (extCount > 0)
			{
				std::vector<VkExtensionProperties> extensions(extCount);
				if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
				{
					for (auto ext : extensions)
					{
						supportedExtensions.push_back(ext.extensionName);
					}
				}
			}
		}

		/** 
		* Default destructor
		*
		* @note Frees the logical device
		*/
		~VulkanDevice()
		{
			//from here on, destructions must happen immediately (the deferred lists
			//are drained below and never processed again)
			m_in_shutdown = true;
			WaitIdleAllFrameSlots();

			//textures in the pool must release their Vulkan objects while the device is still alive
			//(members are destroyed after this body runs, i.e. after vkDestroyDevice)
			tex_pool.clear();
			staging_buf.destroy();
			for (uint32_t i = 0; i < STAGING_RING_SIZE; i++)
			{
				if (m_staging_ring[i].fence != VK_NULL_HANDLE)
				{
					if (m_staging_ring[i].pending)
						vkWaitForFences(logicalDevice, 1, &m_staging_ring[i].fence, VK_TRUE, UINT64_MAX);
					vkDestroyFence(logicalDevice, m_staging_ring[i].fence, nullptr);
					m_staging_ring[i].fence = VK_NULL_HANDLE;
				}
				m_staging_ring[i].buf.destroy();
			}
			for (uint32_t s = 0; s < FRAME_SLOTS_MAX; s++)
			{
				FrameSlot& slot = m_frames[s];
				for (auto& fn : slot.deferred_destroys)
					fn();
				slot.deferred_destroys.clear();
				slot.retired_texs.clear();
				slot.render_semaphore.clear();
				slot.frame_link_sem.reset();
				if (slot.fence != VK_NULL_HANDLE)
				{
					vkDestroyFence(logicalDevice, slot.fence, nullptr);
					slot.fence = VK_NULL_HANDLE;
				}
				slot.ubo.destroy();
				for (auto& b : slot.ubos) b.destroy();
				slot.ubos.clear();
				slot.vbuf.destroy();
				for (auto& b : slot.vbufs) b.destroy();
				slot.vbufs.clear();
				slot.ibuf.destroy();
				for (auto& b : slot.ibufs) b.destroy();
				slot.ibufs.clear();
				slot.hvbuf.destroy();
				for (auto& b : slot.hvbufs) b.destroy();
				slot.hvbufs.clear();
				slot.hibuf.destroy();
				for (auto& b : slot.hibufs) b.destroy();
				slot.hibufs.clear();
				if (!slot.cmdbufs.empty())
					vkFreeCommandBuffers(logicalDevice, commandPool, static_cast<uint32_t>(slot.cmdbufs.size()), slot.cmdbufs.data());
				slot.cmdbufs.clear();
				if (!slot.trans_cmdbufs.empty())
					vkFreeCommandBuffers(logicalDevice, transfer_commandPool, static_cast<uint32_t>(slot.trans_cmdbufs.size()), slot.trans_cmdbufs.data());
				slot.trans_cmdbufs.clear();
			}
			if (linear_sampler)
				vkDestroySampler(logicalDevice, linear_sampler, nullptr);
			if (nearest_sampler)
				vkDestroySampler(logicalDevice, nearest_sampler, nullptr);
			if (pipelineCache)
			{
				savePipelineCache();
				vkDestroyPipelineCache(logicalDevice, pipelineCache, nullptr);
			}
			if (descriptorPool)
				vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
			if (commandPool)
				vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
			if (compute_commandPool)
				vkDestroyCommandPool(logicalDevice, compute_commandPool, nullptr);
			if (transfer_commandPool)
				vkDestroyCommandPool(logicalDevice, transfer_commandPool, nullptr);
			if (logicalDevice)
			{
				vkDestroyDevice(logicalDevice, nullptr);
			}
		}

		/**
		* Get the index of a memory type that has all the requested property bits set
		*
		* @param typeBits Bitmask with bits set for each memory type supported by the resource to request for (from VkMemoryRequirements)
		* @param properties Bitmask of properties for the memory type to request
		* @param (Optional) memTypeFound Pointer to a bool that is set to true if a matching memory type has been found
		* 
		* @return Index of the requested memory type
		*
		* @throw Throws an exception if memTypeFound is null and no memory type could be found that supports the requested properties
		*/
		uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr)
		{
			for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
			{
				if ((typeBits & 1) == 1)
				{
					if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
					{
						if (memTypeFound)
						{
							*memTypeFound = true;
						}
						return i;
					}
				}
				typeBits >>= 1;
			}

			if (memTypeFound)
			{
				*memTypeFound = false;
				return 0;
			}
			else
			{
				throw std::runtime_error("Could not find a matching memory type");
			}
		}

		/**
		* Get the index of a queue family that supports the requested queue flags
		*
		* @param queueFlags Queue flags to find a queue family index for
		*
		* @return Index of the queue family index that matches the flags
		*
		* @throw Throws an exception if no queue family index could be found that supports the requested flags
		*/
		uint32_t getQueueFamilyIndex(VkQueueFlagBits queueFlags)
		{
			// Dedicated queue for compute
			// Try to find a queue family index that supports compute but not graphics
			if (queueFlags & VK_QUEUE_COMPUTE_BIT)
			{
				for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
				{
					if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0))
					{
						return i;
						break;
					}
				}
			}

			// Dedicated queue for transfer
			// Try to find a queue family index that supports transfer but not graphics and compute
			if (queueFlags & VK_QUEUE_TRANSFER_BIT)
			{
				for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
				{
					if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0))
					{
						return i;
						break;
					}
				}
			}

			// For other queue types or if no separate compute queue is present, return the first one to support the requested flags
			for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProperties.size()); i++)
			{
				if (queueFamilyProperties[i].queueFlags & queueFlags)
				{
					return i;
					break;
				}
			}

			throw std::runtime_error("Could not find a matching queue family index");
		}

		/**
		* Create the logical device based on the assigned physical device, also gets default queue family indices
		*
		* @param enabledFeatures Can be used to enable certain features upon device creation
		* @param useSwapChain Set to false for headless rendering to omit the swapchain device extensions
		* @param requestedQueueTypes Bit flags specifying the queue types to be requested from the device  
		*
		* @return VkResult of the device creation call
		*/
		VkResult createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, bool useSwapChain = true, VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)
		{			
			// Desired queues need to be requested upon logical device creation
			// Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially if the application
			// requests different queue types

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

			// Get queue family indices for the requested queue family types
			// Note that the indices may overlap depending on the implementation

			const float defaultQueuePriority(0.0f);

			// Graphics queue
			if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
			{
				queueFamilyIndices.graphics = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
			else
			{
				queueFamilyIndices.graphics = -1;
			}

			// Dedicated compute queue
			if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT)
			{
				queueFamilyIndices.compute = getQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
				if (queueFamilyIndices.compute != queueFamilyIndices.graphics)
				{
					// If compute family index differs, we need an additional queue create info for the compute queue
					VkDeviceQueueCreateInfo queueInfo{};
					queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
					queueInfo.queueCount = 1;
					queueInfo.pQueuePriorities = &defaultQueuePriority;
					queueCreateInfos.push_back(queueInfo);
				}
			}
			else
			{
				// Else we use the same queue
				queueFamilyIndices.compute = queueFamilyIndices.graphics;
			}

			// Dedicated transfer queue
			if (requestedQueueTypes & VK_QUEUE_TRANSFER_BIT)
			{
				queueFamilyIndices.transfer = getQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
				if ((queueFamilyIndices.transfer != queueFamilyIndices.graphics) && (queueFamilyIndices.transfer != queueFamilyIndices.compute))
				{
					// If compute family index differs, we need an additional queue create info for the compute queue
					VkDeviceQueueCreateInfo queueInfo{};
					queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queueInfo.queueFamilyIndex = queueFamilyIndices.transfer;
					queueInfo.queueCount = 1;
					queueInfo.pQueuePriorities = &defaultQueuePriority;
					queueCreateInfos.push_back(queueInfo);
				}
			}
			else
			{
				// Else we use the same queue
				queueFamilyIndices.transfer = queueFamilyIndices.graphics;
			}

			// Create the logical device representation
			std::vector<const char*> deviceExtensions;
			for (auto& ext : enabledExtensions)
			{
				if (extensionSupported(ext))
					deviceExtensions.push_back(ext);
			}
			if (useSwapChain)
			{
				// If the device will be used for presenting to a display via a swapchain we need to request the swapchain extension
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			}

			VkDeviceCreateInfo deviceCreateInfo = {};
			deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());;
			deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
			deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

			// Enable the debug marker extension if it is present (likely meaning a debugging tool is present)
			if (extensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
			{
				deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
				enableDebugMarkers = true;
			}

			if (deviceExtensions.size() > 0)
			{
				deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
				deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
			}

			VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice);

			if (result == VK_SUCCESS)
			{
				// Create a default command pool for graphics command buffers
				commandPool = createCommandPool(queueFamilyIndices.graphics);
				compute_commandPool = createCommandPool(queueFamilyIndices.compute);
				transfer_commandPool = createCommandPool(queueFamilyIndices.transfer);
			}

			this->enabledFeatures = enabledFeatures;

			// Get a graphics queue from the device
			uniqueQueueFamilyCount = 0;
			vkGetDeviceQueue(logicalDevice, queueFamilyIndices.graphics, 0, &queue);
			uniqueQueueFamilyIndices[uniqueQueueFamilyCount] = queueFamilyIndices.graphics;
			uniqueQueueFamilyCount++;
			// Get a compute queue from the device
			if (queueFamilyIndices.compute != queueFamilyIndices.graphics)
			{
				vkGetDeviceQueue(logicalDevice, queueFamilyIndices.compute, 0, &compute_queue);
				uniqueQueueFamilyIndices[uniqueQueueFamilyCount] = queueFamilyIndices.compute;
				uniqueQueueFamilyCount++;
			}
			else
				compute_queue = queue;
			// Get a transfer queue from the device
			if (queueFamilyIndices.transfer != queueFamilyIndices.graphics)
			{
				vkGetDeviceQueue(logicalDevice, queueFamilyIndices.transfer, 0, &transfer_queue);
				uniqueQueueFamilyIndices[uniqueQueueFamilyCount] = queueFamilyIndices.transfer;
				uniqueQueueFamilyCount++;
			}
			else
				transfer_queue = queue;

			vkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(logicalDevice, "vkCmdPushDescriptorSetKHR");
			if (!vkCmdPushDescriptorSetKHR)
			{
				//the whole renderer is built on push descriptors; without the extension
				//the first draw would call a null function pointer
				vks::tools::exitFatal("This GPU/driver does not support VK_KHR_push_descriptor, which VVDViewer requires.", VK_ERROR_EXTENSION_NOT_PRESENT);
			}

			createPipelineCache();
			setupDescriptorPool();
			setMemoryLimit();

			//frames in flight: with 2 slots the CPU records frame N+1 while the GPU
			//still executes frame N; VVD_FRAMES_IN_FLIGHT=1 restores the old fully
			//serialized behavior (frame-end queue drains)
			m_frame_slots = 2;
			if (const char* fif = std::getenv("VVD_FRAMES_IN_FLIGHT"))
			{
				int n = std::atoi(fif);
				if (n >= 1 && n <= (int)FRAME_SLOTS_MAX)
					m_frame_slots = (uint32_t)n;
			}

			PrepareMainRenderBuffers();

			linear_sampler = VK_NULL_HANDLE;
			nearest_sampler = VK_NULL_HANDLE;

			prepareSamplers();

			return result;
		}

		/**
		* Create a buffer on the device
		*
		* @param usageFlags Usage flag bitmask for the buffer (i.e. index, vertex, uniform buffer)
		* @param memoryPropertyFlags Memory properties for this buffer (i.e. device local, host visible, coherent)
		* @param size Size of the buffer in byes
		* @param buffer Pointer to the buffer handle acquired by the function
		* @param memory Pointer to the memory handle acquired by the function
		* @param data Pointer to the data that should be copied to the buffer after creation (optional, if not set, no data is copied over)
		*
		* @return VK_SUCCESS if buffer handle and memory have been created and (optionally passed) data has been copied
		*/
		VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void *data = nullptr)
		{
			// Create the buffer handle
			VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(usageFlags, size);
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			VK_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, buffer));

			// Create the memory backing up the buffer handle
			VkMemoryRequirements memReqs;
			VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
			vkGetBufferMemoryRequirements(logicalDevice, *buffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			// Find a memory type index that fits the properties of the buffer
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
			VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, memory));
			
			// If a pointer to the buffer data has been passed, map the buffer and copy over the data
			if (data != nullptr)
			{
				void *mapped;
				VK_CHECK_RESULT(vkMapMemory(logicalDevice, *memory, 0, size, 0, &mapped));
				memcpy(mapped, data, size);
				// If host coherency hasn't been requested, do a manual flush to make writes visible
				if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
				{
					VkMappedMemoryRange mappedRange = vks::initializers::mappedMemoryRange();
					mappedRange.memory = *memory;
					mappedRange.offset = 0;
					mappedRange.size = size;
					vkFlushMappedMemoryRanges(logicalDevice, 1, &mappedRange);
				}
				vkUnmapMemory(logicalDevice, *memory);
			}

			// Attach the memory to the buffer object
			VK_CHECK_RESULT(vkBindBufferMemory(logicalDevice, *buffer, *memory, 0));

			return VK_SUCCESS;
		}

		/**
		* Create a buffer on the device
		*
		* @param usageFlags Usage flag bitmask for the buffer (i.e. index, vertex, uniform buffer)
		* @param memoryPropertyFlags Memory properties for this buffer (i.e. device local, host visible, coherent)
		* @param buffer Pointer to a vk::Vulkan buffer object
		* @param size Size of the buffer in byes
		* @param data Pointer to the data that should be copied to the buffer after creation (optional, if not set, no data is copied over)
		*
		* @return VK_SUCCESS if buffer handle and memory have been created and (optionally passed) data has been copied
		*/
		VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, vks::Buffer *buffer, VkDeviceSize size, void *data = nullptr)
		{
			buffer->device = logicalDevice;

			// Create the buffer handle
			VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo(usageFlags, size);
			VK_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, &buffer->buffer));

			// Create the memory backing up the buffer handle
			VkMemoryRequirements memReqs;
			VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
			vkGetBufferMemoryRequirements(logicalDevice, buffer->buffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			// Find a memory type index that fits the properties of the buffer
			memAlloc.memoryTypeIndex = getMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
			VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, &buffer->memory));

			buffer->alignment = memReqs.alignment;
			buffer->size = memAlloc.allocationSize;
			buffer->usageFlags = usageFlags;
			buffer->memoryPropertyFlags = memoryPropertyFlags;

			// If a pointer to the buffer data has been passed, map the buffer and copy over the data
			if (data != nullptr)
			{
				VK_CHECK_RESULT(buffer->map());
				memcpy(buffer->mapped, data, size);
				if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
					buffer->flush();

				buffer->unmap();
			}

			// Initialize a default descriptor that covers the whole buffer size
			buffer->setupDescriptor();

			// Attach the memory to the buffer object
			return buffer->bind();
		}

		VkResult createBufferInPool(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, vks::Buffer* buffer, VkDeviceSize size, void* data = nullptr)
		{
			VkResult result = createBuffer(usageFlags, memoryPropertyFlags, buffer, size, data);
			buffer->in_pool = true;
			available_mem -= buffer->size / MEM_MB;

			return result;
		}

		void destroyBufferInPool(vks::Buffer* buffer)
		{
			if (!buffer)
				return;
			
			buffer->destroy();
			if (buffer->in_pool)
				available_mem += buffer->size / MEM_MB;
		}

		/**
		* Copy buffer data from src to dst using VkCmdCopyBuffer
		* 
		* @param src Pointer to the source buffer to copy from
		* @param dst Pointer to the destination buffer to copy tp
		* @param queue Pointer
		* @param copyRegion (Optional) Pointer to a copy region, if NULL, the whole buffer is copied
		*
		* @note Source and destionation pointers must have the approriate transfer usage flags set (TRANSFER_SRC / TRANSFER_DST)
		*/
		void copyBuffer(vks::Buffer *src, vks::Buffer *dst, VkQueue queue, VkBufferCopy *copyRegion = nullptr)
		{
			assert(dst->size <= src->size);
			assert(src->buffer);
			VkCommandBuffer copyCmd = createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
			VkBufferCopy bufferCopy{};
			if (copyRegion == nullptr)
			{
				bufferCopy.size = src->size;
			}
			else
			{
				bufferCopy = *copyRegion;
			}

			vkCmdCopyBuffer(copyCmd, src->buffer, dst->buffer, 1, &bufferCopy);

			flushCommandBuffer(copyCmd, queue);
		}

		/** 
		* Create a command pool for allocation command buffers from
		* 
		* @param queueFamilyIndex Family index of the queue to create the command pool for
		* @param createFlags (Optional) Command pool creation flags (Defaults to VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
		*
		* @note Command buffers allocated from the created pool can only be submitted to a queue with the same family index
		*
		* @return A handle to the created command buffer
		*/
		VkCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
		{
			VkCommandPoolCreateInfo cmdPoolInfo = {};
			cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			cmdPoolInfo.queueFamilyIndex = queueFamilyIndex;
			cmdPoolInfo.flags = createFlags;
			VkCommandPool cmdPool;
			VK_CHECK_RESULT(vkCreateCommandPool(logicalDevice, &cmdPoolInfo, nullptr, &cmdPool));
			return cmdPool;
		}

		/**
		* Allocate a command buffer from the command pool
		*
		* @param level Level of the new command buffer (primary or secondary)
		* @param (Optional) begin If true, recording on the new command buffer will be started (vkBeginCommandBuffer) (Defaults to false)
		*
		* @return A handle to the allocated command buffer
		*/
		VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin = false)
		{
			VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(commandPool, level, 1);

			VkCommandBuffer cmdBuffer;
			VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

			// If requested, also start recording for the new command buffer
			if (begin)
			{
				VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
				VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
			}

			return cmdBuffer;
		}
		VkCommandBuffer createComputeCommandBuffer(VkCommandBufferLevel level, bool begin = false)
		{
			VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(compute_commandPool, level, 1);

			VkCommandBuffer cmdBuffer;
			VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

			// If requested, also start recording for the new command buffer
			if (begin)
			{
				VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
				VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
			}

			return cmdBuffer;
		}
		VkCommandBuffer createTransferCommandBuffer(VkCommandBufferLevel level, bool begin = false)
		{
			VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(transfer_commandPool, level, 1);

			VkCommandBuffer cmdBuffer;
			VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, &cmdBuffer));

			// If requested, also start recording for the new command buffer
			if (begin)
			{
				VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
				VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));
			}

			return cmdBuffer;
		}

		/**
		* Finish command buffer recording and submit it to a queue
		*
		* @param commandBuffer Command buffer to flush
		* @param queue Queue to submit the command buffer to 
		* @param free (Optional) Free the command buffer once it has been submitted (Defaults to true)
		*
		* @note The queue that the command buffer is submitted to must be from the same family index as the pool it was allocated from
		* @note Uses a fence to ensure command buffer has finished executing
		*/
		void flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free = true)
		{
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return;
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = vks::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			// Create fence to ensure that the command buffer has finished executing
			VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));
			
			// Submit to the queue
			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
			// Wait for the fence to signal that command buffer has finished executing
			VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

			vkDestroyFence(logicalDevice, fence, nullptr);

			if (free)
			{
				vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
			}
		}

		void flushComputeCommandBuffer(VkCommandBuffer commandBuffer, bool free = true)
		{
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return;
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = vks::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			// Create fence to ensure that the command buffer has finished executing
			VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));

			// Submit to the queue
			VK_CHECK_RESULT(vkQueueSubmit(compute_queue, 1, &submitInfo, fence));
			// Wait for the fence to signal that command buffer has finished executing
			VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

			vkDestroyFence(logicalDevice, fence, nullptr);

			if (free)
			{
				vkFreeCommandBuffers(logicalDevice, compute_commandPool, 1, &commandBuffer);
			}
		}

		void flushTransferCommandBuffer(VkCommandBuffer commandBuffer, bool free = true)
		{
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return;
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

			VkSubmitInfo submitInfo = vks::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &commandBuffer;

			// Create fence to ensure that the command buffer has finished executing
			VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));

			// Submit to the queue
			VK_CHECK_RESULT(vkQueueSubmit(transfer_queue, 1, &submitInfo, fence));
			// Wait for the fence to signal that command buffer has finished executing
			VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

			vkDestroyFence(logicalDevice, fence, nullptr);

			if (free)
			{
				vkFreeCommandBuffers(logicalDevice, transfer_commandPool, 1, &commandBuffer);
			}
		}

		/**
		* Check if an extension is supported by the (physical device)
		*
		* @param extension Name of the extension to check
		*
		* @return True if the extension is supported (present in the list read at device creation time)
		*/
		bool extensionSupported(std::string extension)
		{
			return (std::find(supportedExtensions.begin(), supportedExtensions.end(), extension) != supportedExtensions.end());
		}

	};

	class VTexture {
	public:
		VkSampler sampler;
		VkImage image;
		VkDeviceMemory deviceMemory;
		VkImageView view;
		VkImageView stencil_view;
		VkDescriptorImageInfo descriptor;
		VkFormat format;
		VkImageUsageFlags usage;
		VkAttachmentDescription attdesc;
		VkImageSubresourceRange subresourceRange;
		VulkanDevice *device;
		uint32_t w, h, d, bytes;
		uint32_t mipLevels;
		bool free_sampler;
		bool is_swapchain_images;

		VkDeviceSize memsize;
		unsigned char* mapped;
		//true once the image has been written at least once; until then its actual
		//layout is VK_IMAGE_LAYOUT_UNDEFINED regardless of descriptor.imageLayout
		bool uploaded;

		VTexture()
		{
			sampler = VK_NULL_HANDLE;
			image = VK_NULL_HANDLE;
			deviceMemory = VK_NULL_HANDLE;
			view = VK_NULL_HANDLE;
			stencil_view = VK_NULL_HANDLE;
			device = VK_NULL_HANDLE;
			free_sampler = true;
			is_swapchain_images = false;
			memsize = 0;
			mapped = nullptr;
			w = 0; h = 0; d = 0;
			bytes = 0;
			mipLevels = 1;
			format = VK_FORMAT_UNDEFINED;
			usage = 0;
			descriptor = {};
			attdesc = {};
			subresourceRange = {};
			uploaded = false;
		}

		~VTexture()
		{
			destroy();
		}

		//destroy or (while frames may be in flight) defer destruction of the raw
		//handles to the device's per-frame-slot deletion queue. Members are nulled
		//immediately either way. Definition follows VulkanDevice (needs retire()).
		void destroy();

		//immediate destruction of the raw handles (used by the deferred lambda and
		//during device shutdown)
		void destroy_handles_now(VkImageView v, VkImageView sv, VkImage img, VkSampler smp, VkDeviceMemory mem)
		{
			if (v != VK_NULL_HANDLE)
				vkDestroyImageView(device->logicalDevice, v, nullptr);
			if (sv != VK_NULL_HANDLE)
				vkDestroyImageView(device->logicalDevice, sv, nullptr);
			if (img != VK_NULL_HANDLE)
				vkDestroyImage(device->logicalDevice, img, nullptr);
			if (smp != VK_NULL_HANDLE)
				vkDestroySampler(device->logicalDevice, smp, nullptr);
			if (mem != VK_NULL_HANDLE)
				vkFreeMemory(device->logicalDevice, mem, nullptr);
		}

	};

	class VFrameBuffer {
	public:
		VkFramebuffer framebuffer;
		VkRenderPass renderPass;
		VulkanDevice *device;
		uint32_t w, h;
		std::vector<std::shared_ptr<VTexture>> attachments;
		bool delete_renderpass;

		VFrameBuffer()
		{
			framebuffer = VK_NULL_HANDLE;
			renderPass = VK_NULL_HANDLE;
			device = VK_NULL_HANDLE;
			w = 0;
			h = 0;
			delete_renderpass = false;
		}

		~VFrameBuffer()
		{
			destroy();
		}

		//destroy or (while frames may be in flight) defer destruction of the raw
		//handles to the device's deletion queue. Definition follows VulkanDevice.
		void destroy();

		uint32_t addAttachment(std::shared_ptr<VTexture> &attachment)
		{
			// Fill attachment description
			attachment->attdesc = {};
			attachment->attdesc.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment->attdesc.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment->attdesc.storeOp = (attachment->usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment->attdesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment->attdesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment->attdesc.format = attachment->format;
			attachment->attdesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			// Final layout
			// If not, final layout depends on attachment type
			attachment->attdesc.finalLayout = attachment->descriptor.imageLayout;

			attachments.push_back(attachment);

			return static_cast<uint32_t>(attachments.size() - 1);
		}

		VkResult setup(VkRenderPass render_pass)
		{
			renderPass = render_pass;

			// Collect attachment references
			std::vector<VkAttachmentReference> colorReferences;
			VkAttachmentReference depthReference = {};
			bool hasDepth = false;
			bool hasColor = false;

			uint32_t attachmentIndex = 0;

			for (auto& attachment : attachments)
			{
				if (attachment->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
				{
					// Only one depth attachment allowed
					assert(!hasDepth);
					depthReference.attachment = attachmentIndex;
					depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					hasDepth = true;
				}
				else
				{
					colorReferences.push_back({ attachmentIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
					hasColor = true;
				}
				attachmentIndex++;
			};

			std::vector<VkImageView> attachmentViews;
			for (auto attachment : attachments)
			{
				attachmentViews.push_back(attachment->view);
			}

			// Find. max number of layers across attachments
			uint32_t maxLayers = 0;
			for (auto attachment : attachments)
			{
				if (attachment->subresourceRange.layerCount > maxLayers)
				{
					maxLayers = attachment->subresourceRange.layerCount;
				}
			}

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.pAttachments = attachmentViews.data();
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
			framebufferInfo.width = w;
			framebufferInfo.height = h;
			framebufferInfo.layers = maxLayers;
			VK_CHECK_RESULT(vkCreateFramebuffer(device->logicalDevice, &framebufferInfo, nullptr, &framebuffer));

			delete_renderpass = false;

			return VK_SUCCESS;
		}

		void replaceRenderPass(VkRenderPass render_pass)
		{
			if (render_pass != renderPass)
			{
				destroy();
				setup(render_pass);
			}
		}

		/**
		* Creates a default render pass setup with one sub pass
		*
		* @return VK_SUCCESS if all resources have been created successfully
		*/
		VkResult createRenderPass()
		{
			std::vector<VkAttachmentDescription> attachmentDescriptions;
			for (auto& attachment : attachments)
			{
				attachmentDescriptions.push_back(attachment->attdesc);
			};

			// Collect attachment references
			std::vector<VkAttachmentReference> colorReferences;
			VkAttachmentReference depthReference = {};
			bool hasDepth = false;
			bool hasColor = false;

			uint32_t attachmentIndex = 0;

			for (auto& attachment : attachments)
			{
				if (attachment->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
				{
					// Only one depth attachment allowed
					assert(!hasDepth);
					depthReference.attachment = attachmentIndex;
					depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
					hasDepth = true;
				}
				else
				{
					colorReferences.push_back({ attachmentIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
					hasColor = true;
				}
				attachmentIndex++;
			};

			// Default render pass setup uses only one subpass
			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			if (hasColor)
			{
				subpass.pColorAttachments = colorReferences.data();
				subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
			}
			if (hasDepth)
			{
				subpass.pDepthStencilAttachment = &depthReference;
			}

			// Use subpass dependencies for attachment layout transitions
			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			// Create render pass
			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescriptions.data();
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(device->logicalDevice, &renderPassInfo, nullptr, &renderPass));

			std::vector<VkImageView> attachmentViews;
			for (auto attachment : attachments)
			{
				attachmentViews.push_back(attachment->view);
			}

			// Find. max number of layers across attachments
			uint32_t maxLayers = 0;
			for (auto attachment : attachments)
			{
				if (attachment->subresourceRange.layerCount > maxLayers)
				{
					maxLayers = attachment->subresourceRange.layerCount;
				}
			}

			VkFramebufferCreateInfo framebufferInfo = {};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = renderPass;
			framebufferInfo.pAttachments = attachmentViews.data();
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
			framebufferInfo.width = w;
			framebufferInfo.height = h;
			framebufferInfo.layers = maxLayers;
			VK_CHECK_RESULT(vkCreateFramebuffer(device->logicalDevice, &framebufferInfo, nullptr, &framebuffer));

			delete_renderpass = true;

			return VK_SUCCESS;
		}
	};

	class VSemaphore {
	public:
		VkSemaphore vksemaphore;
		VulkanDevice* device;
		VSemaphore()
		{
			device = nullptr;
			vksemaphore = VK_NULL_HANDLE;
		}
		VSemaphore(VulkanDevice *dev) 
		{
			device = nullptr;
			vksemaphore = VK_NULL_HANDLE;
			init(dev);
		}
		~VSemaphore()
		{
			destroy();
		}
		void init(VulkanDevice* dev)
		{
			if (vksemaphore != VK_NULL_HANDLE)
				destroy();
			device = dev;
			VkSemaphoreCreateInfo semaphoreInfo = vks::initializers::semaphoreCreateInfo();
			VK_CHECK_RESULT(vkCreateSemaphore(device->logicalDevice, &semaphoreInfo, nullptr, &vksemaphore));
		}
		void destroy()
		{
			if (vksemaphore != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device->logicalDevice, vksemaphore, nullptr);
				vksemaphore = VK_NULL_HANDLE;
			}
		}
	};

	struct VulkanSemaphoreSettings {
		uint32_t waitSemaphoreCount = 0;
		uint32_t signalSemaphoreCount = 0;
		VkSemaphore* waitSemaphores = nullptr;
		VkSemaphore* signalSemaphores = nullptr;
	};

	//deferred destruction: while frames may be in flight, releasing a texture or
	//framebuffer must not free the underlying Vulkan handles immediately (a previous
	//frame's command buffers may still reference them). The handles are captured and
	//queued on the device's current frame slot; the queue runs once that slot's
	//frame fence has signaled. During shutdown the handles are freed immediately.
	inline void VTexture::destroy()
	{
		VkImageView v = is_swapchain_images ? VK_NULL_HANDLE : view;
		VkImageView sv = is_swapchain_images ? VK_NULL_HANDLE : stencil_view;
		VkImage img = is_swapchain_images ? VK_NULL_HANDLE : image;
		VkSampler smp = free_sampler ? sampler : VK_NULL_HANDLE;
		VkDeviceMemory mem = is_swapchain_images ? VK_NULL_HANDLE : deviceMemory;
		view = VK_NULL_HANDLE;
		stencil_view = VK_NULL_HANDLE;
		image = VK_NULL_HANDLE;
		sampler = VK_NULL_HANDLE;
		deviceMemory = VK_NULL_HANDLE;

		if (v == VK_NULL_HANDLE && sv == VK_NULL_HANDLE && img == VK_NULL_HANDLE &&
			smp == VK_NULL_HANDLE && mem == VK_NULL_HANDLE)
			return;
		if (!device)
			return;

		if (device->m_in_shutdown)
		{
			destroy_handles_now(v, sv, img, smp, mem);
		}
		else
		{
			VulkanDevice* dev = device;
			device->retire([dev, v, sv, img, smp, mem]() {
				if (v != VK_NULL_HANDLE)
					vkDestroyImageView(dev->logicalDevice, v, nullptr);
				if (sv != VK_NULL_HANDLE)
					vkDestroyImageView(dev->logicalDevice, sv, nullptr);
				if (img != VK_NULL_HANDLE)
					vkDestroyImage(dev->logicalDevice, img, nullptr);
				if (smp != VK_NULL_HANDLE)
					vkDestroySampler(dev->logicalDevice, smp, nullptr);
				if (mem != VK_NULL_HANDLE)
					vkFreeMemory(dev->logicalDevice, mem, nullptr);
			});
		}
	}

	inline void VFrameBuffer::destroy()
	{
		VkRenderPass rp = delete_renderpass ? renderPass : VK_NULL_HANDLE;
		VkFramebuffer fb = framebuffer;
		if (delete_renderpass)
			renderPass = VK_NULL_HANDLE;
		framebuffer = VK_NULL_HANDLE;

		if (rp == VK_NULL_HANDLE && fb == VK_NULL_HANDLE)
			return;
		if (!device)
			return;

		if (device->m_in_shutdown)
		{
			if (fb != VK_NULL_HANDLE)
				vkDestroyFramebuffer(device->logicalDevice, fb, nullptr);
			if (rp != VK_NULL_HANDLE)
				vkDestroyRenderPass(device->logicalDevice, rp, nullptr);
		}
		else
		{
			VulkanDevice* dev = device;
			device->retire([dev, fb, rp]() {
				if (fb != VK_NULL_HANDLE)
					vkDestroyFramebuffer(dev->logicalDevice, fb, nullptr);
				if (rp != VK_NULL_HANDLE)
					vkDestroyRenderPass(dev->logicalDevice, rp, nullptr);
			});
		}
	}

}
