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

// VolWarpShader: a Vulkan compute shader that resamples a (moving) source
// volume into a fixed-space output volume. For each output voxel the resampling
// map of a ThinPlateSpline (fixed -> moving: BigWarp's TPS, or an inverted
// linear model) is evaluated directly, and the source is sampled like
// BigWarp/imglib2: nearest neighbor or N-linear, zero outside the volume.

#ifndef VolWarpShader_h
#define VolWarpShader_h

#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "DLLExport.h"
#include "VulkanDevice.hpp"

namespace FLIVR
{
	class ShaderProgram;

	class EXPORT_API VolWarpShader
	{
	public:
		VolWarpShader(VkDevice device, int out_bytes);
		~VolWarpShader();

		bool create();

		inline VkDevice device() { return device_; }
		inline int out_bytes() { return out_bytes_; }

		inline bool match(VkDevice device, int out_bytes)
		{
			return (device_ == device && out_bytes_ == out_bytes);
		}

		inline ShaderProgram* program() { return program_; }

	protected:
		bool emit(std::string& s);

		VkDevice device_;
		int out_bytes_;

		ShaderProgram* program_;
	};

	class EXPORT_API VolWarpShaderFactory
	{
	public:
		VolWarpShaderFactory();
		VolWarpShaderFactory(std::vector<vks::VulkanDevice*>& devices);
		~VolWarpShaderFactory();

		ShaderProgram* shader(VkDevice device, int out_bytes);

		void init(std::vector<vks::VulkanDevice*>& devices);

		struct VolWarpPipeline {
			VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		};

		// Invariant transform parameters (std140 uniform buffer, binding 3).
		// The map is evaluated in the units of ThinPlateSpline's u space with
		// the result in source voxel coords (voxel centers at integers).
		struct WarpCompShaderUBO {
			glm::mat4 G;         // affine part: p = G * (u, 1)
			glm::vec4 gscale;    // u = o * gscale + goff (o = output voxel index)
			glm::vec4 goff;
			glm::ivec4 srcDim;   // (svx, svy, svz, 0) source volume dims (voxels)
			glm::ivec4 cfg;      // (num_landmarks, interp (0 nearest, 1 linear), 0, 0)
		};

		// Per-dispatch parameters (push constants, <= 128 bytes). One dispatch
		// covers an output sub-region: normally a whole brick, but bricks whose
		// source AABB exceeds the GPU 3D-texture limit are subdivided, each
		// sub-region getting its own source tile, and sub-regions are cut into
		// z slabs to bound the work of one dispatch.
		struct WarpCompShaderBrickConst {
			glm::ivec4 brickOrigin;  // (ox, oy, oz, 0) sub-region offset in volume
			glm::ivec4 validDims;    // (mx, my, mz, 0) sub-region valid data dims
			glm::ivec4 outOffset;    // (ox, oy, oz, 0) sub-region offset within the
			                         // output brick image (0 when not subdivided)
			glm::ivec4 tileOrigin;   // source tile origin (source voxels)
			glm::ivec4 tileSize;     // source tile size (source voxels)
		};

		static inline VkWriteDescriptorSet writeDescriptorSetStrageImage(
			VkDescriptorSet dstSet,
			uint32_t id,
			VkDescriptorImageInfo* imageInfo,
			uint32_t descriptorCount = 1)
		{
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = dstSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			writeDescriptorSet.dstBinding = id;
			writeDescriptorSet.pImageInfo = imageInfo;
			writeDescriptorSet.descriptorCount = descriptorCount;
			return writeDescriptorSet;
		}

		// Output image (binding 0).
		static inline VkWriteDescriptorSet writeDescriptorSetOutput(
			VkDescriptorSet dstSet,
			VkDescriptorImageInfo* imageInfo,
			uint32_t descriptorCount = 1)
		{
			return writeDescriptorSetStrageImage(dstSet, 0, imageInfo, descriptorCount);
		}

		// Source tile sampler (binding 1).
		static inline VkWriteDescriptorSet writeDescriptorSetSrc(
			VkDescriptorSet dstSet,
			VkDescriptorImageInfo* imageInfo,
			uint32_t descriptorCount = 1)
		{
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = dstSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.dstBinding = 1;
			writeDescriptorSet.pImageInfo = imageInfo;
			writeDescriptorSet.descriptorCount = descriptorCount;
			return writeDescriptorSet;
		}

		// Landmark storage buffer (binding 2).
		static inline VkWriteDescriptorSet writeDescriptorSetStrageBuf(
			VkDescriptorSet dstSet,
			VkDescriptorBufferInfo* bufInfo,
			uint32_t descriptorCount = 1)
		{
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = dstSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			writeDescriptorSet.dstBinding = 2;
			writeDescriptorSet.pBufferInfo = bufInfo;
			writeDescriptorSet.descriptorCount = descriptorCount;
			return writeDescriptorSet;
		}

		// Uniform buffer with transform params (binding 3).
		static inline VkWriteDescriptorSet writeDescriptorSetUBO(
			VkDescriptorSet dstSet,
			VkDescriptorBufferInfo* bufInfo,
			uint32_t descriptorCount = 1)
		{
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = dstSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.dstBinding = 3;
			writeDescriptorSet.pBufferInfo = bufInfo;
			writeDescriptorSet.descriptorCount = descriptorCount;
			return writeDescriptorSet;
		}

		void setupDescriptorSetLayout();

		std::map<vks::VulkanDevice*, VolWarpPipeline> pipeline_;

		std::vector<vks::VulkanDevice*> vdevices_;

	protected:
		std::vector<VolWarpShader*> shader_;
		int prev_shader_;
	};

	// must match the std140 "Warp" block and the push_constant block in VolWarpShader.cpp
	static_assert(sizeof(VolWarpShaderFactory::WarpCompShaderUBO) == 128 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderUBO, gscale) == 64 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderUBO, goff) == 80 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderUBO, srcDim) == 96 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderUBO, cfg) == 112,
		"WarpCompShaderUBO layout");
	static_assert(sizeof(VolWarpShaderFactory::WarpCompShaderBrickConst) == 80 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderBrickConst, outOffset) == 32 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderBrickConst, tileOrigin) == 48 &&
		offsetof(VolWarpShaderFactory::WarpCompShaderBrickConst, tileSize) == 64,
		"WarpCompShaderBrickConst layout");

} // end namespace FLIVR

#endif // VolWarpShader_h
