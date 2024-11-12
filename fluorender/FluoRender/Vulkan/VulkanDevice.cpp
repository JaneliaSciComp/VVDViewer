#include "VulkanDevice.hpp"
#include "vk_format_utils.h"
#include <thread>

#ifdef _WIN32
#include <omp.h>
#endif

namespace vks
{
	void VulkanDevice::setMemoryLimit(double limit)
	{
		uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
        std::vector<std::string> supportedInstanceExtensions;
        for (auto ext : extensions)
			supportedInstanceExtensions.push_back(ext.extensionName);
        
        VkDeviceSize cur_mem_lim = 0;

		if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), "VK_KHR_get_physical_device_properties2") != supportedInstanceExtensions.end())
		{
			VkPhysicalDeviceMemoryBudgetPropertiesEXT mem_bprop;
			VkPhysicalDeviceMemoryProperties2 mem_prop2;

			mem_prop2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			mem_prop2.pNext = &mem_bprop;

			mem_bprop.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
			mem_bprop.pNext = nullptr;

			vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &mem_prop2);

			for (int i = 0; i < mem_prop2.memoryProperties.memoryHeapCount; i++)
			{
				if (mem_prop2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					if (extensionSupported("VK_EXT_memory_budget"))
					{
						VkDeviceSize heap_budget = mem_bprop.heapBudget[i];
						VkDeviceSize heap_usage = mem_bprop.heapUsage[i];
						if (cur_mem_lim < heap_budget - heap_usage)
							cur_mem_lim = heap_budget - heap_usage;
					}
					else
					{
						VkDeviceSize heap_size = mem_prop2.memoryProperties.memoryHeaps[i].size;
						if (cur_mem_lim < heap_size)
							cur_mem_lim = heap_size;
					}
				}
			}
		}
		else
		{
			VkPhysicalDeviceMemoryProperties mem_prop;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mem_prop);
			for (int i = 0; i < mem_prop.memoryHeapCount; i++)
			{
				if (mem_prop.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				{
					VkDeviceSize heap_size = mem_prop.memoryHeaps[i].size;
					if (cur_mem_lim < heap_size)
						cur_mem_lim = heap_size;
				}
			}
		}

		double dev_max_mem = (double)cur_mem_lim / 1024.0 / 1024.0;
		if (dev_max_mem >= 4096.0) dev_max_mem -= 1024.0;
		else if (dev_max_mem >= 1024.0) dev_max_mem -= 512.0;
		else dev_max_mem *= 0.8;

		double new_mem_limit = 0.0;

		if (dev_max_mem <= 0.0 || (limit > 0 && limit < dev_max_mem))
			new_mem_limit = limit;
		else
			new_mem_limit = dev_max_mem;

		use_mem_limit = true;
		available_mem = new_mem_limit - mem_limit + available_mem;
		mem_limit = new_mem_limit;
	}

	void VulkanDevice::clear_tex_pool() 
	{
		for (int j = int(tex_pool.size() - 1); j >= 0; j--)
			available_mem += tex_pool[j].tex->memsize / 1.04e6;
		tex_pool.clear();

		//available_mem = mem_limit;
	}

	bool VulkanDevice::return_brick(const TexParam &texp)
	{
		FLIVR::TextureBrick* b = texp.brick;
		int c = texp.comp;
		if (!b->dirty(c) || c < 0 || c >= TEXTURE_MAX_COMPONENTS)
			return false;

		VkOffset3D offset;
		uint64_t ypitch;
		uint64_t zpitch;

		offset.x = b->ox();
		offset.y = b->oy();
		offset.z = b->oz();
		ypitch = (VkDeviceSize)b->sx() * b->nb(c);
		zpitch = (VkDeviceSize)b->sy() * b->sx() * b->nb(c);

		vks::VulkanDevice *device = texp.tex->device;
		void* data = b->get_nrrd_raw(c)->data;
		device->DownloadTexture3D(texp.tex, data, offset, ypitch, zpitch);

		b->set_dirty(c, false);

		return true;
	}

	void VulkanDevice::update_texpool()
	{
		for (int j=int(tex_pool.size()-1); j>=0; j--)
		{
			if (tex_pool[j].delayed_del && tex_pool[j].tex)
			{
				//save before deletion
				return_brick(tex_pool[j]);
				if (tex_pool[j].comp >= 0 && tex_pool[j].comp < TEXTURE_MAX_COMPONENTS && tex_pool[j].tex->bytes > 0)
					available_mem += tex_pool[j].tex->memsize / 1.04e6;
				tex_pool.erase(tex_pool.begin()+j);
			}
		}
	}

	struct BrickDist
	{
		unsigned int index;    //index of the brick in current tex pool
		FLIVR::TextureBrick* brick;
		double dist;      //distance to another brick
	};
	bool brick_sort(const BrickDist& bd1, const BrickDist& bd2)
	{
		return bd1.dist > bd2.dist;
	}

	int VulkanDevice::check_swap_memory(FLIVR::TextureBrick* brick, int c, bool *swapped)
	{
		unsigned int i;
		double new_mem = (VkDeviceSize)brick->nx()*brick->ny()*brick->nz()*brick->nb(c)/1.04e6;

		int overwrite = -1;

		if (use_mem_limit)
		{
			if (available_mem >= new_mem)
			{
				if (swapped)
					*swapped = false;
				return overwrite;
			}
		}
		else
		{
			if (swapped)
				*swapped = false;
			return overwrite;
		}

		VK_CHECK_RESULT(vkQueueWaitIdle(queue));
		if (transfer_queue != queue)
			VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));

		if (swapped)
			*swapped = true;

		std::vector<BrickDist> bd_list;
		BrickDist bd;
		//generate a list of bricks and their distances to the new brick
		for (i=0; i<tex_pool.size(); i++)
		{
			bd.index = i;
			bd.brick = tex_pool[i].brick;
			//calculate the distance
			bd.dist = brick->bbox().distance(bd.brick->bbox());
			bd_list.push_back(bd);
		}

		//release bricks far away
		double est_avlb_mem = available_mem;
		int comp;
		if (bd_list.size() > 0)
		{
			//sort from farthest to closest
			std::sort(bd_list.begin(), bd_list.end(), brick_sort);

			std::vector<BrickDist> bd_undisp;
			std::vector<BrickDist> bd_saved;
			std::vector<BrickDist> bd_others;
			for (i=0; i<bd_list.size(); i++)
			{
				FLIVR::TextureBrick* b = bd_list[i].brick;
				if (b->is_tex_deletion_prevented())
					bd_saved.push_back(bd_list[i]);
				else if (!b->get_disp())
					bd_undisp.push_back(bd_list[i]);
				else
					bd_others.push_back(bd_list[i]);
			}

			//overwrite or remove undisplayed bricks.
			//try to overwrite
			for (i=0; i<bd_undisp.size(); i++)
			{
				TexParam &texp = tex_pool[bd_undisp[i].index];
				if( texp.tex
					&& texp.comp == c
					&& brick->nx() == texp.tex->w
					&& brick->ny() == texp.tex->h
					&& brick->nz() == texp.tex->d
					&& brick->nb(c) == texp.tex->bytes
					&& brick->tex_format(c) == texp.tex->format
					&& !brick->dirty(c))
				{
					//over write a texture that has exact same properties.
					overwrite = bd_undisp[i].index;
					break;
				}
			}
			if (overwrite >= 0)
			{
				//save before deletion
				return_brick(tex_pool[overwrite]);
				return overwrite;
			}
			//remove
			std::vector<int> deleted;
			for (i=0; i<bd_undisp.size(); i++)
			{
				TexParam &texp = tex_pool[bd_undisp[i].index];
				texp.delayed_del = true;
				comp = texp.comp;
				deleted.push_back(bd_undisp[i].index);
				double released_mem = texp.tex->memsize / 1.04e6;
				est_avlb_mem += released_mem;
				if (est_avlb_mem >= new_mem)
					break;
			}

			//overwrite or remove displayed bricks far away.
			if (est_avlb_mem < new_mem)
			{
				//try to overwrite
				for (i=0; i<bd_others.size(); i++)
				{
					TexParam &texp = tex_pool[bd_others[i].index];
					if( texp.tex
						&& texp.comp == c
						&& brick->nx() == texp.tex->w
						&& brick->ny() == texp.tex->h
						&& brick->nz() == texp.tex->d
						&& brick->nb(c) == texp.tex->bytes
						&& brick->tex_format(c) == texp.tex->format
						&& !brick->dirty(c))
					{
						//over write a texture that has exact same properties.
						overwrite = bd_others[i].index;
						break;
					}
				}
				if (overwrite >= 0)
				{
					//save before deletion
					return_brick(tex_pool[overwrite]);
				}
				else
				{
					//remove
					for (i=0; i<bd_others.size(); i++)
					{
						TexParam &texp = tex_pool[bd_others[i].index];
						texp.delayed_del = true;
						comp = texp.comp;
						deleted.push_back(bd_others[i].index);
						double released_mem = texp.tex->memsize / 1.04e6;
						est_avlb_mem += released_mem;
						if (est_avlb_mem >= new_mem)
							break;
					}
				}
			}

			update_texpool();
			if (use_mem_limit)
				available_mem = est_avlb_mem;

			if (overwrite >= 0)
			{
				for (auto i : deleted)
					if (i < overwrite) overwrite--;
			}
		}

		return overwrite;
	}

	int VulkanDevice::findTexInPool(FLIVR::TextureBrick* b, int c, int w, int h, int d, int bytes, VkFormat format)
	{
		int count = 0;
		for (auto &e : tex_pool)
		{
			if (e.tex && b == e.brick && c == e.comp &&
				w == e.tex->w && h == e.tex->h && d == e.tex->d && bytes == e.tex->bytes &&
				format == e.tex->format)
			{
				//found!
				return count;
			}
			count++;
		}

		return -1;
	}

	int VulkanDevice::GenTexture3D_pool(VkFormat format, VkFilter filter, FLIVR::TextureBrick *b, int comp)
	{
		std::shared_ptr<VTexture> ret;

		ret = GenTexture3D(format, filter, b->nx(), b->ny(), b->nz());

		if (ret)
		{
			vks::TexParam p = vks::TexParam(comp, ret);
			p.brick = b;
			tex_pool.push_back(p);
			available_mem -= ret->memsize / 1.04e6;

			return int(tex_pool.size()) - 1;
		}
		else
			return -1;
	}

	void VulkanDevice::createPipelineCache()
	{
		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		VK_CHECK_RESULT(vkCreatePipelineCache(logicalDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
	}

	void VulkanDevice::setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = 
			vks::initializers::descriptorPoolCreateInfo(
			static_cast<uint32_t>(poolSizes.size()),
			poolSizes.data(),
			100);
		//descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;

		VK_CHECK_RESULT(vkCreateDescriptorPool(logicalDevice, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void VulkanDevice::PrepareMainRenderBuffers()
	{
		VkCommandBuffer cmdbuf = createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		m_cmdbufs.push_back(cmdbuf);
		VkCommandBuffer trans_cmdbuf = createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		m_trans_cmdbufs.push_back(trans_cmdbuf);

		VkDeviceSize max_ubo_range = properties.limits.maxUniformBufferRange;
		VkDeviceSize ubo_size = 1024;//max_ubo_range > 65536 ? 65536 : max_ubo_range;
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&m_ubo,
				ubo_size
			)
		);
		m_ubo.map();

		VkDeviceSize buf_size = 1024;
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&m_vbuf,
				buf_size
			)
		);
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&m_ibuf,
				buf_size
			)
		);
	}
	void VulkanDevice::ResetMainRenderBuffers()
	{
		m_cur_cmdbuf_id = 0;
		m_cur_trans_cmdbuf_id = 0;
		m_ubo_offset = 0;
		m_vbuf_offset = 0;
		m_ibuf_offset = 0;
		if (m_ubos.size() > 0)
		{
			VkDeviceSize newsize = m_ubo.size;
			for (auto b : m_ubos)
			{
				newsize += b.size;
				b.destroy();
			}
			m_ubos.clear();

			m_ubo.destroy();
			VK_CHECK_RESULT(
				createBuffer(
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					&m_ubo,
					newsize
				)
			);
			m_ubo.map();
		}
		if (m_vbufs.size() > 0)
		{
			VkDeviceSize newsize = m_vbuf.size;
			for (auto b : m_vbufs)
			{
				newsize += b.size;
				b.destroy();
			}
			m_vbufs.clear();

			m_vbuf.destroy();
			VK_CHECK_RESULT(
				createBuffer(
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&m_vbuf,
					newsize
				)
			);
		}
		if (m_ibufs.size() > 0)
		{
			VkDeviceSize newsize = m_ibuf.size;
			for (auto b : m_ibufs)
			{
				newsize += b.size;
				b.destroy();
			}
			m_ibufs.clear();

			m_ibuf.destroy();
			VK_CHECK_RESULT(
				createBuffer(
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&m_ibuf,
					newsize
				)
			);
		}
	}
	void VulkanDevice::GetNextUniformBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		offset = m_ubo_offset;
		
		if (m_ubo.alignment > 0)
			req_size = (req_size + m_ubo.alignment - 1) & ~(m_ubo.alignment - 1);
		m_ubo_offset += req_size;

		if (m_ubo.size >= m_ubo_offset + req_size)
			m_ubo_offset += req_size;
		else
		{
			offset = 0;
			m_ubos.push_back(m_ubo);
			VK_CHECK_RESULT(
				createBuffer(
					VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
					&m_ubo,
					req_size
				)
			);
			m_ubo_offset = req_size;
			m_ubo.map();
		}

		buf = m_ubo;
		buf.descriptor.offset = offset;
		buf.descriptor.range = req_size;
	}
	VkDeviceSize VulkanDevice::GetCurrentUniformBufferOffset()
	{
		return m_ubo_offset;
	}
	void VulkanDevice::GetNextVertexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		offset = m_vbuf_offset;

		if (m_vbuf.alignment > 0)
			req_size = (req_size + m_vbuf.alignment - 1) & ~(m_vbuf.alignment - 1);

		if (m_vbuf.size >= m_vbuf_offset + req_size)
			m_vbuf_offset += req_size;
		else
		{
			offset = 0;
			m_vbufs.push_back(m_vbuf);
			VK_CHECK_RESULT(
				createBuffer(
					VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&m_vbuf,
					req_size
				)
			);
			m_vbuf_offset = req_size;
		}
		buf = m_vbuf;
		buf.descriptor.buffer = buf.buffer;
		buf.descriptor.offset = offset;
		buf.descriptor.range = req_size;
	}
	void VulkanDevice::GetNextIndexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		offset = m_ibuf_offset;

		if (m_ibuf.alignment > 0)
			req_size = (req_size + m_ibuf.alignment - 1) & ~(m_ibuf.alignment - 1);

		if (m_ibuf.size >= m_ibuf_offset + req_size)
			m_ibuf_offset += req_size;
		else
		{
			offset = 0;
			m_ibufs.push_back(m_ibuf);
			VK_CHECK_RESULT(
				createBuffer(
					VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
					&m_ibuf,
					req_size
				)
			);
			m_ibuf_offset = req_size;
		}
		buf = m_ibuf;
		buf.descriptor.buffer = buf.buffer;
		buf.descriptor.offset = offset;
		buf.descriptor.range = req_size;
	}
	VkDeviceSize VulkanDevice::GetCurrentVertexBufferOffset()
	{
		return m_vbuf_offset;
	}
	VkDeviceSize VulkanDevice::GetCurrentIndexBufferOffset()
	{
		return m_ibuf_offset;
	}

	VkCommandBuffer VulkanDevice::GetNextCommandBuffer()
	{
		if (m_cur_cmdbuf_id >= m_cmdbufs.size())
		{
			VkCommandBuffer cmdbuf = createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			m_cmdbufs.push_back(cmdbuf);
		}

		return m_cmdbufs[m_cur_cmdbuf_id++];
	}

	VkCommandBuffer VulkanDevice::GetNextTransferCommandBuffer()
	{
		if (m_cur_trans_cmdbuf_id >= m_trans_cmdbufs.size())
		{
			VkCommandBuffer cmdbuf = createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			m_trans_cmdbufs.push_back(cmdbuf);
		}

		return m_trans_cmdbufs[m_cur_trans_cmdbuf_id++];
	}

	void VulkanDevice::prepareSamplers()
	{
		// Create samplers
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(logicalDevice, &sampler, nullptr, &nearest_sampler));

		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(logicalDevice, &sampler, nullptr, &linear_sampler));
	}

	std::shared_ptr<VTexture> VulkanDevice::GenTexture2D(VkFormat format, VkFilter filter, uint32_t w, uint32_t h, VkImageUsageFlags usage)
	{
		std::shared_ptr<VTexture> ret = std::make_shared<VTexture>();

		ret->w = w;
		ret->h = h;
		ret->d = 1;
		ret->bytes = FormatTexelSize(format);
		ret->mipLevels = 1;
		ret->format = format;
		ret->usage = usage;
		ret->image = VK_NULL_HANDLE;
		ret->deviceMemory = VK_NULL_HANDLE;
		ret->sampler = VK_NULL_HANDLE;
		ret->view = VK_NULL_HANDLE;
		ret->device = this;

		// Format support check
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(ret->device->physicalDevice, ret->format, &formatProperties);
		// Check if format supports transfer
		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
		{
			std::cout << "Error: Device does not support flag TRANSFER_DST for selected texture format!" << std::endl;
			return std::move(ret);
		}
		// Check if GPU supports requested 2D texture dimensions
		uint32_t maxImageDimension2D(ret->device->properties.limits.maxImageDimension2D);
		if (w > maxImageDimension2D || h > maxImageDimension2D)
		{
			std::cout << "Error: Requested texture dimensions is greater than supported 2D texture dimension!" << std::endl;
			return std::move(ret);
		}

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = ret->format;
		imageCreateInfo.mipLevels = ret->mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.extent.width = ret->w;
		imageCreateInfo.extent.height = ret->h;
		imageCreateInfo.extent.depth = ret->d;
		// Set initial layout of the image to undefined
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = usage;
		if (uniqueQueueFamilyCount > 1)
		{
			imageCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			imageCreateInfo.pQueueFamilyIndices = uniqueQueueFamilyIndices;
			imageCreateInfo.queueFamilyIndexCount = uniqueQueueFamilyCount;
		}
		else
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateImage(ret->device->logicalDevice, &imageCreateInfo, nullptr, &ret->image));

		// Device local memory to back up image
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs = {};
		vkGetImageMemoryRequirements(ret->device->logicalDevice, ret->image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = ret->device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(ret->device->logicalDevice, &memAllocInfo, nullptr, &ret->deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(ret->device->logicalDevice, ret->image, ret->deviceMemory, 0));

		// Create sampler
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = filter;
		sampler.minFilter = filter;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.mipLodBias = 0.0f;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.maxAnisotropy = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(ret->device->logicalDevice, &sampler, nullptr, &ret->sampler));

		// Create image view
		VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
		view.image = ret->image;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = ret->format;
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		else
		{
			view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
			view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = 1;
		ret->subresourceRange = view.subresourceRange;
		VK_CHECK_RESULT(vkCreateImageView(ret->device->logicalDevice, &view, nullptr, &ret->view));

		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D16_UNORM_S8_UINT)
			{
				view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
				VK_CHECK_RESULT(vkCreateImageView(ret->device->logicalDevice, &view, nullptr, &ret->stencil_view));
				ret->subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}

		// Fill image descriptor image info to be used descriptor set setup
		ret->descriptor.imageLayout = 
			(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ret->descriptor.imageView = ret->view;
		ret->descriptor.sampler = ret->sampler;

		return std::move(ret);
	}

	std::shared_ptr<VTexture> VulkanDevice::GenTexture3D(VkFormat format, VkFilter filter, uint32_t w, uint32_t h, uint32_t d)
	{
		std::shared_ptr<VTexture> ret = std::make_shared<VTexture>();

		ret->w = w;
		ret->h = h;
		ret->d = d;
		ret->bytes = FormatTexelSize(format);
		ret->mipLevels = 1;
		ret->format = format;
		ret->usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		ret->image = VK_NULL_HANDLE;
		ret->deviceMemory = VK_NULL_HANDLE;
		ret->sampler = VK_NULL_HANDLE;
		ret->view = VK_NULL_HANDLE;
		ret->device = this;

		if (format != VK_FORMAT_BC4_SNORM_BLOCK && format != VK_FORMAT_BC4_UNORM_BLOCK)
			ret->usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		else
			ret->bytes = 1;
		
		// Format support check
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(ret->device->physicalDevice, ret->format, &formatProperties);
		// Check if format supports transfer
		if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
		{
			std::cout << "Error: Device does not support flag TRANSFER_DST for selected texture format!" << std::endl;
			return std::move(ret);
		}
		// Check if GPU supports requested 3D texture dimensions
		uint32_t maxImageDimension3D(ret->device->properties.limits.maxImageDimension3D);
		if (w > maxImageDimension3D || h > maxImageDimension3D)
		{
			std::cout << "Error: Requested texture dimensions is greater than supported 3D texture dimension!" << std::endl;
			return std::move(ret);
		}

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
		imageCreateInfo.format = ret->format;
		imageCreateInfo.mipLevels = ret->mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.extent.width = ret->w;
		imageCreateInfo.extent.height = ret->h;
		imageCreateInfo.extent.depth = ret->d;
		// Set initial layout of the image to undefined
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = ret->usage;
		if (uniqueQueueFamilyCount > 1)
		{
			imageCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			imageCreateInfo.pQueueFamilyIndices = uniqueQueueFamilyIndices;
			imageCreateInfo.queueFamilyIndexCount = uniqueQueueFamilyCount;
		}
		else
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK_RESULT(vkCreateImage(ret->device->logicalDevice, &imageCreateInfo, nullptr, &ret->image));

		// Device local memory to back up image
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs = {};
		vkGetImageMemoryRequirements(ret->device->logicalDevice, ret->image, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		ret->memsize = memReqs.size;
		memAllocInfo.memoryTypeIndex = ret->device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(ret->device->logicalDevice, &memAllocInfo, nullptr, &ret->deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(ret->device->logicalDevice, ret->image, ret->deviceMemory, 0));

		// Set sampler
		if (filter == VK_FILTER_LINEAR)
			ret->sampler = linear_sampler;
		else
			ret->sampler = nearest_sampler;

		ret->free_sampler = false;

		// Create image view
		VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
		view.image = ret->image;
		view.viewType = VK_IMAGE_VIEW_TYPE_3D;
		view.format = ret->format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.baseMipLevel = 0;
		view.subresourceRange.baseArrayLayer = 0;
		view.subresourceRange.layerCount = 1;
		view.subresourceRange.levelCount = 1;
		ret->subresourceRange = view.subresourceRange;
		VK_CHECK_RESULT(vkCreateImageView(ret->device->logicalDevice, &view, nullptr, &ret->view));

		// Fill image descriptor image info to be used descriptor set setup
		ret->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		ret->descriptor.imageView = ret->view;
		ret->descriptor.sampler = ret->sampler;

		return std::move(ret);
	}

	void VulkanDevice::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = usage;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to create buffer!");
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(logicalDevice, buffer, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = getMemoryType(memRequirements.memoryTypeBits, properties);

		VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &bufferMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(logicalDevice, buffer, bufferMemory, 0));
	}

	void VulkanDevice::checkStagingBuffer(VkDeviceSize size)
	{
		if (size > staging_buf.size)
		{
			staging_buf.unmap();
			staging_buf.destroy();
		}
		if (staging_buf.buffer == VK_NULL_HANDLE)
		{
			createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&staging_buf, size);
			// Map persistent
			VK_CHECK_RESULT(staging_buf.map());
		}
	}

    //long long milliseconds_now() {
    //    static LARGE_INTEGER s_frequency;
    //    static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
    //    if (s_use_qpc) {
    //        LARGE_INTEGER now;
    //        QueryPerformanceCounter(&now);
    //        return (1000LL * now.QuadPart) / s_frequency.QuadPart;
    //    }
    //    else {
    //        return GetTickCount64();
    //    }
    //}

	bool VulkanDevice::UploadTexture3D(
		const std::shared_ptr<VTexture> &tex, void *data, VkOffset3D offset, uint32_t ypitch, uint32_t zpitch,
		bool flush, vks::VulkanSemaphoreSettings* semaphore, bool sync 
	)
	{
		static int bufsize = 4096;

		VkDeviceSize texMemSize = tex->memsize;

		if (sync)
			VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));

		checkStagingBuffer(texMemSize);

		//uint64_t st_time, ed_time;
		//char dbgstr[50];
		//st_time = milliseconds_now();

		// Copy texture data into staging buffer
		if (tex->format != VK_FORMAT_BC4_UNORM_BLOCK)
		{
			uint64_t poffset = (VkDeviceSize)offset.z * zpitch + (VkDeviceSize)offset.y * ypitch + offset.x * (VkDeviceSize)tex->bytes;
			uint64_t dst_ypitch = (VkDeviceSize)tex->w * (VkDeviceSize)tex->bytes;
			uint64_t dst_zpitch = (VkDeviceSize)tex->w * (VkDeviceSize)tex->h * (VkDeviceSize)tex->bytes;
			unsigned char* dst = (unsigned char*)staging_buf.mapped;
			unsigned char* src = (unsigned char*)data + poffset;

			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads > 8) nthreads = 8;
			std::vector<std::thread> threads(nthreads);
			int grain_size = tex->d / nthreads;
			auto worker = [&zpitch, &dst_zpitch, &ypitch, &dst_ypitch](unsigned char* dst, unsigned char* src, int d, int texh) {
				int xite = dst_ypitch / bufsize;
				unsigned char* src_p, * dst_p, * tmp_xsrc_p, * tmp_xdst_p;
				for (uint32_t z = 0; z < d; z++)
				{
					src_p = src + (VkDeviceSize)z * zpitch;
					dst_p = dst + (VkDeviceSize)z * dst_zpitch;

					for (uint32_t y = 0; y < texh; y++)
					{
						tmp_xsrc_p = src_p + (VkDeviceSize)y * ypitch;
						tmp_xdst_p = dst_p + (VkDeviceSize)y * dst_ypitch;
						for (int x = 0; x < xite - 1; x++)
						{
							memcpy(tmp_xdst_p, tmp_xsrc_p, bufsize);
							tmp_xdst_p += bufsize;
							tmp_xsrc_p += bufsize;
						}
						memcpy(tmp_xdst_p, tmp_xsrc_p, dst_ypitch - bufsize * (xite >= 1 ? xite - 1 : 0));
					}
				}
			};
			for (uint32_t i = 0; i < nthreads - 1; i++)
			{
				threads[i] = std::thread(worker, dst, src, grain_size, tex->h);
				dst += grain_size * dst_zpitch;
				src += grain_size * zpitch;
			}
			threads.back() = std::thread(worker, dst, src, tex->d - grain_size * (nthreads - 1), tex->h);
			for (auto&& i : threads) {
				i.join();
			}
		}
		else
		{
			//BC4 Compression
			uint64_t bnum_x = ((VkDeviceSize)tex->w % 4 > 0 ? (VkDeviceSize)tex->w / 4 + 1 : (VkDeviceSize)tex->w / 4);
			uint64_t bnum_y = ((VkDeviceSize)tex->h % 4 > 0 ? (VkDeviceSize)tex->h / 4 + 1 : (VkDeviceSize)tex->h / 4);
			uint64_t poffset = (VkDeviceSize)offset.z * zpitch + (VkDeviceSize)offset.y * ypitch + offset.x * (VkDeviceSize)tex->bytes;
			uint64_t dst_ypitch = bnum_x * 8;
			uint64_t dst_zpitch = bnum_y * dst_ypitch;
			uint64_t w = tex->w;
			uint64_t h = tex->h;
			unsigned char* dst = (unsigned char*)staging_buf.mapped;
			unsigned char* src = (unsigned char*)data + poffset;

			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads > 8) nthreads = 8;
			std::vector<std::thread> threads(nthreads);
			int grain_size = tex->d / nthreads;
			
			auto worker = [&zpitch, &dst_zpitch, &ypitch, &dst_ypitch, &w, &h](unsigned char* dst, unsigned char* src, int d, int bnum_y, int bnum_x) {
				uint64_t buf[512];
				unsigned char block[16];
				uint64_t count = 0;
				unsigned char* src_p, * dst_p, * tmp_xsrc_p, * tmp_xdst_p;
				for (uint32_t z = 0; z < d; z++)
				{
					src_p = src + (VkDeviceSize)z * zpitch;
					for (uint32_t y = 0; y < bnum_y; y++)
					{
						tmp_xsrc_p = src_p + (VkDeviceSize)y * ypitch * 4;
						for (int x = 0; x < bnum_x; x++)
						{
							int bcount = 0;
							uint8_t maxval = 0;
							uint8_t minval = 255;
							for (uint32_t by = 0; by < 4; by++)
							{
								for (uint32_t bx = 0; bx < 4; bx++)
								{
									if (x * 4 + bx < w && y * 4 + by < h)
										block[bcount] = tmp_xsrc_p[by * ypitch + bx];
									else
										block[bcount] = block[bcount - 1];
									if (maxval < block[bcount])
										maxval = block[bcount];
									if (minval > block[bcount])
										minval = block[bcount];
									bcount++;
								}
							}

							uint64_t b = 0;
							if (maxval > minval)
							{
								for (int i = 15; i >= 0; i--)
								{
									uint8_t id = (uint8_t)((double)(block[i] - minval) / (double)(maxval - minval) * 7.0 + 0.5);
									if (id == 7)
										id = 0;
									else if (id == 0)
										id = 1;
									else
										id = 7 - id + 1;
									
									if (i < 15)
										b = b << 3;
									b |= id;
								}
							}
							else
								b = b << 48;

							b = b << 8;
							b |= minval;
							b = b << 8;
							b |= maxval;

							buf[count] = b;
							count++;
							if (count >= 512)
							{
								memcpy(dst, buf, (size_t)count * 8);
								dst += (size_t)count * 8;
								count = 0;
							}

							tmp_xsrc_p += 4;
						}
					}
				}
				if (count > 0)
					memcpy(dst, buf, (size_t)count * 8);
			};
			for (uint32_t i = 0; i < nthreads - 1; i++)
			{
				threads[i] = std::thread(worker, dst, src, grain_size, bnum_y, bnum_x);
				dst += grain_size * dst_zpitch;
				src += grain_size * zpitch;
			}
			threads.back() = std::thread(worker, dst, src, tex->d - grain_size * (nthreads - 1), bnum_y, bnum_x);
			for (auto&& i : threads) {
				i.join();
			}
		}

		//ed_time = milliseconds_now();
		//sprintf(dbgstr, "memcpy time: %lld  size: %lld\n", ed_time - st_time, texMemSize);
		//OutputDebugStringA(dbgstr);

		VkDeviceSize atom = properties.limits.nonCoherentAtomSize;
		if (atom > 0)
			texMemSize = (texMemSize + atom - 1) & ~(atom - 1);
		if (texMemSize > staging_buf.size)
			staging_buf.flush();
		else
			staging_buf.flush(texMemSize);

		//st_time = milliseconds_now();

		CopyDataStagingBuf2Tex(tex, flush, semaphore);

		//VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));
		//ed_time = milliseconds_now();
		//sprintf(dbgstr, "tex upload time: %lld  size: %lld\n", ed_time - st_time, texMemSize);
		//OutputDebugStringA(dbgstr);

		return true;
	}

	bool VulkanDevice::UploadSubTexture2D(const std::shared_ptr<VTexture>& tex, void* data, VkOffset2D offset, VkExtent2D extent, bool sync)
	{
		VkDeviceSize texMemSize = (VkDeviceSize)extent.width * (VkDeviceSize)extent.height * (VkDeviceSize)tex->bytes;

		if (sync)
			VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));

		checkStagingBuffer(texMemSize);

		// Copy texture data into staging buffer
		memcpy(staging_buf.mapped, data, texMemSize);

		VkDeviceSize atom = properties.limits.nonCoherentAtomSize;
		if (atom > 0)
			texMemSize = (texMemSize + atom - 1) & ~(atom - 1);
		if (texMemSize > staging_buf.size)
			staging_buf.flush();
		else
			staging_buf.flush(texMemSize);

		CopyDataStagingBuf2SubTex2D(tex, offset, extent);

		return true;
	}

	bool VulkanDevice::UploadTexture(const std::shared_ptr<VTexture> &tex, void *data, bool flush, vks::VulkanSemaphoreSettings* semaphore, bool sync)
	{
		static int bufsize = 4096;

		VkDeviceSize texMemSize = (VkDeviceSize)tex->w * (VkDeviceSize)tex->h * (VkDeviceSize)tex->d * (VkDeviceSize)tex->bytes;

		if (sync)
			VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));

		checkStagingBuffer(texMemSize);

		//uint64_t st_time, ed_time;
		//char dbgstr[50];
		//st_time = milliseconds_now();

		// Copy texture data into staging buffer
		if (tex->format != VK_FORMAT_BC4_UNORM_BLOCK)
		{
			//std::stringstream debugMessage;
			//debugMessage << "uploadTex: " << data;
			//OutputDebugStringA(debugMessage.str().c_str()); OutputDebugString(L"\n");
			
			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads > 8) nthreads = 8;
			nthreads = 1;
			std::vector<std::thread> threads(nthreads);
			size_t grain_size = texMemSize / nthreads;
			auto worker = [](unsigned char* dst, unsigned char* src, size_t size) {
				int ite = size / bufsize;
				for (int i = 0; i < ite - 1; i++)
				{
					memcpy(dst, src, bufsize);
					dst += bufsize;
					src += bufsize;
				}
				memcpy(dst, src, size - bufsize * (ite >= 1 ? ite - 1 : 0));
			};
			unsigned char* dstp = (unsigned char*)staging_buf.mapped;
			unsigned char* stp = (unsigned char*)data;
			for (uint32_t i = 0; i < nthreads - 1; i++)
			{
				threads[i] = std::thread(worker, dstp, stp, grain_size);
				dstp += grain_size;
				stp += grain_size;
			}
			threads.back() = std::thread(worker, dstp, stp, texMemSize - grain_size * (nthreads - 1));
			for (auto&& i : threads) {
				i.join();
			}
			
			/*
			unsigned char* dstp = (unsigned char*)staging_buf.mapped;
			unsigned char* stp = (unsigned char*)data;
			int ite = texMemSize / bufsize;
			for (int i = 0; i < ite; i++)
			{
				memcpy(dstp, stp, bufsize);
				dstp += bufsize;
				stp += bufsize;
			}
			if (texMemSize % bufsize > 0)
				memcpy(dstp, stp, texMemSize % bufsize);
			*/
			//OutputDebugStringA("uploadtex finished\n");
			
		}
		else
		{
			//BC4 Compression
			uint64_t ypitch = (uint64_t)tex->w * (uint64_t)tex->bytes;
			uint64_t zpitch = (uint64_t)tex->h * (uint64_t)tex->w * (uint64_t)tex->bytes;

			uint64_t bnum_x = ((VkDeviceSize)tex->w % 4 > 0 ? (VkDeviceSize)tex->w / 4 + 1 : (VkDeviceSize)tex->w / 4);
			uint64_t bnum_y = ((VkDeviceSize)tex->h % 4 > 0 ? (VkDeviceSize)tex->h / 4 + 1 : (VkDeviceSize)tex->h / 4);
			uint64_t dst_ypitch = bnum_x * 8;
			uint64_t dst_zpitch = bnum_y * dst_ypitch;
			uint64_t w = tex->w;
			uint64_t h = tex->h;
			unsigned char* dst = (unsigned char*)staging_buf.mapped;
			unsigned char* src = (unsigned char*)data;

			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads > 8) nthreads = 8;
			std::vector<std::thread> threads(nthreads);
			int grain_size = tex->d / nthreads;

			auto worker = [&zpitch, &dst_zpitch, &ypitch, &dst_ypitch, &w, &h](unsigned char* dst, unsigned char* src, int d, int bnum_y, int bnum_x) {
				uint64_t buf[512];
				unsigned char block[16];
				uint64_t count = 0;
				unsigned char* src_p, * dst_p, * tmp_xsrc_p, * tmp_xdst_p;
				for (uint32_t z = 0; z < d; z++)
				{
					src_p = src + (VkDeviceSize)z * zpitch;
					for (uint32_t y = 0; y < bnum_y; y++)
					{
						tmp_xsrc_p = src_p + (VkDeviceSize)y * ypitch * 4;
						for (int x = 0; x < bnum_x; x++)
						{
							int bcount = 0;
							uint8_t maxval = 0;
							uint8_t minval = 255;
							for (uint32_t by = 0; by < 4; by++)
							{
								for (uint32_t bx = 0; bx < 4; bx++)
								{
									if (x * 4 + bx < w && y * 4 + by < h)
										block[bcount] = tmp_xsrc_p[by * ypitch + bx];
									else
										block[bcount] = block[bcount - 1];
									if (maxval < block[bcount])
										maxval = block[bcount];
									if (minval > block[bcount])
										minval = block[bcount];
									bcount++;
								}
							}

							uint64_t b = 0;
							if (maxval > minval)
							{
								for (int i = 15; i >= 0; i--)
								{
									uint8_t id = (uint8_t)((double)(block[i] - minval) / (double)(maxval - minval) * 7.0 + 0.5);
									if (id == 7)
										id = 0;
									else if (id == 0)
										id = 1;
									else
										id = 7 - id + 1;

									if (i < 15)
										b = b << 3;
									b |= id;
								}
							}
							else
								b = b << 48;

							b = b << 8;
							b |= minval;
							b = b << 8;
							b |= maxval;

							buf[count] = b;
							count++;
							if (count >= 512)
							{
								memcpy(dst, buf, (size_t)count * 8);
								dst += (size_t)count * 8;
								count = 0;
							}

							tmp_xsrc_p += 4;
						}
					}
				}
				if (count > 0)
					memcpy(dst, buf, (size_t)count * 8);
			};
			for (uint32_t i = 0; i < nthreads - 1; i++)
			{
				threads[i] = std::thread(worker, dst, src, grain_size, bnum_y, bnum_x);
				dst += grain_size * dst_zpitch;
				src += grain_size * zpitch;
			}
			threads.back() = std::thread(worker, dst, src, tex->d - grain_size * (nthreads - 1), bnum_y, bnum_x);
			for (auto&& i : threads) {
				i.join();
			}
		}

		//ed_time = milliseconds_now();
		//sprintf(dbgstr, "memcpy time: %lld  size: %lld\n", ed_time - st_time, texMemSize);
		//OutputDebugStringA(dbgstr);

		VkDeviceSize atom = properties.limits.nonCoherentAtomSize;
		if (atom > 0)
			texMemSize = (texMemSize + atom - 1) & ~(atom - 1);
		if (texMemSize > staging_buf.size)
			staging_buf.flush();
		else
			staging_buf.flush(texMemSize);
		
		CopyDataStagingBuf2Tex(tex, flush, semaphore);

		return true;
	}

	void VulkanDevice::CopyDataStagingBuf2Tex(const std::shared_ptr<VTexture> &tex, bool flush, vks::VulkanSemaphoreSettings* semaphore)
	{
		VkCommandBuffer copyCmd = VK_NULL_HANDLE;
		if (flush)
			copyCmd = createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		else
		{
			copyCmd = GetNextTransferCommandBuffer();
			VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));
		}

		// The sub resource range describes the regions of the image we will be transitioned
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		// Optimal image will be used as destination for the copy, so we must transfer from our
		// initial undefined image layout to the transfer destination layout
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			tex->descriptor.imageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresourceRange);

		// Setup buffer copy regions
		VkBufferImageCopy bufferCopyRegion{};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = tex->w;
		bufferCopyRegion.imageExtent.height = tex->h;
		bufferCopyRegion.imageExtent.depth = tex->d;

		vkCmdCopyBufferToImage(
			copyCmd,
			staging_buf.buffer,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&bufferCopyRegion);

		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			tex->descriptor.imageLayout,
			subresourceRange);

		if (flush)
			flushTransferCommandBuffer(copyCmd, true);
		else
		{
			VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));
			VkSubmitInfo submitInfo = vks::initializers::submitInfo();
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &copyCmd;

			std::vector<VkPipelineStageFlags> waitStages;
			if (semaphore)
			{
				submitInfo.pSignalSemaphores = semaphore->signalSemaphores;
				submitInfo.signalSemaphoreCount = semaphore->signalSemaphoreCount;
				if (semaphore->waitSemaphoreCount > 0)
				{
					for (uint32_t i = 0; i < semaphore->waitSemaphoreCount; i++)
						waitStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
					submitInfo.waitSemaphoreCount = semaphore->waitSemaphoreCount;
					submitInfo.pWaitSemaphores = semaphore->waitSemaphores;
					submitInfo.pWaitDstStageMask = waitStages.data();
				}
			}
			// Submit to the queue
			VK_CHECK_RESULT(vkQueueSubmit(transfer_queue, 1, &submitInfo, VK_NULL_HANDLE));
		}
	}

	void VulkanDevice::CopyDataStagingBuf2SubTex2D(const std::shared_ptr<VTexture>& tex, VkOffset2D offset, VkExtent2D extent)
	{
		VkCommandBuffer copyCmd = createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// The sub resource range describes the regions of the image we will be transitioned
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		// Optimal image will be used as destination for the copy, so we must transfer from our
		// initial undefined image layout to the transfer destination layout
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			tex->descriptor.imageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			subresourceRange);

		// Setup buffer copy regions
		VkBufferImageCopy bufferCopyRegion{};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = extent.width;
		bufferCopyRegion.imageExtent.height = extent.height;
		bufferCopyRegion.imageExtent.depth = 1;
		bufferCopyRegion.imageOffset.x = offset.x;
		bufferCopyRegion.imageOffset.y = offset.y;
		bufferCopyRegion.imageOffset.z = 0;

		vkCmdCopyBufferToImage(
			copyCmd,
			staging_buf.buffer,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&bufferCopyRegion);

		// Change texture image layout to shader read after all mip levels have been copied
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			tex->descriptor.imageLayout,
			subresourceRange);

		flushTransferCommandBuffer(copyCmd, true);
	}

	bool VulkanDevice::DownloadTexture3D(const std::shared_ptr<VTexture> &tex, void *data, VkOffset3D offset, uint32_t ypitch, uint32_t zpitch)
	{
		const VkDeviceSize texMemSize = (VkDeviceSize)tex->w * (VkDeviceSize)tex->h * (VkDeviceSize)tex->d * (VkDeviceSize)tex->bytes;

		checkStagingBuffer(texMemSize);

		CopyDataTex2StagingBuf(tex);

		staging_buf.invalidate();

		// Copy texture data from staging buffer
		uint64_t poffset = (VkDeviceSize)offset.z*zpitch + (VkDeviceSize)offset.y*ypitch + offset.x*(VkDeviceSize)tex->bytes;
		uint64_t src_ypitch = (VkDeviceSize)tex->w * (VkDeviceSize)tex->bytes;
		unsigned char* srcp = (unsigned char*)staging_buf.mapped;
		unsigned char* tp = (unsigned char *)data + poffset;
		unsigned char* tp2; 
		for (uint32_t z = 0; z < tex->d; z++)
		{
			tp2 = tp;
			for (uint32_t y = 0; y < tex->h; y++)
			{
				memcpy(tp2, srcp, src_ypitch);
				srcp += src_ypitch;
				tp2 += ypitch;
			}
			tp += zpitch;
		}

		return true;
	}

	bool VulkanDevice::DownloadTexture(const std::shared_ptr<VTexture> &tex, void *data)
	{
		const VkDeviceSize texMemSize = (VkDeviceSize)tex->w * (VkDeviceSize)tex->h * (VkDeviceSize)tex->d * (VkDeviceSize)tex->bytes;

		checkStagingBuffer(texMemSize);

		CopyDataTex2StagingBuf(tex);

		memcpy(data, staging_buf.mapped, texMemSize);
		
		return true;
	}

	bool VulkanDevice::DownloadSubTexture2D(const std::shared_ptr<VTexture>& tex, void* data, VkOffset2D offset, VkExtent2D extent)
	{
		const VkDeviceSize texMemSize = (VkDeviceSize)extent.width * (VkDeviceSize)extent.height * (VkDeviceSize)tex->bytes;

		checkStagingBuffer(texMemSize);

		CopyDataSubTex2StagingBuf2D(tex, offset, extent);

		memcpy(data, staging_buf.mapped, texMemSize);

		return true;
	}

	void VulkanDevice::CopyDataTex2StagingBuf(const std::shared_ptr<VTexture> &tex)
	{
		VkCommandBuffer copyCmd = createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// The sub resource range describes the regions of the image we will be transitioned
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		// Optimal image will be used as destination for the copy, so we must transfer from our
		// initial undefined image layout to the transfer destination layout
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			tex->descriptor.imageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			subresourceRange);

		// Setup image copy regions
		VkBufferImageCopy imageCopyRegion{};
		imageCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.imageSubresource.mipLevel = 0;
		imageCopyRegion.imageSubresource.baseArrayLayer = 0;
		imageCopyRegion.imageSubresource.layerCount = 1;
		imageCopyRegion.imageExtent.width = tex->w;
		imageCopyRegion.imageExtent.height = tex->h;
		imageCopyRegion.imageExtent.depth = tex->d;

		vkCmdCopyImageToBuffer(
			copyCmd,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			staging_buf.buffer,
			1,
			&imageCopyRegion);

		// Change texture image layout to shader read after all mip levels have been copied
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			tex->descriptor.imageLayout,
			subresourceRange);

		flushTransferCommandBuffer(copyCmd, true);
	}

	void VulkanDevice::CopyDataSubTex2StagingBuf2D(const std::shared_ptr<VTexture>& tex, VkOffset2D offset, VkExtent2D extent)
	{
		VkCommandBuffer copyCmd = createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// The sub resource range describes the regions of the image we will be transitioned
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;

		// Optimal image will be used as destination for the copy, so we must transfer from our
		// initial undefined image layout to the transfer destination layout
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			tex->descriptor.imageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			subresourceRange);

		// Setup image copy regions
		VkBufferImageCopy imageCopyRegion{};
		imageCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageCopyRegion.imageSubresource.mipLevel = 0;
		imageCopyRegion.imageSubresource.baseArrayLayer = 0;
		imageCopyRegion.imageSubresource.layerCount = 1;
		imageCopyRegion.imageExtent.width = extent.width;;
		imageCopyRegion.imageExtent.height = extent.height;;
		imageCopyRegion.imageExtent.depth = 1;
		imageCopyRegion.imageOffset.x = offset.x;
		imageCopyRegion.imageOffset.y = offset.y;
		imageCopyRegion.imageOffset.z = 0;

		vkCmdCopyImageToBuffer(
			copyCmd,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			staging_buf.buffer,
			1,
			&imageCopyRegion);

		// Change texture image layout to shader read after all mip levels have been copied
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			tex->descriptor.imageLayout,
			subresourceRange);

		flushTransferCommandBuffer(copyCmd, true);
	}

	void VulkanDevice::ResetRenderSemaphores()
	{
		m_cur_semaphore_id = -1;
	}

	VulkanSemaphoreSettings VulkanDevice::GetNextRenderSemaphoreSettings()
	{
		vks::VulkanSemaphoreSettings ret;

		VkSemaphore* cur = GetCurrentRenderSemaphore();
		if (cur)
		{
			ret.waitSemaphores = cur;
			ret.waitSemaphoreCount = 1;
		}

		VkSemaphore* next = GetNextRenderSemaphore();
		if (next)
		{
			ret.signalSemaphores = next;
			ret.signalSemaphoreCount = 1;
		}

		return ret;
	}

	VkSemaphore* VulkanDevice::GetNextRenderSemaphore()
	{
		m_cur_semaphore_id++;
		if (m_cur_semaphore_id >= m_render_semaphore.size())
			m_render_semaphore.resize((size_t)m_cur_semaphore_id + 1);
		if (!m_render_semaphore[m_cur_semaphore_id])
			m_render_semaphore[m_cur_semaphore_id] = std::make_unique<vks::VSemaphore>(this);
		return &m_render_semaphore[m_cur_semaphore_id]->vksemaphore;
	}

	VkSemaphore* VulkanDevice::GetCurrentRenderSemaphore()
	{
		if (m_cur_semaphore_id < 0 || m_cur_semaphore_id >= m_render_semaphore.size())
			return nullptr;
		return &m_render_semaphore[m_cur_semaphore_id]->vksemaphore;
	}

	void VulkanDevice::UploadData2Buffer(void* data, vks::Buffer* dst, VkDeviceSize offset, VkDeviceSize size)
	{
		VkDeviceSize bufsize = size;
		VkDeviceSize atom = properties.limits.nonCoherentAtomSize;
		if (atom > 0)
			bufsize = (bufsize + atom - 1) & ~(atom - 1);

		checkStagingBuffer(bufsize);

		memcpy(staging_buf.mapped, data, size);

		if (bufsize > staging_buf.size)
			staging_buf.flush();
		else
			staging_buf.flush(bufsize);

		VkBufferCopy bc;
		bc.size = size;
		bc.srcOffset = 0;
		bc.dstOffset = offset;

		copyBuffer(&staging_buf, dst, queue, &bc);
	}
}
