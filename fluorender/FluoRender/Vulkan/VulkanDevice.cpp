#include "VulkanDevice.hpp"
#include "vk_format_utils.h"
#include <thread>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>

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
		{
			if (tex_pool[j].tex)
			{
				available_mem += tex_pool[j].tex->memsize / MEM_MB;
				//keep the texture alive until the next frame boundary:
				//in-flight command buffers may still reference it
				frame().retired_texs.push_back(tex_pool[j].tex);
			}
		}
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
		//dirty bricks are read back before eviction; that transitions the image layout,
		//which must not race with in-flight reads of the same image
		bool need_sync = false;
		for (size_t j = 0; j < tex_pool.size(); j++)
		{
			if (tex_pool[j].delayed_del && tex_pool[j].tex && tex_pool[j].brick &&
				tex_pool[j].comp >= 0 && tex_pool[j].comp < TEXTURE_MAX_COMPONENTS &&
				tex_pool[j].brick->dirty(tex_pool[j].comp))
			{
				need_sync = true;
				break;
			}
		}
		if (need_sync)
		{
			VK_CHECK_RESULT(vkQueueWaitIdle(queue));
			if (transfer_queue != queue)
				VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));
		}

		for (int j=int(tex_pool.size()-1); j>=0; j--)
		{
			if (tex_pool[j].delayed_del && tex_pool[j].tex)
			{
				//save before deletion
				return_brick(tex_pool[j]);
				if (tex_pool[j].comp >= 0 && tex_pool[j].comp < TEXTURE_MAX_COMPONENTS && tex_pool[j].tex->bytes > 0)
					available_mem += tex_pool[j].tex->memsize / MEM_MB;
				//defer the actual destruction to the next frame boundary:
				//in-flight command buffers may still sample this texture
				frame().retired_texs.push_back(tex_pool[j].tex);
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
	//max-heap comparator: the heap top is the FARTHEST brick
	inline bool brick_closer(const BrickDist& bd1, const BrickDist& bd2)
	{
		return bd1.dist < bd2.dist;
	}

	int VulkanDevice::check_swap_memory(FLIVR::TextureBrick* brick, int c, bool *swapped)
	{
		unsigned int i;
		double new_mem = (VkDeviceSize)brick->nx()*brick->ny()*brick->nz()*brick->nb(c)/MEM_MB;

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

		if (swapped)
			*swapped = true;

		std::vector<BrickDist> bd_list;
		BrickDist bd;
		//generate a list of bricks and their distances to the new brick
		for (i=0; i<tex_pool.size(); i++)
		{
			if (!tex_pool[i].brick || !tex_pool[i].tex)
				continue;
			bd.index = i;
			bd.brick = tex_pool[i].brick;
			//calculate the distance
			bd.dist = brick->bbox().distance(bd.brick->bbox());
			bd_list.push_back(bd);
		}

		//release bricks far away
		double est_avlb_mem = available_mem;
		if (bd_list.size() > 0)
		{
			//partition without sorting; eviction order comes from a max-heap on the
			//distance, popped only as far as needed (the full sort was O(N log N)
			//per brick per frame on pools of thousands of entries)
			std::vector<BrickDist> bd_undisp;
			std::vector<BrickDist> bd_others;
			for (i=0; i<bd_list.size(); i++)
			{
				FLIVR::TextureBrick* b = bd_list[i].brick;
				if (b->is_tex_deletion_prevented())
					continue; //saved bricks are never touched
				else if (!b->get_disp())
					bd_undisp.push_back(bd_list[i]);
				else
					bd_others.push_back(bd_list[i]);
			}

			//pop bricks farthest-first, marking them for delayed deletion until
			//enough memory would be released
			auto evict_from = [&](std::vector<BrickDist>& bucket, std::vector<int>& deleted)
			{
				std::make_heap(bucket.begin(), bucket.end(), brick_closer);
				size_t heap_end = bucket.size();
				while (heap_end > 0 && est_avlb_mem < new_mem)
				{
					std::pop_heap(bucket.begin(), bucket.begin() + heap_end, brick_closer);
					heap_end--;
					TexParam &texp = tex_pool[bucket[heap_end].index];
					texp.delayed_del = true;
					deleted.push_back(bucket[heap_end].index);
					est_avlb_mem += texp.tex->memsize / MEM_MB;
				}
			};

			//overwrite or remove undisplayed bricks.
			//try to overwrite (any exact-property match is valid; order-insensitive)
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
				//the texture is reused in place (a new upload overwrites it), so any
				//in-flight reads of it must complete first
				VK_CHECK_RESULT(vkQueueWaitIdle(queue));
				if (transfer_queue != queue)
					VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));
				//save before deletion
				return_brick(tex_pool[overwrite]);
				return overwrite;
			}
			//remove
			std::vector<int> deleted;
			evict_from(bd_undisp, deleted);

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
					//the texture is reused in place: wait for in-flight reads first
					VK_CHECK_RESULT(vkQueueWaitIdle(queue));
					if (transfer_queue != queue)
						VK_CHECK_RESULT(vkQueueWaitIdle(transfer_queue));
					//save before deletion
					return_brick(tex_pool[overwrite]);
				}
				else
				{
					evict_from(bd_others, deleted);
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
			available_mem -= ret->memsize / MEM_MB;

			return int(tex_pool.size()) - 1;
		}
		else
			return -1;
	}

	//per-user cache file used to persist the Vulkan pipeline cache across runs
	//(all shader variants are compiled at runtime, so a cold cache causes first-use hitches)
	static std::string getPipelineCachePath(const VkPhysicalDeviceProperties& props)
	{
		std::string base;
#ifdef _WIN32
		const char* dir = std::getenv("LOCALAPPDATA");
		if (!dir) dir = std::getenv("APPDATA");
		if (!dir) return "";
		base = std::string(dir) + "\\VVDViewer";
#else
		const char* xdg = std::getenv("XDG_CACHE_HOME");
		if (xdg && *xdg)
			base = std::string(xdg) + "/VVDViewer";
		else
		{
			const char* home = std::getenv("HOME");
			if (!home) return "";
			base = std::string(home) + "/.cache/VVDViewer";
		}
#endif
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		if (ec) return "";
#ifdef _WIN32
		base += "\\";
#else
		base += "/";
#endif
		return base + "pipeline_cache_" + std::to_string(props.vendorID) + "_" + std::to_string(props.deviceID) + ".bin";
	}

	void VulkanDevice::createPipelineCache()
	{
		std::vector<char> initial;
		pipeline_cache_path = getPipelineCachePath(properties);
		if (!pipeline_cache_path.empty())
		{
			std::ifstream ifs(pipeline_cache_path, std::ios::binary | std::ios::ate);
			if (ifs)
			{
				std::streamsize sz = ifs.tellg();
				if (sz >= 32) //VkPipelineCacheHeaderVersionOne is 32 bytes
				{
					initial.resize((size_t)sz);
					ifs.seekg(0);
					if (ifs.read(initial.data(), sz))
					{
						uint32_t headerLen = 0, headerVer = 0, vendorID = 0, deviceID = 0;
						std::memcpy(&headerLen, initial.data(), 4);
						std::memcpy(&headerVer, initial.data() + 4, 4);
						std::memcpy(&vendorID, initial.data() + 8, 4);
						std::memcpy(&deviceID, initial.data() + 12, 4);
						if (headerLen < 32 ||
							headerVer != VK_PIPELINE_CACHE_HEADER_VERSION_ONE ||
							vendorID != properties.vendorID ||
							deviceID != properties.deviceID ||
							std::memcmp(initial.data() + 16, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
							initial.clear();
					}
					else
						initial.clear();
				}
			}
		}

		VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
		pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		pipelineCacheCreateInfo.initialDataSize = initial.size();
		pipelineCacheCreateInfo.pInitialData = initial.empty() ? nullptr : initial.data();
		if (vkCreatePipelineCache(logicalDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache) != VK_SUCCESS)
		{
			//stale or corrupt cache data: retry with an empty cache
			pipelineCacheCreateInfo.initialDataSize = 0;
			pipelineCacheCreateInfo.pInitialData = nullptr;
			VK_CHECK_RESULT(vkCreatePipelineCache(logicalDevice, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
		}
	}

	void VulkanDevice::savePipelineCache()
	{
		if (!pipelineCache || !logicalDevice || pipeline_cache_path.empty())
			return;
		size_t size = 0;
		if (vkGetPipelineCacheData(logicalDevice, pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0)
			return;
		std::vector<char> data(size);
		if (vkGetPipelineCacheData(logicalDevice, pipelineCache, &size, data.data()) != VK_SUCCESS)
			return;
		std::ofstream ofs(pipeline_cache_path, std::ios::binary | std::ios::trunc);
		if (ofs)
			ofs.write(data.data(), size);
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

	void VulkanDevice::prepareFrameSlot(FrameSlot& slot)
	{
		//fence and link semaphore live outside the early-return: the link semaphore
		//is dropped by ResetAllFrameSlots (window resize) and must be recreated even
		//when the slot's buffers already exist
		if (slot.fence == VK_NULL_HANDLE)
		{
			VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &slot.fence));
		}
		if (!slot.frame_link_sem)
		{
			slot.frame_link_sem = std::make_unique<vks::VSemaphore>(this);
			slot.frame_link_signaled = false;
		}

		if (!slot.cmdbufs.empty())
			return;

		slot.cmdbufs.push_back(createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY));
		slot.trans_cmdbufs.push_back(createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY));

		VkDeviceSize ubo_size = 1024;
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&slot.ubo,
				ubo_size
			)
		);
		slot.ubo.map();

		VkDeviceSize buf_size = 1024;
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&slot.vbuf,
				buf_size
			)
		);
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				&slot.ibuf,
				buf_size
			)
		);
		//host-visible rings for CPU-generated overlay geometry
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&slot.hvbuf,
				4096
			)
		);
		slot.hvbuf.map();
		VK_CHECK_RESULT(
			createBuffer(
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&slot.hibuf,
				4096
			)
		);
		slot.hibuf.map();
	}

	void VulkanDevice::PrepareMainRenderBuffers()
	{
		for (uint32_t s = 0; s < m_frame_slots; s++)
			prepareFrameSlot(m_frames[s]);
	}

	//grow-consolidate a transient ring at the frame boundary: if the last frame in
	//this slot spilled into extra blocks, merge them into one larger block
	void VulkanDevice::consolidateRing(vks::Buffer& cur, std::vector<vks::Buffer>& spill,
		VkBufferUsageFlags usage, VkMemoryPropertyFlags mem, bool map_buf)
	{
		if (spill.empty())
			return;
		VkDeviceSize newsize = cur.size;
		for (auto& b : spill)
		{
			newsize += b.size;
			b.destroy();
		}
		spill.clear();
		cur.destroy();
		VK_CHECK_RESULT(createBuffer(usage, mem, &cur, newsize));
		if (map_buf)
			cur.map();
	}

	void VulkanDevice::ResetMainRenderBuffers()
	{
		//advance to the next frame slot; with more than one slot the CPU can start
		//recording the new frame while the GPU still executes the previous one
		m_cur_frame = (m_cur_frame + 1) % m_frame_slots;
		FrameSlot& slot = frame();

		//the slot is only reused once its frame has fully executed
		if (slot.fence_pending && slot.fence != VK_NULL_HANDLE)
		{
			VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &slot.fence, VK_TRUE, UINT64_MAX));
			VK_CHECK_RESULT(vkResetFences(logicalDevice, 1, &slot.fence));
			slot.fence_pending = false;
		}

		//now everything this slot's frame referenced is safe to destroy
		for (auto& fn : slot.deferred_destroys)
			fn();
		slot.deferred_destroys.clear();
		slot.retired_texs.clear();

		prepareFrameSlot(slot);

		slot.cur_cmdbuf_id = 0;
		slot.cur_trans_cmdbuf_id = 0;
		slot.ubo_offset = 0;
		slot.vbuf_offset = 0;
		slot.ibuf_offset = 0;
		slot.hvbuf_offset = 0;
		slot.hibuf_offset = 0;
		slot.cur_semaphore_id = -1;

		consolidateRing(slot.ubo, slot.ubos,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true);
		consolidateRing(slot.vbuf, slot.vbufs,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
		consolidateRing(slot.ibuf, slot.ibufs,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
		consolidateRing(slot.hvbuf, slot.hvbufs,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true);
		consolidateRing(slot.hibuf, slot.hibufs,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true);
	}

	//allocate the next req_size bytes from a transient ring, spilling to a fresh
	//block when the current one is full
	void VulkanDevice::ringNext(vks::Buffer& cur, std::vector<vks::Buffer>& spill, VkDeviceSize& cursor,
		VkDeviceSize req_size, VkBufferUsageFlags usage, VkMemoryPropertyFlags mem, bool map_buf,
		vks::Buffer& buf, VkDeviceSize& offset)
	{
		offset = cursor;

		if (cur.alignment > 0)
			req_size = (req_size + cur.alignment - 1) & ~(cur.alignment - 1);

		if (cur.size >= cursor + req_size)
			cursor += req_size;
		else
		{
			offset = 0;
			spill.push_back(cur);
			VK_CHECK_RESULT(createBuffer(usage, mem, &cur, req_size));
			cursor = req_size;
			if (map_buf)
				cur.map();
		}

		buf = cur;
		buf.descriptor.buffer = buf.buffer;
		buf.descriptor.offset = offset;
		buf.descriptor.range = req_size;
	}

	void VulkanDevice::GetNextUniformBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		FrameSlot& slot = frame();
		ringNext(slot.ubo, slot.ubos, slot.ubo_offset, req_size,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true,
			buf, offset);
	}
	VkDeviceSize VulkanDevice::GetCurrentUniformBufferOffset()
	{
		return frame().ubo_offset;
	}
	void VulkanDevice::GetNextVertexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		FrameSlot& slot = frame();
		ringNext(slot.vbuf, slot.vbufs, slot.vbuf_offset, req_size,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false,
			buf, offset);
	}
	void VulkanDevice::GetNextIndexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		FrameSlot& slot = frame();
		ringNext(slot.ibuf, slot.ibufs, slot.ibuf_offset, req_size,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false,
			buf, offset);
	}
	void VulkanDevice::GetNextHostVertexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		FrameSlot& slot = frame();
		ringNext(slot.hvbuf, slot.hvbufs, slot.hvbuf_offset, req_size,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true,
			buf, offset);
	}
	void VulkanDevice::GetNextHostIndexBuffer(VkDeviceSize req_size, vks::Buffer& buf, VkDeviceSize& offset)
	{
		FrameSlot& slot = frame();
		ringNext(slot.hibuf, slot.hibufs, slot.hibuf_offset, req_size,
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, true,
			buf, offset);
	}
	VkCommandBuffer VulkanDevice::GetNextCommandBuffer()
	{
		FrameSlot& slot = frame();
		if (slot.cur_cmdbuf_id >= slot.cmdbufs.size())
			slot.cmdbufs.push_back(createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY));

		return slot.cmdbufs[slot.cur_cmdbuf_id++];
	}

	VkCommandBuffer VulkanDevice::GetNextTransferCommandBuffer()
	{
		FrameSlot& slot = frame();
		if (slot.cur_trans_cmdbuf_id >= slot.trans_cmdbufs.size())
			slot.trans_cmdbufs.push_back(createTransferCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY));

		return slot.trans_cmdbufs[slot.cur_trans_cmdbuf_id++];
	}

	//queue a destruction to run when the current frame slot is next reused (i.e.
	//after its frame fence has signaled)
	void VulkanDevice::retire(std::function<void()> fn)
	{
		if (m_in_shutdown)
		{
			fn();
			return;
		}
		frame().deferred_destroys.push_back(std::move(fn));
	}

	void VulkanDevice::retireBuffer(vks::Buffer& buf)
	{
		if (buf.buffer == VK_NULL_HANDLE && buf.memory == VK_NULL_HANDLE)
			return;
		if (m_in_shutdown)
		{
			buf.destroy();
			return;
		}
		if (buf.mapped)
			buf.unmap();
		VkDevice dev = buf.device;
		VkBuffer b = buf.buffer;
		VkDeviceMemory m = buf.memory;
		buf.buffer = VK_NULL_HANDLE;
		buf.memory = VK_NULL_HANDLE;
		retire([dev, b, m]() {
			if (b != VK_NULL_HANDLE)
				vkDestroyBuffer(dev, b, nullptr);
			if (m != VK_NULL_HANDLE)
				vkFreeMemory(dev, m, nullptr);
		});
	}

	//reset every slot after the caller has drained the device (vkDeviceWaitIdle)
	void VulkanDevice::ResetAllFrameSlots()
	{
		for (uint32_t s = 0; s < FRAME_SLOTS_MAX; s++)
		{
			FrameSlot& slot = m_frames[s];
			if (slot.fence != VK_NULL_HANDLE && slot.fence_pending)
			{
				vkResetFences(logicalDevice, 1, &slot.fence);
				slot.fence_pending = false;
			}
			for (auto& fn : slot.deferred_destroys)
				fn();
			slot.deferred_destroys.clear();
			slot.retired_texs.clear();
			//recreate the chains: a binary semaphore may be left signaled with no
			//pending waiter after an OUT_OF_DATE acquire/present sequence
			slot.render_semaphore.clear();
			slot.frame_link_sem.reset();
			slot.frame_link_signaled = false;
			slot.cur_semaphore_id = -1;
			slot.cur_cmdbuf_id = 0;
			slot.cur_trans_cmdbuf_id = 0;
			slot.ubo_offset = 0;
			slot.vbuf_offset = 0;
			slot.ibuf_offset = 0;
			slot.hvbuf_offset = 0;
			slot.hibuf_offset = 0;
		}
	}

	//wait until every slot's in-flight frame has completed and run its deferred work
	void VulkanDevice::WaitIdleAllFrameSlots()
	{
		for (uint32_t s = 0; s < FRAME_SLOTS_MAX; s++)
		{
			FrameSlot& slot = m_frames[s];
			if (slot.fence_pending && slot.fence != VK_NULL_HANDLE)
			{
				vkWaitForFences(logicalDevice, 1, &slot.fence, VK_TRUE, UINT64_MAX);
				vkResetFences(logicalDevice, 1, &slot.fence);
				slot.fence_pending = false;
			}
		}
		//with no pending fences (or single-slot mode) the queues may still be busy
		if (m_frame_slots <= 1)
			return;
		//deferred work of non-current slots can now run safely
		for (uint32_t s = 0; s < FRAME_SLOTS_MAX; s++)
		{
			if (s == m_cur_frame)
				continue;
			FrameSlot& slot = m_frames[s];
			for (auto& fn : slot.deferred_destroys)
				fn();
			slot.deferred_destroys.clear();
			slot.retired_texs.clear();
		}
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
		ret->memsize = memReqs.size;

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
			//return an empty shared_ptr (not a half-built texture) so callers'
			//"if (!t)" guards actually detect the failure instead of later
			//dereferencing a VK_NULL_HANDLE image in vkCmdCopyBufferToImage.
			return nullptr;
		}
		// Check if GPU supports requested 3D texture dimensions (all three axes)
		uint32_t maxImageDimension3D(ret->device->properties.limits.maxImageDimension3D);
		if (w > maxImageDimension3D || h > maxImageDimension3D || d > maxImageDimension3D)
		{
			std::cout << "Error: Requested texture dimensions is greater than supported 3D texture dimension!" << std::endl;
			return nullptr;
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

	//get the next upload staging slot, waiting only for that slot's own transfer
	//(if still in flight) instead of draining the whole transfer queue
	VulkanDevice::StagingRingSlot* VulkanDevice::acquireUploadStagingSlot(VkDeviceSize size)
	{
		StagingRingSlot& slot = m_staging_ring[m_staging_ring_idx];
		m_staging_ring_idx = (m_staging_ring_idx + 1) % STAGING_RING_SIZE;

		if (slot.fence == VK_NULL_HANDLE)
		{
			VkFenceCreateInfo fenceInfo = vks::initializers::fenceCreateInfo(VK_FLAGS_NONE);
			VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &slot.fence));
		}
		if (slot.pending)
		{
			VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &slot.fence, VK_TRUE, UINT64_MAX));
			slot.pending = false;
		}
		VK_CHECK_RESULT(vkResetFences(logicalDevice, 1, &slot.fence));

		if (size > slot.buf.size)
		{
			slot.buf.unmap();
			slot.buf.destroy();
		}
		if (slot.buf.buffer == VK_NULL_HANDLE)
		{
			createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
				&slot.buf, size);
			VK_CHECK_RESULT(slot.buf.map());
		}
		return &slot;
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
		(void)sync; //slot fences now guard staging reuse; no queue-wide drain needed

		VkDeviceSize texMemSize = tex->memsize;

		StagingRingSlot* slot = acquireUploadStagingSlot(texMemSize);

		//uint64_t st_time, ed_time;
		//char dbgstr[50];
		//st_time = milliseconds_now();

		// Copy texture data into staging buffer
		if (tex->format != VK_FORMAT_BC4_UNORM_BLOCK)
		{
			uint64_t poffset = (VkDeviceSize)offset.z * zpitch + (VkDeviceSize)offset.y * ypitch + offset.x * (VkDeviceSize)tex->bytes;
			uint64_t dst_ypitch = (VkDeviceSize)tex->w * (VkDeviceSize)tex->bytes;
			uint64_t dst_zpitch = (VkDeviceSize)tex->w * (VkDeviceSize)tex->h * (VkDeviceSize)tex->bytes;
			unsigned char* dst = (unsigned char*)slot->buf.mapped;
			unsigned char* src = (unsigned char*)data + poffset;

			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads == 0) nthreads = 1;
			if (nthreads > 8) nthreads = 8;
			if (tex->d > 0 && nthreads > tex->d) nthreads = tex->d;
			std::vector<std::thread> threads(nthreads);
			int grain_size = tex->d / nthreads;
			auto worker = [&zpitch, &dst_zpitch, &ypitch, &dst_ypitch](unsigned char* dst, unsigned char* src, int d, int texh) {
				if (ypitch == dst_ypitch && zpitch == dst_zpitch)
				{
					//source and destination layouts match: one contiguous copy
					memcpy(dst, src, (uint64_t)d * dst_zpitch);
					return;
				}
				unsigned char* src_p, * dst_p;
				for (int z = 0; z < d; z++)
				{
					src_p = src + (VkDeviceSize)z * zpitch;
					dst_p = dst + (VkDeviceSize)z * dst_zpitch;
					for (uint32_t y = 0; y < (uint32_t)texh; y++)
						memcpy(dst_p + (VkDeviceSize)y * dst_ypitch, src_p + (VkDeviceSize)y * ypitch, dst_ypitch);
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
			unsigned char* dst = (unsigned char*)slot->buf.mapped;
			unsigned char* src = (unsigned char*)data + poffset;

			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads == 0) nthreads = 1;
			if (nthreads > 8) nthreads = 8;
			if (tex->d > 0 && nthreads > tex->d) nthreads = tex->d;
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
		if (texMemSize > slot->buf.size)
			slot->buf.flush();
		else
			slot->buf.flush(texMemSize);

		//st_time = milliseconds_now();

		CopyDataStagingBuf2Tex(tex, flush, semaphore, slot);

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
		(void)sync; //slot fences now guard staging reuse; no queue-wide drain needed

		VkDeviceSize texMemSize = (VkDeviceSize)tex->w * (VkDeviceSize)tex->h * (VkDeviceSize)tex->d * (VkDeviceSize)tex->bytes;

		StagingRingSlot* slot = acquireUploadStagingSlot(texMemSize);

		//uint64_t st_time, ed_time;
		//char dbgstr[50];
		//st_time = milliseconds_now();

		// Copy texture data into staging buffer
		if (tex->format != VK_FORMAT_BC4_UNORM_BLOCK)
		{
			//source and destination are both contiguous
			memcpy(slot->buf.mapped, data, texMemSize);
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
			unsigned char* dst = (unsigned char*)slot->buf.mapped;
			unsigned char* src = (unsigned char*)data;

			size_t nthreads = std::thread::hardware_concurrency();
			if (nthreads == 0) nthreads = 1;
			if (nthreads > 8) nthreads = 8;
			if (tex->d > 0 && nthreads > tex->d) nthreads = tex->d;
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
		if (texMemSize > slot->buf.size)
			slot->buf.flush();
		else
			slot->buf.flush(texMemSize);

		CopyDataStagingBuf2Tex(tex, flush, semaphore, slot);

		return true;
	}

	void VulkanDevice::CopyDataStagingBuf2Tex(const std::shared_ptr<VTexture> &tex, bool flush, vks::VulkanSemaphoreSettings* semaphore, StagingRingSlot* slot)
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

		// Optimal image will be used as destination for the copy.
		// A freshly created image is still in VK_IMAGE_LAYOUT_UNDEFINED (the descriptor
		// holds the intended sampling layout, not the actual one): passing the true
		// current layout keeps the barrier spec-valid, and the contents may be discarded
		// because the copy overwrites the whole image.
		vks::tools::setImageLayout(
			copyCmd,
			tex->image,
			tex->uploaded ? tex->descriptor.imageLayout : VK_IMAGE_LAYOUT_UNDEFINED,
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
			slot ? slot->buf.buffer : staging_buf.buffer,
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

		tex->uploaded = true;

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
			// Submit to the queue; the slot fence marks when its staging buffer can be reused
			VK_CHECK_RESULT(vkQueueSubmit(transfer_queue, 1, &submitInfo, slot ? slot->fence : VK_NULL_HANDLE));
			if (slot)
			{
				if (semaphore)
				{
					//the render submit waits on the semaphore; only the staging slot
					//needs the fence, which is waited when the slot is reused
					slot->pending = true;
				}
				else
				{
					//no downstream synchronization requested: complete the upload here
					VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &slot->fence, VK_TRUE, UINT64_MAX));
					slot->pending = false;
				}
			}
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
		frame().cur_semaphore_id = -1;
	}

	void VulkanDevice::SubmitFrameLink()
	{
		//called right after a successful acquire: chain[0] must be the only
		//semaphore allocated so far this frame
		assert(frame().cur_semaphore_id == 0);

		uint32_t prev = (m_cur_frame + m_frame_slots - 1) % m_frame_slots;
		FrameSlot& prev_slot = m_frames[prev];

		VkSemaphore waits[2];
		VkPipelineStageFlags waitStages[2] = {
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
		uint32_t nwaits = 0;
		//chain[0], signaled by the swapchain acquire
		VkSemaphore* acquired = GetCurrentRenderSemaphore();
		if (acquired)
			waits[nwaits++] = *acquired;
		//previous frame's frame-end signal (absent on the very first frame and
		//right after a resize recreated the semaphores)
		if (prev_slot.frame_link_sem && prev_slot.frame_link_signaled)
		{
			waits[nwaits++] = prev_slot.frame_link_sem->vksemaphore;
			prev_slot.frame_link_signaled = false;
		}

		//chain[1]: everything this frame submits is ordered after this link
		VkSemaphore* next = GetNextRenderSemaphore();

		VkSubmitInfo si = vks::initializers::submitInfo();
		si.waitSemaphoreCount = nwaits;
		si.pWaitSemaphores = nwaits ? waits : nullptr;
		si.pWaitDstStageMask = nwaits ? waitStages : nullptr;
		si.signalSemaphoreCount = 1;
		si.pSignalSemaphores = next;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
	}

	void VulkanDevice::SubmitFrameEnd(VkSemaphore present_sem)
	{
		FrameSlot& slot = frame();
		//id 0 would mean an acquire happened but SubmitFrameLink never ran, and
		//waiting chain[0] here would consume the acquire signal the link expects
		assert(slot.cur_semaphore_id != 0);
		//tail of this frame's semaphore chain (at least the link submit signaled one)
		VkSemaphore* tail = GetCurrentRenderSemaphore();
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		VkSemaphore signals[2];
		uint32_t nsignals = 0;
		if (slot.frame_link_sem)
		{
			signals[nsignals++] = slot.frame_link_sem->vksemaphore;
			slot.frame_link_signaled = true;
		}
		if (present_sem != VK_NULL_HANDLE)
			signals[nsignals++] = present_sem;

		VkSubmitInfo si = vks::initializers::submitInfo();
		if (tail)
		{
			si.waitSemaphoreCount = 1;
			si.pWaitSemaphores = tail;
			si.pWaitDstStageMask = &waitStage;
		}
		si.signalSemaphoreCount = nsignals;
		si.pSignalSemaphores = nsignals ? signals : nullptr;
		//the fence signals only after all earlier submission-order work on this
		//queue completes, so it covers the whole frame, not just this empty submit
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &si, slot.fence));
		slot.fence_pending = true;
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
		FrameSlot& slot = frame();
		slot.cur_semaphore_id++;
		if (slot.cur_semaphore_id >= slot.render_semaphore.size())
			slot.render_semaphore.resize((size_t)slot.cur_semaphore_id + 1);
		if (!slot.render_semaphore[slot.cur_semaphore_id])
			slot.render_semaphore[slot.cur_semaphore_id] = std::make_unique<vks::VSemaphore>(this);
		return &slot.render_semaphore[slot.cur_semaphore_id]->vksemaphore;
	}

	VkSemaphore* VulkanDevice::GetCurrentRenderSemaphore()
	{
		FrameSlot& slot = frame();
		if (slot.cur_semaphore_id < 0 || slot.cur_semaphore_id >= slot.render_semaphore.size())
			return nullptr;
		return &slot.render_semaphore[slot.cur_semaphore_id]->vksemaphore;
	}

	void VulkanDevice::RollbackRenderSemaphore()
	{
		FrameSlot& slot = frame();
		if (slot.cur_semaphore_id >= 0)
			slot.cur_semaphore_id--;
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
