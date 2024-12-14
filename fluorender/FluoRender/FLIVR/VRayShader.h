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
//    File   : VolShader.h
//    Author : Milan Ikits
//    Date   : Tue Jul 13 02:27:58 2004

#ifndef VRayShader_h
#define VRayShader_h

#include "VulkanDevice.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include "DLLExport.h"

namespace FLIVR
{
	class ShaderProgram;

	class EXPORT_API VRayShader
	{
	public:
		VRayShader(VkDevice device, bool poly, int channels,
				bool shading, bool fog,
				int peel, bool clip,
				bool hiqual, int mask,
				int color_mode, int colormap, int colormap_proj,
				bool solid, int vertex_shader, int mask_hide_mode, bool persp, int blend_mode, int multi_mode, bool na_mode, bool highlight);
		~VRayShader();

		bool create();

		inline VkDevice device() { return device_; }
		inline bool poly() {return poly_; }
		inline int channels() { return channels_; }
		inline bool shading() { return shading_; }
		inline bool fog() { return fog_; }
		inline int peel() { return peel_; }
		inline bool clip() { return clip_; }
		inline bool hiqual() {return hiqual_; }
		inline int mask() {return mask_;}
		inline int color_mode() {return color_mode_;}
		inline int colormap() {return colormap_;}
		inline int colormap_proj() {return colormap_proj_;}
		inline bool solid() {return solid_;}
		inline bool mask_hide_mode() {return mask_hide_mode_;}
		inline bool persp() { return persp_; }
		inline int blend_mode() { return blend_mode_; }
		inline int multi_mode() { return multi_mode_; }
        inline bool na_mode() { return na_mode_; }

		inline bool match(VkDevice device, bool poly, int channels,
						bool shading, bool fog,
						int peel, bool clip,
						bool hiqual, int mask,
						int color_mode, int colormap, int colormap_proj,
						bool solid, int vertex_shader, int mask_hide_mode, bool persp, int blend_mode, int multi_mode, bool na_mode, bool highlight)
		{ 
			return (device_ == device &&
				poly_ == poly &&
				channels_ == channels &&
				shading_ == shading && 
				fog_ == fog && 
				peel_ == peel &&
				clip_ == clip &&
				hiqual_ == hiqual &&
				mask_ == mask &&
				color_mode_ == color_mode &&
				colormap_ == colormap &&
				colormap_proj_ == colormap_proj &&
				solid_ == solid &&
				vertex_type_ == vertex_shader &&
				mask_hide_mode_ == mask_hide_mode &&
				persp_ == persp &&
				blend_mode_ == blend_mode &&
				multi_mode_ == multi_mode &&
                na_mode_ == na_mode &&
                highlight_ == highlight);
		}

		inline ShaderProgram* program() { return program_; }

	protected:
		bool emit_f(std::string& s);
		bool emit_v(std::string& s);
		std::string get_colormap_code();
		std::string get_colormap_proj();

		bool poly_;
		int channels_;
		bool shading_;
		bool fog_;
		int peel_;
		bool clip_;
		bool hiqual_;
		int mask_;	//0-normal, 1-render with mask, 2-render with mask excluded
					//3-random color with label, 4-random color with label+mask
		int color_mode_;//0-normal; 1-rainbow; 2-depth; 3-index; 255-index(depth mode)
		int colormap_;//index
		int colormap_proj_;//projection direction
		bool solid_;//no transparency
		int vertex_type_;
		int mask_hide_mode_;
		bool persp_;
		int blend_mode_;
		int multi_mode_;
        bool na_mode_;
        bool highlight_;

		VkDevice device_;

		ShaderProgram* program_;
	};

	class EXPORT_API VRayShaderFactory
	{
	public:
		VRayShaderFactory();
		VRayShaderFactory(std::vector<vks::VulkanDevice*> &devices);
		~VRayShaderFactory();

		void init(std::vector<vks::VulkanDevice*> &devices);

		ShaderProgram* shader(VkDevice device, bool poly, int channels,
								bool shading, bool fog,
								int peel, bool clip,
								bool hiqual, int mask,
								int color_mode, int colormap, int colormap_proj,
								bool solid, int vertex_type, int mask_hide_mode, bool persp, int blend_mode, int multi_mode, bool na_mode, bool highlight);
		//mask: 0-no mask, 1-segmentation mask, 2-labeling mask
		//color_mode: 0-normal; 1-rainbow; 2-depth; 3-index; 255-index(depth mode)

		struct VRayPipelineSettings {
			VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
			VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
			VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		};
		
		struct VRayVertShaderUBO {
			glm::mat4 proj_mat;
			glm::mat4 mv_mat;
		};

		struct VRayFragShaderBaseUBO {
			glm::vec4 loc0_light_alpha;	//loc0
			glm::vec4 loc1_material;	//loc1
			glm::vec4 loc2_scscale_th;	//loc2
			glm::vec4 loc3_gamma_offset;//loc3
//			glm::vec4 loc4_dim;			//loc4
			glm::vec4 loc5_spc_id;		//loc5
			glm::vec4 loc6_colparam;	//loc6
			glm::vec4 loc7_view;		//loc7
			glm::vec4 loc8_fog;			//loc8
			glm::vec4 loc9_fog_col;		//loc9
			glm::vec4 plane0;			//loc10
			glm::vec4 plane1;			//loc11
			glm::vec4 plane2;			//loc12
			glm::vec4 plane3;			//loc13
			glm::vec4 plane4;			//loc14
			glm::vec4 plane5;			//loc15
			glm::mat4 proj_mat_inv;
			glm::mat4 mv_mat_inv;
		};

		struct VRayFragShaderBrickConst {
			glm::vec4 b_scale_invnx;
			glm::vec4 b_trans_invny;
			glm::vec4 mask_b_scale_invnz;
			glm::vec4 mask_b_trans;
			glm::vec4 tbmin;
			glm::vec4 tbmax;
			glm::vec3 loc_zmin_zmax_dz;
			uint32_t stepnum;
		};

		struct VRayUniformBufs {
			vks::Buffer vert;
			vks::Buffer frag_base;
		};

		void setupDescriptorSetLayout();
		void getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, VRayUniformBufs& uniformBuffers, std::vector<VkWriteDescriptorSet>& writeDescriptorSets);
		void getDescriptorSetWriteUniforms(vks::VulkanDevice* vdev, vks::Buffer& vert, vks::Buffer& frag, std::vector<VkWriteDescriptorSet>& writeDescriptorSets);
		void prepareUniformBuffers(std::map<vks::VulkanDevice*, VRayUniformBufs> &uniformBuffers);
		static void updateUniformBuffers(VRayUniformBufs& uniformBuffers, VRayVertShaderUBO vubo, VRayFragShaderBaseUBO fubo);

		
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
			writeDescriptorSet.dstBinding = texid+2;
			writeDescriptorSet.pImageInfo = imageInfo;
			writeDescriptorSet.descriptorCount = descriptorCount;
			return writeDescriptorSet;
		}
		
		std::map<vks::VulkanDevice*, VRayPipelineSettings> pipeline_;

		std::vector<vks::VulkanDevice*> vdevices_;

	protected:
		std::vector<VRayShader*> shader_;
		int prev_shader_;
	};

} // end namespace FLIVR

#endif // VRayShader_h
