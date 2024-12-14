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

#ifndef SLIVR_VolumeRenderer_h
#define SLIVR_VolumeRenderer_h

#include "DLLExport.h"

#include "VVulkan.h"

#include "Color.h"
#include "Plane.h"
#include "Texture.h"
#include "TextureRenderer.h"
#include "FLIVR/Quaternion.h"

namespace FLIVR
{
	
#define VOL_MASK_HIDE_NONE		0
#define VOL_MASK_HIDE_OUTSIDE	1
#define VOL_MASK_HIDE_INSIDE	2

	class MultiVolumeRenderer;

	class EXPORT_API VolumeRenderer : public TextureRenderer
	{
	public:
		VolumeRenderer(Texture* tex,
			const vector<Plane*> &planes,
			bool hiqual);
		VolumeRenderer(const VolumeRenderer&);
		virtual ~VolumeRenderer();

		//quality and data type
		//set by initialization
		bool get_hiqual() {return hiqual_;}

		//render mode
		void set_mode(RenderMode mode);
		//range and scale
		void set_scalar_scale(double scale);
		double get_scalar_scale();
		void set_gm_scale(double scale);
		//transfer function properties
		void set_gamma3d(double gamma);
		double get_gamma3d();
		void set_gm_thresh(double thresh);
		double get_gm_thresh();
		void set_offset(double offset);
		double get_offset();
		void set_lo_thresh(double thresh);
		double get_lo_thresh();
		void set_hi_thresh(double thresh);
		double get_hi_thresh();
		void set_color(const Color color);
		Color get_color();
		void set_mask_color(const Color color, bool set=true);
		Color get_mask_color();
		bool get_mask_color_set() {return mask_color_set_; }
		void reset_mask_color_set() {mask_color_set_ = false;}
		void set_mask_alpha(double alpha);
		double get_mask_alpha();
		void set_mask_thresh(double thresh);
		double get_mask_thresh();
		void set_alpha(double alpha);
		double get_alpha();

		//shading
		void set_shading(bool shading) { shading_ = shading; }
		bool get_shading() { return shading_; }
		void set_material(double amb, double diff, double spec, double shine)
		{ ambient_ = amb; diffuse_ = amb+0.7;
		diffuse_ = diffuse_>1.0?1.0:diffuse_;
		specular_ = spec; shine_ = shine; }

		//colormap mode
		void set_colormap_mode(int mode)
		{colormap_mode_ = mode;}
		void set_colormap_values(double low, double hi)
		{colormap_low_value_ = low; colormap_hi_value_ = hi;}
		void set_colormap(int value)
		{colormap_ = value;}
		void set_colormap_proj(int value)
		{colormap_proj_ = value;}

		//solid
		void set_solid(bool mode)
		{solid_ = mode;}
		bool get_solid()
		{return solid_;}

		//sampling rate
		void set_sampling_rate(double rate);
		double get_sampling_rate();
		double num_slices_to_rate(int slices);
		//slice number
		int get_slice_num();

		//interactive modes
		void set_interactive_rate(double irate);
		void set_interactive_mode(bool mode);
		void set_adaptive(bool b);

		//clipping planes
		void set_planes(vector<Plane*> *p);
		vector<Plane*> *get_planes();

		//interpolation
		bool get_interpolate();
		void set_interpolate(bool b);

		//depth peel
		void set_depth_peel(int dp) {depth_peel_ = dp;}
		int get_depth_peel() {return depth_peel_;}

		double compute_dt_fac(double sampling_frq_fac=-1.0, double *rate_fac=nullptr);
		double compute_dt_fac_1px(uint32_t w, uint32_t h, double zoom, double rate);

		//draw
		void eval_ml_mode(Texture* ext_msk = NULL, Texture* ext_lbl = NULL);

		//mask and label
		int get_ml_mode() {return ml_mode_;}
		void set_ml_mode(int mode) {ml_mode_ = mode;}
		//bool get_mask() {return mask_;}
		//void set_mask(bool mask) {mask_ = mask;}
		//bool get_label() {return label_;}
		//void set_label(bool label) {label_ = label;}

		//set noise reduction
		void SetNoiseRed(bool nd) {noise_red_ = nd;}
		bool GetNoiseRed() {return noise_red_;}

		//soft threshold
		static void set_soft_threshold(double val) {sw_ = val;}

		//inversion
		void set_inversion(bool mode) {inv_ = mode;}
		bool get_inversion() {return inv_;}

		//compression
		void set_compression(bool compression) {compression_ = compression;}

		//done loop
		bool get_done_loop(int mode)
		{if (mode>=0 && mode<TEXTURE_RENDER_MODES) return done_loop_[mode]; else return false;}
		void set_done_loop(bool done)
		{for (int i=0; i<TEXTURE_RENDER_MODES; i++) done_loop_[i] = done;}

		//estimated threshold
		double get_estimated_thresh()
		{ return est_thresh_; }

		//set matrices
		void set_matrices(glm::mat4 &mv_mat, glm::mat4 &proj_mat, glm::mat4 &tex_mat)
		{ m_mv_mat = mv_mat; m_proj_mat = proj_mat; m_tex_mat = tex_mat; }

		//fog
		void set_fog(bool use_fog, double fog_intensity, double fog_start, double fog_end, Color fog_col)
		{
			m_use_fog = use_fog; m_fog_intensity = fog_intensity; m_fog_start = fog_start; m_fog_end = fog_end; m_fog_col = fog_col;
		}

		bool test_against_view_clip(const BBox &bbox, const BBox &tbox, const BBox &dbox, bool persp);
		void set_clip_quaternion(Quaternion q){ m_q_cl = q; }

		void set_buffer_scale(double val)
		{
			if (val > 0.0 && val <= 1.0 && val != buffer_scale_)
			{
				buffer_scale_ = val;
				resize();
			}
		}
		double get_buffer_scale() { return buffer_scale_; }

		bool is_mask_active() { eval_ml_mode(); return mask_; }
		bool is_label_active() { eval_ml_mode(); return label_; }
		
		void set_clear_color(VkClearColorValue col) { m_clear_color = col; }
		VkClearColorValue get_clear_color() { return m_clear_color; }
        
        void set_na_mode(bool val) { m_na_mode = val; }
        bool get_na_mode() { return m_na_mode; }
        
        void set_highlight_mode(bool val) { m_highlight = val; }
        bool get_highlight_mode() { return m_highlight; }
        void set_highlight_th(double val) { m_highlight_th = val; }
        double get_highlight_th() { return m_highlight_th; }
        
#ifdef _DARWIN
        bool get_slice_mode() { return false; }
        void set_slice_mode(bool val) { slice_mode_ = val; }
#endif

		friend class MultiVolumeRenderer;

	protected:
#ifdef _DARWIN
        bool slice_mode_;
#endif
		double buffer_scale_;
		double scalar_scale_;
		double gm_scale_;
		//transfer function properties
		double gamma3d_;
		double gm_thresh_;
		double offset_;
		double lo_thresh_;
		double hi_thresh_;
		Color color_;
		Color mask_color_;
		double mask_alpha_;
		bool mask_color_set_;
		double mask_thresh_;
		double alpha_;
		//shading
		bool shading_;
		double ambient_, diffuse_, specular_, shine_;
		//colormap mode
		int colormap_mode_;//0-normal; 1-rainbow; 2-depth; 3-index
		double colormap_low_value_;
		double colormap_hi_value_;
		int colormap_;
		int colormap_proj_;
		//solid
		bool solid_;
		//interpolation
		bool interpolate_;
		//adaptive
		bool adaptive_;
		//planes
		vector<Plane *> planes_;
		//depth peel
		int depth_peel_;
		//hi quality
		bool hiqual_;
		//segmentation
		int ml_mode_;	//0-normal, 1-render with mask, 2-render with mask excluded,
						//3-random color with label, 4-random color with label+mask
		bool mask_;
		bool label_;

		//noise reduction
		bool noise_red_;
		//scale factor and filter sizes
		double sfactor_;
		double filter_size_min_;
		double filter_size_max_;
		double filter_size_shp_;

		//soft threshold
		static double sw_;

		//inversion
		bool inv_;

		//compression
		bool compression_;

		//done loop
		bool done_loop_[TEXTURE_RENDER_MODES];

		//estimated threshold
		double est_thresh_;

		//fog
		bool m_use_fog;
		double m_fog_intensity;
		double m_fog_start;
		double m_fog_end;
		Color m_fog_col;
        
        bool m_na_mode;
        
        bool m_highlight;
        double m_highlight_th;

		/*
		static KernelProgram* m_dslt_kernel = NULL;
		KernelProgram* m_dslt_l2_kernel = NULL;
		KernelProgram* m_dslt_b_kernel = NULL;
		KernelProgram* m_dslt_em_kernel = NULL;
		*/

		Quaternion m_q_cl;

		// 0:none, 1:outside, 2:inside 
		int m_mask_hide_mode;

		//calculating scaling factor, etc
		double CalcScaleFactor(double w, double h, double tex_w, double tex_h, double zoom);
		//calculate the filter sizes
		double CalcFilterSize(int type, 
			double w, double h, double tex_w, double tex_h, 
			double zoom, double sf);	//type - 1:min filter
										//		 2:max filter
										//		 3:sharpening

public:
		VkClearColorValue m_clear_color;
		std::map<vks::VulkanDevice*, VolShaderFactory::VolUniformBufs> m_volUniformBuffers;
		std::map<vks::VulkanDevice*, VRayShaderFactory::VRayUniformBufs> m_vrayUniformBuffers;
		std::map<vks::VulkanDevice*, SegShaderFactory::SegUniformBufs> m_segUniformBuffers;
		std::map<vks::VulkanDevice*, VkCommandBuffer> m_commandBuffers;
		std::map<vks::VulkanDevice*, VkCommandBuffer> m_seg_commandBuffers;
		
		struct Vertex {
			float pos[4];
			float uv[4];
		};
		struct {
			VkPipelineVertexInputStateCreateInfo inputState;
			std::vector<VkVertexInputBindingDescription> inputBinding;
			std::vector<VkVertexInputAttributeDescription> inputAttributes;
		} m_vertices;
		void setupVertexDescriptions();

		struct VSemaphoreSettings {
			uint32_t waitSemaphoreCount = 0;
			uint32_t signalSemaphoreCount = 0;
			VkSemaphore* waitSemaphores = nullptr;
			VkSemaphore* signalSemaphores = nullptr;
			bool wait_before_brk_rendering = false;
		};

		virtual void draw(
			const std::unique_ptr<vks::VFrameBuffer>& framebuf,
			bool clear_framebuf,
			bool draw_wireframe_p,
			bool interactive_mode_p,
			bool orthographic_p = false,
			double zoom = 1.0,
			int mode = 0,
			double sampling_frq_fac = -1.0,
			VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f },
			Texture* ext_msk = NULL,
			Texture* ext_lbl = NULL
		);
		void draw_volume(
			const std::unique_ptr<vks::VFrameBuffer>& framebuf,
			bool clear_framebuf,
			bool interactive_mode_p,
			bool orthographic_p = false,
			double zoom = 1.0,
			int mode = 0,
			double sampling_frq_fac = -1.0,
			VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f }
		);
		void draw_volume_ray(
			const std::unique_ptr<vks::VFrameBuffer>& framebuf,
			bool clear_framebuf,
			bool interactive_mode_p,
			bool orthographic_p = false,
			double zoom = 1.0,
			int mode = 0,
			double sampling_frq_fac = -1.0,
			VkClearColorValue clearColor = { 0.0f, 0.0f, 0.0f, 0.0f },
			Texture* ext_msk = NULL,
			Texture* ext_lbl = NULL
		);
		//type: 0-initial; 1-diffusion-based growing; 2-masked filtering
		//paint_mode: 1-select; 2-append; 3-erase; 4-diffuse; 5-flood; 6-clear; 7-all;
		//			  11-posterize
		//hr_mode (hidden removal): 0-none; 1-ortho; 2-persp
		void draw_mask(
			int type, int paint_mode, int hr_mode,
			double ini_thresh, double gm_falloff, double scl_falloff,
			double scl_translate, double w2d, double bins, bool ortho, bool estimate, Texture* ext_msk = NULL, bool use_absolute_value = false, bool save_stroke = true);

		//generate the labeling assuming the mask is already generated
		//type: 0-initialization; 1-maximum intensity filtering
		//mode: 0-normal; 1-posterized
		void draw_label(int type, int mode, double thresh, double gm_falloff);

		//calculation
		void calculate(int type, VolumeRenderer* vr_a, VolumeRenderer* vr_b, VolumeRenderer* vr_c = NULL,
			Texture* ext_msk = NULL, Texture* ext_lbl = NULL);

		//double calc_hist_3d(GLuint, GLuint, size_t, size_t, size_t);
		////return
		void return_component(int c);//base function
		void return_volume();//return the data volume
		void return_mask();//return the mask volume
		void return_label(); //return the label volume
		void return_stroke(); //return the stroke volume

		//void draw_wireframe(bool orthographic_p = false, double sampling_frq_fac = -1.0);
		
		//void draw_mask_cpu(int type, int paint_mode, int hr_mode,
		//	double ini_thresh, double gm_falloff, double scl_falloff,
		//	double scl_translate, double w2d, double bins, bool ortho, bool estimate);
		void draw_mask_th(float thresh, bool orthographic_p, bool use_absolute_value = false);
		//void draw_mask_dslt(int type, int paint_mode, int hr_mode,
		//	double ini_thresh, double gm_falloff, double scl_falloff,
		//	double scl_translate, double w2d, double bins, bool ortho, bool estimate, int dslt_r, int dslt_q, double dslt_c);
		//void dslt_mask(int rmax, int quality, double c);


		void set_mask_hide_mode(int mode)
		{
			if (tex_ && tex_->isBrxml() && m_mask_hide_mode == VOL_MASK_HIDE_NONE && mode != VOL_MASK_HIDE_NONE)
			{
				int curlv = tex_->GetCurLevel();
				tex_->setLevel(tex_->GetMaskLv());
				return_mask();
				for (int lv = 0; lv < tex_->GetLevelNum(); lv++)
				{
					if (lv != tex_->GetMaskLv())
					{
						tex_->setLevel(lv);
						clear_tex_current_mask();
					}
				}
				tex_->setLevel(curlv);
			}
			m_mask_hide_mode = mode;
		}
		int get_mask_hide_mode() { return m_mask_hide_mode; }


		static VkRenderPass prepareRenderPass(vks::VulkanDevice* device, int attatchment_num);

		struct VVolVertexBuffers {
			vks::Buffer vertexBuffer;
			vks::Buffer indexBuffer;
			uint32_t indexCount;
		};
		std::map<vks::VulkanDevice*, VVolVertexBuffers> m_vertbufs;
		void prepareVertexBuffers(vks::VulkanDevice* device, unsigned int total_slicenum);

		static void init();
		static void finalize();

		struct VRayPipeline {
			VkPipeline vkpipeline;
			VkRenderPass renderpass;
			ShaderProgram* shader;
			vks::VulkanDevice* device;
			int mode;
			int update_order;
			int colormap_mode;
			VkBool32 samplers[IMG_SHDR_SAMPLER_NUM] = { VK_FALSE };
		};
		static std::vector<VRayPipeline> m_vray_pipelines;
		static std::map<vks::VulkanDevice*, VkRenderPass> m_vray_draw_pass;
		int m_prev_vray_pipeline;
        VRayPipeline prepareVRayPipeline(vks::VulkanDevice* device, int mode, int update_order, int colormap_mode, bool persp, int multi_mode = 0, bool na_mode = false, Texture* ext_msk = NULL, bool highlight = false);
		
		struct VRayVertexBuffers {
			vks::Buffer vertexBuffer;
			vks::Buffer indexBuffer;
			uint32_t indexCount;
		};
		std::map<vks::VulkanDevice*, VRayVertexBuffers> m_vray_vbufs;
		void prepareVRayVertexBuffers(vks::VulkanDevice* device);


		struct VVolPipeline {
			VkPipeline vkpipeline;
			VkRenderPass renderpass;
			ShaderProgram* shader;
			vks::VulkanDevice* device;
			int mode;
			int update_order;
			int colormap_mode;
			VkBool32 samplers[IMG_SHDR_SAMPLER_NUM] = { VK_FALSE };
		};
		static std::vector<VVolPipeline> m_vol_pipelines;
		static std::map<vks::VulkanDevice*, VkRenderPass> m_vol_draw_pass;
		int m_prev_vol_pipeline;
		VVolPipeline prepareVolPipeline(vks::VulkanDevice* device, int mode, int update_order, int colormap_mode);
		
		struct VSlicePipeline {
			VkPipeline vkpipeline;
			ShaderProgram* shader;
			vks::VulkanDevice* device;
		};
		static std::vector<VSlicePipeline> m_vslice_pipelines;
		int m_prev_vslice_pipeline;
		VSlicePipeline prepareVSlicePipeline(vks::VulkanDevice* device, int update_order);

		struct VSegPipeline {
			VkPipeline vkpipeline;
			ShaderProgram* shader;
			vks::VulkanDevice* device;
			VkBool32 uniforms[V2DRENDER_UNIFORM_NUM] = { VK_FALSE };
			VkBool32 samplers[IMG_SHDR_SAMPLER_NUM] = { VK_FALSE };
		};
		static std::vector<VSegPipeline> m_seg_pipelines;
		int m_prev_seg_pipeline;
		VSegPipeline prepareSegPipeline(vks::VulkanDevice* device, int type, int paint_mode, int hr_mode, bool use_stroke, bool stroke_clear, int out_bytes, bool use_absolute_value);

		struct VCalPipeline {
			VkPipeline vkpipeline;
			ShaderProgram* shader;
			vks::VulkanDevice* device;
			VkBool32 uniforms[V2DRENDER_UNIFORM_NUM] = { VK_FALSE };
			VkBool32 samplers[IMG_SHDR_SAMPLER_NUM] = { VK_FALSE };
		};
		static std::vector<VCalPipeline> m_cal_pipelines;
		int m_prev_cal_pipeline;
		VCalPipeline prepareCalPipeline(vks::VulkanDevice* device, int type, int out_bytes);


		static void saveScreenshot(const char* filename, const std::shared_ptr<vks::VTexture> tex);
	};

} // End namespace FLIVR

#endif 
