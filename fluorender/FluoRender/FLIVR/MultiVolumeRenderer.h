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

#ifndef SLIVR_MultiVolumeRenderer_h
#define SLIVR_MultiVolumeRenderer_h

//#include "GL/glew.h"
#include "BBox.h"
#include "Plane.h"
#include "Texture.h"
#include "VolumeRenderer.h"
#include <vector>

#include "DLLExport.h"

using namespace std;

namespace FLIVR
{
	class EXPORT_API MultiVolumeRenderer
	{
	public:
		MultiVolumeRenderer();
		MultiVolumeRenderer(MultiVolumeRenderer&);
		virtual ~MultiVolumeRenderer();

		//mode and sampling rate
		void set_mode(TextureRenderer::RenderMode mode);
		void set_sampling_rate(double rate);
		void set_interactive_rate(double irate);
		void set_interactive_mode(bool mode);
		void set_adaptive(bool b);
		int get_slice_num();

		//manages volume renderers for rendering
		void add_vr(VolumeRenderer* vr);
		void clear_vr();
		int get_vr_num();

		void draw(
			const std::unique_ptr<vks::VFrameBuffer>& framebuf,
			bool clear_framebuf,
			bool interactive_mode_p,
			bool orthographic_p, double zoom, bool intp, double sampling_frq_fac,
			VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f });

		void draw_wireframe(bool orthographic_p);
		void draw_volume(
			const std::unique_ptr<vks::VFrameBuffer>& framebuf,
			bool clear_framebuf,
			bool interactive_mode_p,
			bool orthographic_p, double zoom, bool intp, double sampling_frq_fac,
			VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f });

		double num_slices_to_rate(int slices);

		//depth peeling
		int get_depth_peel() {return depth_peel_;}
		void set_depth_peel(int dp) {depth_peel_ = dp;}

		//blend slices
		bool get_blend_slices() {return blend_slices_;}
		void set_blend_slices(bool bs) {blend_slices_ = bs;}

		//resize fbo texture
		void resize();

		//set noise reduction
		void SetNoiseRed(bool nd) {noise_red_ = nd;}
		bool GetNoiseRed() {return noise_red_;}

		//colormap mode
		void set_colormap_mode(int mode) {colormap_mode_ = mode;}

		//soft threshold
		static void set_soft_threshold(double val) {sw_ = val;}

		void set_main_membuf_size(long long size) { main_membuf_size_ = size; }

		void set_front_depth_tex(std::shared_ptr<vks::VTexture> dtex) { dtex_front_ = dtex; }
		void set_back_depth_tex(std::shared_ptr<vks::VTexture> dtex) { dtex_back_ = dtex; }

	private:
		//volume renderer list
		vector<VolumeRenderer*> vr_list_;

		//mode and quality control
		TextureRenderer::RenderMode mode_;
		int depth_peel_;
		bool hiqual_;
		int blend_num_bits_;
		bool blend_slices_;
		double buffer_scale_;

		//blend frame buffers
		std::unique_ptr<vks::VFrameBuffer> blend_framebuffer_;
		std::shared_ptr<vks::VTexture> blend_tex_id_;
		std::shared_ptr<vks::VTexture> label_tex_id_;
		std::unique_ptr<vks::VFrameBuffer> slice_fbo_;
		std::shared_ptr<vks::VTexture> slice_tex_;
		std::shared_ptr<vks::VTexture> blend_id_tex_;
		//2nd buffer for multiple filtering
		std::unique_ptr<vks::VFrameBuffer> filter_buffer_;
		std::shared_ptr<vks::VTexture> filter_tex_id_;

		std::shared_ptr<vks::VTexture> dtex_front_;
		std::shared_ptr<vks::VTexture> dtex_back_;

		//scale factor
		bool noise_red_;
		double sfactor_;
		double filter_size_min_;
		double filter_size_max_;
		double filter_size_shp_;

		//bounding box of all volumes
		BBox bbox_;
		//total resolution
		Vector res_;

		//sample rate etc
		bool imode_;
		bool adaptive_;
		double irate_;
		double sampling_rate_;
		int num_slices_;

		//light position
		Vector light_pos_;

		//colormap mode
		int colormap_mode_;//0-normal; 1-rainbow; 2-depth

		//soft threshold
		static double sw_;

		long long main_membuf_size_;

		//find out combined bricks in interactive mode
		vector<TextureBrick*> *get_combined_bricks(
			Point& center, Ray& view, bool is_orthographic = false);

		struct MultiVolRenederSettings {
			VolumeRenderer::VRayPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			vks::Buffer vert_ubuf, frag_ubuf;
			VkDeviceSize vert_ubuf_offset, frag_ubuf_offset;
			vector<VkWriteDescriptorSet> descriptorWritesBase;
			VRayShaderFactory::VRayVertShaderUBO vert_ubo;
			VRayShaderFactory::VRayFragShaderBaseUBO frag_ubo;
			Ray view_ray;
			VkFilter filter;
			//per-volume constants hoisted out of the per-brick loop
			Transform mv;
			Transform tform_tr;
		};

		struct MultiVolBrick {
			TextureBrick* b = nullptr;
			VRayShaderFactory::VRayFragShaderBrickConst frag_const = {};
			vector<VkWriteDescriptorSet> descriptorWrites;
		};

		inline void SubmitAndRestartCommandBuf(
			vks::VulkanDevice *device,
			VkCommandBuffer &cmdbuf,
			const VkRenderPassBeginInfo &renderPassBeginInfo,
			VkPipeline* last_bound_pipeline = nullptr);

		inline bool TestTexMemSwap(
			vks::VulkanDevice* device,
			TextureBrick *b,
			int c,
			vector<TextureBrick*> *locked_bricks);
	};

} // End namespace FLIVR

#endif 
