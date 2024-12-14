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

#ifndef MshShader_h
#define MshShader_h

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "DLLExport.h"

#include "VulkanDevice.hpp"


namespace FLIVR
{

	class ShaderProgram;

	class EXPORT_API MshShader
	{
	public:
		static const int MSH_SAMPLER_NUM = 3;

		MshShader(VkDevice device, int type,
			int peel, bool tex,
			bool fog, bool light, bool particle);
		~MshShader();

		bool create();

		inline VkDevice device() { return device_; }
		inline int type() { return type_; }
		inline int peel() { return peel_; }
		inline bool tex() { return tex_; }
		inline bool fog() { return fog_; }
		inline bool light() { return light_; }
        inline bool particle() { return particle_; }

		inline bool match(VkDevice device, int type,
			int peel, bool tex,
			bool fog, bool light, bool particle)
		{ 
			return (device_ == device &&
					type_ == type &&
					fog_ == fog && 
					peel_ == peel &&
					tex_ == tex &&
					light_ == light &&
                    particle_ == particle);
		}

		inline ShaderProgram* program() { return program_; }

	protected:
		bool emit_v(std::string& s);
		bool emit_f(std::string& s);

		VkDevice device_;
		int type_;	//0:normal; 1:integer 2:integer-segments
		int peel_;	//0:no peeling; 1:peel positive; 2:peel both; -1:peel negative
		bool tex_;
		bool fog_;
		bool light_;
        bool particle_;

		ShaderProgram* program_;
	};

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	class EXPORT_API MshShaderFactory
	{
	public:
		MshShaderFactory();
		MshShaderFactory(std::vector<vks::VulkanDevice*>& devices);
		~MshShaderFactory();

		ShaderProgram* shader(VkDevice device, int type, int peel, bool tex,
			bool fog, bool light, bool particle);

		void init(std::vector<vks::VulkanDevice*>& devices);

		struct MshPipelineSettings {
			VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		};

		struct MshVertShaderUBO {
			glm::mat4 proj_mat;
			glm::mat4 mv_mat;
			glm::mat4 normal_mat;
            float threshold;
		};

		struct MshFragShaderUBO {
			glm::vec4 loc0;//ambient color
			glm::vec4 loc1;//diffuse color
			glm::vec4 loc2;//specular color
			glm::vec4 loc3;//(shine, alpha, 0, 0)
			glm::vec4 loc7;//(1/vx, 1/vy, 0, 0)
			glm::vec4 loc8;//(int, start, end, 0.0)
			glm::vec4 loc9;//(fog_r, fog_g, fog_b, 0.0)
			glm::vec4 plane0; //plane0
			glm::vec4 plane1; //plane1
			glm::vec4 plane2; //plane2
			glm::vec4 plane3; //plane3
			glm::vec4 plane4; //plane4
			glm::vec4 plane5; //plane5
			glm::mat4 matrix3;
			uint32_t loci0;//name
		};

		struct MshUniformBufs {
			vks::Buffer vert;
			vks::Buffer frag;
		};

		void setupDescriptorSetLayout();
		void getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, MshUniformBufs& uniformBuffers, std::vector<VkWriteDescriptorSet>& writeDescriptorSets);
		void getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, vks::Buffer& vert, vks::Buffer& frag, std::vector<VkWriteDescriptorSet>& writeDescriptorSets);
		void prepareUniformBuffers(std::map<vks::VulkanDevice*, MshUniformBufs>& uniformBuffers);
		static void updateUniformBuffers(MshUniformBufs& uniformBuffers, MshVertShaderUBO vubo, MshFragShaderUBO fubo);

		static inline VkWriteDescriptorSet writeDescriptorSetTex(
			VkDescriptorSet dstSet,
			uint32_t texid,
			VkDescriptorImageInfo* imageInfo,
			uint32_t descriptorCount = 1)
		{
			VkWriteDescriptorSet writeDescriptorSet{};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.dstSet = dstSet;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet.dstBinding = texid + 2;
			writeDescriptorSet.pImageInfo = imageInfo;
			writeDescriptorSet.descriptorCount = descriptorCount;
			return writeDescriptorSet;
		}

		std::map<vks::VulkanDevice*, MshPipelineSettings> pipeline_;

		std::vector<vks::VulkanDevice*> vdevices_;

	protected:
		std::vector<MshShader*> shader_;
		int prev_shader_;
	};

} // end namespace FLIVR

#endif // MshShader_h
