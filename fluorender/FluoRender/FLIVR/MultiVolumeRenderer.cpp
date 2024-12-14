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

#include "MultiVolumeRenderer.h"
#include "VolShader.h"
#include "ShaderProgram.h"
#include "../compatibility.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

namespace FLIVR
{
#ifdef _WIN32
#undef min
#undef max
#endif

	double MultiVolumeRenderer::sw_ = 0.0;

	MultiVolumeRenderer::MultiVolumeRenderer()
		: mode_(TextureRenderer::MODE_OVER),
		depth_peel_(0),
		hiqual_(true),
		blend_num_bits_(32),
		blend_slices_(false),
		noise_red_(false),
		sfactor_(1.0),
		filter_size_min_(0.0),
		filter_size_max_(0.0),
		filter_size_shp_(0.0),
		imode_(false),
		adaptive_(true),
		irate_(1.0),
		sampling_rate_(1.0),
		num_slices_(0),
		colormap_mode_(0),
		buffer_scale_(1.0),
		main_membuf_size_(-1)
	{
	}

	MultiVolumeRenderer::MultiVolumeRenderer(MultiVolumeRenderer& copy)
		: mode_(copy.mode_),
		depth_peel_(copy.depth_peel_),
		hiqual_(copy.hiqual_),
		blend_num_bits_(copy.blend_num_bits_),
		blend_slices_(copy.blend_slices_),
		noise_red_(false),
		sfactor_(1.0),
		filter_size_min_(0.0),
		filter_size_max_(0.0),
		filter_size_shp_(0.0),
		imode_(copy.imode_),
		adaptive_(copy.adaptive_),
		irate_(copy.irate_),
		sampling_rate_(copy.sampling_rate_),
		num_slices_(0),
		colormap_mode_(copy.colormap_mode_),
		buffer_scale_(copy.buffer_scale_),
		main_membuf_size_(copy.main_membuf_size_)
	{
	}

	MultiVolumeRenderer::~MultiVolumeRenderer()
	{

	}

	//mode and sampling rate
	void MultiVolumeRenderer::set_mode(TextureRenderer::RenderMode mode)
	{
		mode_ = mode;
	}

	void MultiVolumeRenderer::set_sampling_rate(double rate)
	{
		sampling_rate_ = rate;
		//irate_ = rate>1.0 ? max(rate / 2.0, 1.0) : rate;
		irate_ = max(rate / 2.0, 0.01);
	}

	void MultiVolumeRenderer::set_interactive_rate(double rate)
	{
		irate_ = rate;
	}

	void MultiVolumeRenderer::set_interactive_mode(bool mode)
	{
		imode_ = mode;
	}

	void MultiVolumeRenderer::set_adaptive(bool b)
	{
		adaptive_ = b;
	}

	int MultiVolumeRenderer::get_slice_num()
	{
		return num_slices_;
	}

	//manages volume renderers for rendering
	void MultiVolumeRenderer::add_vr(VolumeRenderer* vr)
	{
		for (unsigned int i = 0; i < vr_list_.size(); i++)
		{
			if (vr_list_[i] == vr)
				return;
		}

		vr_list_.push_back(vr);

		if (vr && vr->tex_)
		{
			bbox_.extend(*(vr->tex_->bbox()));
			res_ = Max(res_, vr->tex_->res());
		}
	}

	void MultiVolumeRenderer::clear_vr()
	{
		vr_list_.clear();
		bbox_.reset();
		res_ = Vector(0.0);
	}

	int MultiVolumeRenderer::get_vr_num()
	{
		return int(vr_list_.size());
	}

	//draw
	void MultiVolumeRenderer::draw(
		const std::unique_ptr<vks::VFrameBuffer>& framebuf,
		bool clear_framebuf,
		bool interactive_mode_p,
		bool orthographic_p, double zoom, bool intp, double sampling_frq_fac,
		VkClearColorValue clearColor)
	{
		draw_volume(framebuf, clear_framebuf, interactive_mode_p, orthographic_p, zoom, intp, sampling_frq_fac, clearColor);
		//if(draw_wireframe_p)
		//   draw_wireframe(orthographic_p);
	}

#define FLV_SMODE_SHADING 0
#define FLV_SMODE_NO_SHADING 1
#define FLV_VR_ALPHA 0
#define FLV_VR_SOLID 2
#define FLV_CTYPE_DEFAULT 0
#define FLV_CTYPE_RAINBOW 4
#define FLV_CTYPE_INDEX 8
#define FLV_CTYPE_DEPTH 16
#define FLV_VR_INVALID 32

	inline void MultiVolumeRenderer::SubmitAndRestartCommandBuf(
		vks::VulkanDevice * device,
		VkCommandBuffer cmdbuf,
		const VkRenderPassBeginInfo & renderPassBeginInfo)
	{
		vkCmdEndRenderPass(cmdbuf);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));
		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdbuf;
		VK_CHECK_RESULT(vkQueueSubmit(device->queue, 1, &submitInfo, VK_NULL_HANDLE));

		vkQueueWaitIdle(device->queue);

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));
		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		int w = renderPassBeginInfo.renderArea.extent.width;
		int h = renderPassBeginInfo.renderArea.extent.height;
		VkViewport viewport = vks::initializers::viewport((float)w, (float)h, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(w, h, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);

	}

	inline bool MultiVolumeRenderer::TestTexMemSwap(
		vks::VulkanDevice* device,
		TextureBrick* b,
		int c,
		vector<TextureBrick*>* locked_bricks)
	{
		bool result = true;
		if (VolumeRenderer::mem_swap_)
		{
			int c = 0;
			int idx = device->findTexInPool(b, 0, b->nx(), b->ny(), b->nz(), b->nb(c), b->tex_format(c));
			if (idx == -1) {
				double new_mem = (VkDeviceSize)b->nx() * b->ny() * b->nz() * b->nb(c) / 1.04e6;
				int sw_idx = device->check_swap_memory(b, c);
				if (sw_idx == -1 && device->available_mem < new_mem)
				{
					if (locked_bricks)
					{
						for (auto bb : *locked_bricks)
							bb->prevent_tex_deletion(false);
						locked_bricks->clear();
					}
					result = false;
				}
			}
		}

		return result;
	}

	void MultiVolumeRenderer::draw_volume(
		const std::unique_ptr<vks::VFrameBuffer>& framebuf,
		bool clear_framebuf,
		bool interactive_mode_p,
		bool orthographic_p,
		double zoom, bool intp, double sampling_frq_fac,
		VkClearColorValue clearColor)
	{
		if (get_vr_num() <= 0)
			return;

		if (TextureRenderer::get_streaming())
		{
			uint32_t rn_time = GET_TICK_COUNT();
			if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
				return;
		}

		vector<VolumeRenderer*> valid_vrs;
		for (auto vr : vr_list_)
		{
			if (vr)
				valid_vrs.push_back(vr);
		}

		if (valid_vrs.size() == 0)
			return;

		set_interactive_mode(adaptive_ && interactive_mode_p);

		bool updating = (TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_start_update_loop() &&
			!TextureRenderer::get_done_update_loop());

		//stream mode (3: shadows)
		int stmode = (colormap_mode_ != 2) ? 0 : 3;

		uint32_t w = VolumeRenderer::m_vulkan->width;
		uint32_t h = VolumeRenderer::m_vulkan->height;
		uint32_t minwh = min(w, h);
		uint32_t w2 = w;
		uint32_t h2 = h;
		int i;

		double max_bufscale = 0.0;
		int maxbsid = -1;
		for (i = 0; i < (int)vr_list_.size(); i++)
		{
			VolumeRenderer* vr = vr_list_[i];
			if (vr)
			{
				double bs = vr->get_buffer_scale();
				if (max_bufscale < bs)
				{
					max_bufscale = bs;
					maxbsid = i;
				}
			}
		}
		if (maxbsid > 0 && maxbsid < (int)valid_vrs.size())
			swap(valid_vrs[0], valid_vrs[maxbsid]);

		double sf = valid_vrs[0]->CalcScaleFactor(w, h, res_.x(), res_.y(), zoom);
		if (fabs(sf - sfactor_) > 0.05)
		{
			sfactor_ = sf;
			//		  blend_framebuffer_resize_ = true;
			//		  filter_buffer_resize_ = true;
			//		  blend_fbo_resize_ = true;
		}
		else if (sf == 1.0 && sfactor_ != 1.0)
		{
			sfactor_ = sf;
			//		  blend_framebuffer_resize_ = true;
			//		  filter_buffer_resize_ = true;
			//		  blend_fbo_resize_ = true;
		}

		w2 = int(w/**sfactor_*/ * valid_vrs[0]->get_buffer_scale() + 0.5);
		h2 = int(h/**sfactor_*/ * valid_vrs[0]->get_buffer_scale() + 0.5);
		if (buffer_scale_ != valid_vrs[0]->get_buffer_scale())
		{
			buffer_scale_ = valid_vrs[0]->get_buffer_scale();
			resize();
		}

		// Set sampling rate based on interaction.
		double rate = imode_ ? irate_ * 2.0 : sampling_rate_ * 2.0;
		Vector diag = valid_vrs[0]->tex_->bbox()->diagonal();
		double dt = 0.0020 / rate;
		num_slices_ = (int)(diag.length() / dt);

		vector<TextureBrick*> bs;
		unsigned long bs_size = 0;
		for (i = 0; i < valid_vrs.size(); i++) bs_size += valid_vrs[i]->tex_->get_bricks()->size();
		bs.reserve(bs_size);
		int remain_brk = 0;
		int finished_brk = 0;

		int all_timin = INT_MAX, all_timax = -INT_MAX;
		for (auto vr : valid_vrs)
		{
			if (!vr)
				continue;

			Texture* tex = vr->tex_;

			double vr_dt = vr->compute_dt_fac_1px(w, h, zoom, rate);

			vector<TextureBrick*>* brs = tex->get_bricks();
			Ray view_ray = vr->compute_view();
			for (int j = 0; j < brs->size(); j++)
			{
				TextureBrick* b = (*brs)[j];

				b->set_vr(vr);

				bool disp = true;
				if (updating) disp = b->get_disp();

				if (b->compute_t_index_min_max(view_ray, vr_dt))
				{
					int tmp = b->timin();
					all_timin = (all_timin > tmp) ? tmp : all_timin;
					tmp = b->timax();
					all_timax = (all_timax < tmp) ? tmp : all_timax;

					bs.push_back(b);

					if (!b->drawn(stmode) && disp) remain_brk++;
				}
				else if (updating && !b->drawn(stmode))
				{
					b->set_drawn(stmode, true);

					if (disp)
						TextureRenderer::cur_brick_num_++;
				}
			}
		}
		bs_size = bs.size();

		if (TextureRenderer::get_mem_swap() && remain_brk == 0) return;


		vks::VulkanDevice* prim_dev = VolumeRenderer::m_vulkan->devices[0];

		int blendmode = blend_slices_ ? TextureRenderer::MODE_SLICE : TextureRenderer::MODE_OVER;

		map<VolumeRenderer*, MultiVolRenederSettings> rsettings;
		for (auto vr : valid_vrs)
		{
			MultiVolRenederSettings setting;

			VRayShaderFactory::VRayVertShaderUBO vert_ubo;
			VRayShaderFactory::VRayFragShaderBaseUBO frag_ubo;
			VRayShaderFactory::VRayFragShaderBrickConst frag_const;

			vr->set_depth_peel(depth_peel_);
			setting.pipeline = vr->prepareVRayPipeline(prim_dev, blendmode, vr->update_order_, vr->colormap_mode_, !orthographic_p);
			setting.pipelineLayout = VolumeRenderer::m_vulkan->vray_shader_factory_->pipeline_[prim_dev].pipelineLayout;

			Ray view_ray = vr->compute_view();
			vector<TextureBrick*>* brs = vr->tex_->get_bricks();

			Vector light = view_ray.direction();
			light.safe_normalize();

			frag_ubo.loc0_light_alpha = { light.x(), light.y(), light.z(), vr->alpha_ };
			if (vr->shading_)
				frag_ubo.loc1_material = { 2.0 - vr->ambient_, vr->diffuse_, vr->specular_, vr->shine_ };
			else
				frag_ubo.loc1_material = { 2.0 - vr->ambient_, 0.0, vr->specular_, vr->shine_ };

			//spacings
			double spcx, spcy, spcz;
			vr->tex_->get_spacings(spcx, spcy, spcz);
			if (vr->colormap_mode_ == 3)
			{
				int max_id = (*brs)[0]->nb(0) == 2 ? USHRT_MAX : UCHAR_MAX;
				frag_ubo.loc5_spc_id = { spcx, spcy, spcz, max_id };
			}
			else
				frag_ubo.loc5_spc_id = { spcx, spcy, spcz, 1.0 };

			//transfer function
			frag_ubo.loc2_scscale_th = { vr->inv_ ? -(vr->scalar_scale_) : vr->scalar_scale_, vr->gm_scale_, vr->lo_thresh_, vr->hi_thresh_ };
			frag_ubo.loc3_gamma_offset = { 1.0 / vr->gamma3d_, vr->gm_thresh_, vr->offset_, vr->sw_ };
			switch (vr->colormap_mode_)
			{
			case 0://normal
				frag_ubo.loc6_colparam = { vr->color_.r(), vr->color_.g(), vr->color_.b(), 0.0 };
				break;
			case 1://colormap
				frag_ubo.loc6_colparam = { vr->colormap_low_value_, vr->colormap_hi_value_, vr->colormap_hi_value_ - vr->colormap_low_value_, 0.0 };
				break;
			case 2://depth map
				frag_ubo.loc6_colparam = { vr->color_.r(), vr->color_.g(), vr->color_.b(), 0.0 };
				break;
			case 3://indexed color
				HSVColor hsv(vr->color_);
				double luminance = hsv.val();
				frag_ubo.loc6_colparam = { 1.0 / double(w2), 1.0 / double(h2), luminance, 0.0 };
				break;
			}

			//setup depth peeling
			frag_ubo.loc7_view = { 1.0 / double(w2), 1.0 / double(h2), 1.0 / (rate * min(max(zoom, 1.0), 10.0) * 2.0), 0.0 };

			//fog
			frag_ubo.loc8_fog = { vr->m_fog_intensity, vr->m_fog_start, vr->m_fog_end, 0.0 };
			frag_ubo.loc9_fog_col = { vr->m_fog_col.r(), vr->m_fog_col.g(), vr->m_fog_col.b(), 0.0 };

			//set clipping planes
			double abcd[4];
			vr->planes_[0]->get(abcd);
			frag_ubo.plane0 = { abcd[0], abcd[1], abcd[2], abcd[3] };
			vr->planes_[1]->get(abcd);
			frag_ubo.plane1 = { abcd[0], abcd[1], abcd[2], abcd[3] };
			vr->planes_[2]->get(abcd);
			frag_ubo.plane2 = { abcd[0], abcd[1], abcd[2], abcd[3] };
			vr->planes_[3]->get(abcd);
			frag_ubo.plane3 = { abcd[0], abcd[1], abcd[2], abcd[3] };
			vr->planes_[4]->get(abcd);
			frag_ubo.plane4 = { abcd[0], abcd[1], abcd[2], abcd[3] };
			vr->planes_[5]->get(abcd);
			frag_ubo.plane5 = { abcd[0], abcd[1], abcd[2], abcd[3] };

			// Set up transform
			Transform* tform = vr->tex_->transform();
			double mvmat[16];
			tform->get_trans(mvmat);
			vr->m_mv_mat2 = glm::mat4(
				mvmat[0], mvmat[4], mvmat[8], mvmat[3],
				mvmat[1], mvmat[5], mvmat[9], mvmat[7],
				mvmat[2], mvmat[6], mvmat[10], mvmat[11],
				mvmat[12], mvmat[13], mvmat[14], mvmat[15]);
			vr->m_mv_mat2 = vr->m_mv_mat * vr->m_mv_mat2;
			vert_ubo.proj_mat = vr->m_proj_mat;
			vert_ubo.mv_mat = vr->m_mv_mat2;

			frag_ubo.proj_mat_inv = glm::inverse(vr->m_proj_mat);
			frag_ubo.mv_mat_inv = glm::inverse(vr->m_mv_mat2);

			setting.view_ray = view_ray;
			if (intp && vr->colormap_mode_ != 3)
				setting.filter = VK_FILTER_LINEAR;
			else
				setting.filter = VK_FILTER_NEAREST;

			rsettings[vr] = setting;


			prim_dev->GetNextUniformBuffer(sizeof(VRayShaderFactory::VRayVertShaderUBO), rsettings[vr].vert_ubuf, rsettings[vr].vert_ubuf_offset);
			prim_dev->GetNextUniformBuffer(sizeof(VRayShaderFactory::VRayFragShaderBaseUBO), rsettings[vr].frag_ubuf, rsettings[vr].frag_ubuf_offset);
			VolumeRenderer::m_vulkan->vray_shader_factory_->getDescriptorSetWriteUniforms(
				prim_dev,
				rsettings[vr].vert_ubuf,
				rsettings[vr].frag_ubuf,
				rsettings[vr].descriptorWritesBase);
			if (vr->colormap_mode_ == 3)
			{
				auto palette = vr->get_palette();
				if (palette.count(prim_dev) > 0)
					rsettings[vr].descriptorWritesBase.push_back(VRayShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 7, &palette[prim_dev]->descriptor));
			}

			if (depth_peel_ == 1 || depth_peel_ == 3 || depth_peel_ == 4)//draw volume before 15
				rsettings[vr].descriptorWritesBase.push_back(VRayShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 15, &dtex_back_->descriptor));
			if (depth_peel_ == 2 || depth_peel_ == 3 || depth_peel_ == 4 || depth_peel_ == 5)//draw volume after 14
				rsettings[vr].descriptorWritesBase.push_back(VRayShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 14, &dtex_front_->descriptor));

			rsettings[vr].vert_ubuf.copyTo(&vert_ubo, sizeof(VRayShaderFactory::VRayVertShaderUBO), rsettings[vr].vert_ubuf_offset);
			rsettings[vr].frag_ubuf.copyTo(&frag_ubo, sizeof(VRayShaderFactory::VRayFragShaderBaseUBO), rsettings[vr].frag_ubuf_offset);
			if (rsettings[vr].vert_ubuf.buffer == rsettings[vr].frag_ubuf.buffer)
				rsettings[vr].vert_ubuf.flush(prim_dev->GetCurrentUniformBufferOffset() - rsettings[vr].vert_ubuf_offset, rsettings[vr].vert_ubuf_offset);
			else
			{
				if (rsettings[vr].vert_ubuf_offset == 0)
					rsettings[vr].vert_ubuf.flush();
				else
					rsettings[vr].vert_ubuf.flush(VK_WHOLE_SIZE, rsettings[vr].vert_ubuf_offset);
				rsettings[vr].frag_ubuf.flush();
			}


			vr->prepareVRayVertexBuffers(prim_dev);
		}

		if (slice_fbo_ && (slice_fbo_->w != w2 || slice_fbo_->h != h2 || slice_fbo_->renderPass != rsettings[valid_vrs[0]].pipeline.renderpass))
		{
			slice_fbo_.reset();
			slice_tex_.reset();
		}
		if (!slice_fbo_)
		{
			slice_fbo_ = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			slice_fbo_->w = w2;
			slice_fbo_->h = h2;
			slice_fbo_->device = prim_dev;

			if (!slice_tex_)
				slice_tex_ = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, w2, h2);

			slice_fbo_->addAttachment(slice_tex_);

			if (!slice_fbo_->renderPass)
				slice_fbo_->setup(rsettings[valid_vrs[0]].pipeline.renderpass);
		}

		Vulkan2dRender::V2DRenderParams blend_params;
		if (blend_slices_)
		{
			blend_params.pipeline =
				VolumeRenderer::m_v2drender->preparePipeline(
					IMG_SHADER_TEXTURE_LOOKUP,
					V2DRENDER_BLEND_OVER_INV,
					framebuf->attachments[0]->format,
					framebuf->attachments.size(),
					0,
					framebuf->attachments[0]->is_swapchain_images);
			blend_params.tex[0] = slice_tex_.get();
			blend_params.clear = true;

			if (blend_framebuffer_ &&
				(blend_framebuffer_->w != w || blend_framebuffer_->h != h || blend_framebuffer_->renderPass != blend_params.pipeline.pass))
			{
				blend_framebuffer_.reset();
				blend_tex_id_.reset();
			}
			if (!blend_framebuffer_)
			{
				blend_framebuffer_ = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
				blend_framebuffer_->w = w2;
				blend_framebuffer_->h = h2;
				blend_framebuffer_->device = prim_dev;

				if (!blend_tex_id_)
					blend_tex_id_ = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, w2, h2);

				blend_framebuffer_->addAttachment(blend_tex_id_);

				if (!blend_framebuffer_->renderPass)
					blend_framebuffer_->setup(blend_params.pipeline.pass);
			}
		}

		bool clear = true;
		VkCommandBuffer cmdbuf = VK_NULL_HANDLE;
		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = rsettings[valid_vrs[0]].pipeline.renderpass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = slice_fbo_->w;
		renderPassBeginInfo.renderArea.extent.height = slice_fbo_->h;
		renderPassBeginInfo.framebuffer = slice_fbo_->framebuffer;


		cmdbuf = prim_dev->GetNextCommandBuffer();
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
		cmdBufInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdbuf, &cmdBufInfo));

		if (depth_peel_ == 1 || depth_peel_ == 3 || depth_peel_ == 4)//draw volume before 15
		{
			vks::tools::setImageLayout(
				cmdbuf,
				dtex_back_->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				dtex_back_->subresourceRange);
			dtex_back_->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}
		if (depth_peel_ == 2 || depth_peel_ == 3 || depth_peel_ == 4 || depth_peel_ == 5)//draw volume after 14
		{
			vks::tools::setImageLayout(
				cmdbuf,
				dtex_front_->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				dtex_front_->subresourceRange);
			dtex_front_->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		vkCmdBeginRenderPass(cmdbuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)w2, (float)h2, 0.0f, 1.0f);
		vkCmdSetViewport(cmdbuf, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(w2, h2, 0, 0);
		vkCmdSetScissor(cmdbuf, 0, 1, &scissor);


		vector<MultiVolBrick> cur_brs;
		int cur_bid;
		bool order = TextureRenderer::get_update_order();
		int start_i = order ? all_timin : all_timax;

		vector<TextureBrick*> locked_bricks;

		if (updating)
		{
			start_i += TextureRenderer::cur_tid_offset_multi_;
		}

		std::sort(bs.begin(), bs.end(), order ? TextureBrick::less_timin : TextureBrick::high_timax);
		cur_bid = 0;
		for (i = start_i; order ? (i <= all_timax) : (i >= all_timin); i += order ? 1 : -1)
		{
			if (TextureRenderer::get_streaming())
			{
				uint32_t rn_time = GET_TICK_COUNT();
				if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
					break;
			}

			int32_t new_brs_num = 0;
			bool fail = false;
			while (cur_bid < bs_size && (order ? (bs[cur_bid]->timin() <= i) : (bs[cur_bid]->timax() >= i)))
			{
				if (updating)
				{
					if (bs[cur_bid]->drawn(stmode))
					{
						cur_bid++;
						continue;
					}

					if (bs[cur_bid]->get_disp())
					{
						MultiVolBrick mb;
						mb.b = bs[cur_bid];
						cur_brs.push_back(mb);
						new_brs_num++;
					}
					else
					{
						if (!bs[cur_bid]->drawn(stmode))
							bs[cur_bid]->set_drawn(stmode, true);
					}
				}
				else
				{
					MultiVolBrick mb;
					mb.b = bs[cur_bid];
					cur_brs.push_back(mb);
					new_brs_num++;
				}

				cur_bid++;
			}

			for (int j = (int)cur_brs.size() - 1; j >= (int)cur_brs.size() - new_brs_num; j--)
			{
				VolumeRenderer* vr = cur_brs[j].b->get_vr();
				TextureBrick* b = cur_brs[j].b;

				//for brick transformation
				BBox dbox = b->dbox();
				cur_brs[j].frag_const.b_scale_invnx = { float(dbox.max().x() - dbox.min().x()),
														float(dbox.max().y() - dbox.min().y()),
														float(dbox.max().z() - dbox.min().z()),
														1.0f / b->nx() };

				cur_brs[j].frag_const.b_trans_invny = { float(dbox.min().x()), float(dbox.min().y()), float(dbox.min().z()), 1.0f / b->ny() };
				cur_brs[j].frag_const.mask_b_scale_invnz.w = 1.0f / b->nz();

				BBox tbox = b->tbox();
				cur_brs[j].frag_const.tbmin = { (float)tbox.min().x(), (float)tbox.min().y(), (float)tbox.min().z(), 0.0f };
				cur_brs[j].frag_const.tbmax = { (float)tbox.max().x(), (float)tbox.max().y(), (float)tbox.max().z(), 0.0f };
			}

			if (cur_brs.size() == 0)
				continue;

			if (i > 0)
			{
				long long loadedsize = 0LL;
				int loaded_num = 0;
				for (int j = 0; j < cur_brs.size(); j++)
				{
					TextureBrick* b = cur_brs[j].b;
					VolumeRenderer* vr = b->get_vr();
					if (vr->tex_->isBrxml() && b->isLoaded())
					{
						loadedsize += (long long)b->nx() * b->ny() * b->nz() * b->nb(0);
						loaded_num++;
					}
						
				}

				bool blend_clear = true;
				for (int j = 0; j < cur_brs.size(); j++)
				{
					TextureBrick* b = cur_brs[j].b;
					VolumeRenderer* vr = b->get_vr();
					Ray view_ray = rsettings[vr].view_ray;

					bool restart = !TestTexMemSwap(prim_dev, b, 0, nullptr);
					if (restart)
					{
						for (auto bb : locked_bricks)
							bb->prevent_tex_deletion(false);
						locked_bricks.clear();
						SubmitAndRestartCommandBuf(prim_dev, cmdbuf, renderPassBeginInfo);
					}

					shared_ptr<vks::VTexture> brktex, msktex, lbltex;
					vector<TextureBrick*> tempbv(1, b);
					brktex = vr->load_brick(prim_dev, 0, 0, &tempbv, 0, rsettings[vr].filter, vr->compression_, stmode, false, nullptr, nullptr, false);

					if (!brktex && vr->tex_->isBrxml() && !b->isLoaded() && main_membuf_size_ > 0LL)
					{
						long long bsize = (long long)b->nx() * b->ny() * b->nz() * b->nb(0);
						if (loadedsize + bsize > main_membuf_size_)
						{
							b->tex_data_brk(0, vr->tex_->GetFileName(b->getID()));
							if (b->isLoaded())
							{
								//retry
								brktex = vr->load_brick(prim_dev, 0, 0, &tempbv, 0, rsettings[vr].filter, vr->compression_, stmode, false, nullptr, nullptr, false);
								b->freeBrkData();
							}
						}
					}

					if (!brktex)
					{
						fail = true;
						continue;
					}

					b->prevent_tex_deletion(true);
					locked_bricks.push_back(b);

					if (vr->tex_->nmask() != -1 && vr->m_mask_hide_mode != VOL_MASK_HIDE_NONE)
					{
						if (vr->tex_->isBrxml() && vr->tex_->GetMaskLv() != vr->tex_->GetCurLevel())
						{
							vr->ms_pThreadCS->Enter();

							int cnx = b->nx(), cny = b->ny(), cnz = b->nz();
							int cox = b->ox(), coy = b->oy(), coz = b->oz();
							int cmx = b->mx(), cmy = b->my(), cmz = b->mz();
							int csx = b->sx(), csy = b->sy(), csz = b->sz();
							auto cdata = b->get_nrrd(0);
							auto cmdata = b->get_nrrd(vr->tex_->nmask());

							b->set_nrrd(vr->tex_->get_nrrd_lv(vr->tex_->GetMaskLv(), 0), 0);
							b->set_nrrd(vr->tex_->get_nrrd(vr->tex_->nmask()), vr->tex_->nmask());
							int sx = b->sx(), sy = b->sy(), sz = b->sz();
							double xscale = (double)sx / csx;
							double yscale = (double)sy / csy;
							double zscale = (double)sz / csz;

							double ox_d = (double)cox * xscale;
							double oy_d = (double)coy * yscale;
							double oz_d = (double)coz * zscale;
							int ox = (int)floor(ox_d);
							int oy = (int)floor(oy_d);
							int oz = (int)floor(oz_d);

							b->ox(ox); b->oy(oy); b->oz(oz);

							double endx_d = (double)(cox + cnx) * xscale;
							double endy_d = (double)(coy + cny) * yscale;
							double endz_d = (double)(coz + cnz) * zscale;
							int nx = (int)ceil(endx_d) - ox;
							int ny = (int)ceil(endy_d) - oy;
							int nz = (int)ceil(endz_d) - oz;

							auto msknrrd = b->get_nrrd(vr->tex_->nmask());
							size_t msk_dim = msknrrd->getNrrd()->dim;
							size_t msk_size[3] = {};
							int msk_offset = 0;
							if (msk_dim > 3) msk_offset = 1;
							for (size_t p = 0; p < 3; p++)
								msk_size[p] = (size_t)msknrrd->getNrrd()->axis[p + msk_offset].size;
							nx = ox + nx > msk_size[0] ? msk_size[0] - ox : nx;
							ny = oy + ny > msk_size[1] ? msk_size[1] - oy : ny;
							nz = oz + nz > msk_size[2] ? msk_size[2] - oz : nz;

							b->nx(nx); b->ny(ny); b->nz(nz);
							b->mx(nx); b->my(ny); b->mz(nz);

							if (!restart)
							{
								restart = !TestTexMemSwap(prim_dev, b, vr->tex_->nmask(), &locked_bricks);
								if (restart)
									SubmitAndRestartCommandBuf(prim_dev, cmdbuf, renderPassBeginInfo);
							}
							msktex = vr->load_brick_mask(prim_dev, &tempbv, 0, rsettings[vr].filter, false, 0, true, false, nullptr, nullptr, false);

							double trans_x = (ox_d - ox) / nx;
							double trans_y = (oy_d - oy) / ny;
							double trans_z = (oz_d - oz) / nz;
							double scale_x = (endx_d - ox_d) / nx;
							double scale_y = (endy_d - oy_d) / ny;
							double scale_z = (endz_d - oz_d) / nz;

							cur_brs[j].frag_const.mask_b_scale_invnz = { (float)scale_x, (float)scale_y, (float)scale_z , 1.0f };
							cur_brs[j].frag_const.mask_b_trans = { (float)trans_x, (float)trans_y, (float)trans_z, 0.0f };

							b->nx(cnx); b->ny(cny); b->nz(cnz);
							b->ox(cox); b->oy(coy); b->oz(coz);
							b->mx(cmx); b->my(cmy); b->mz(cmz);
							b->set_nrrd(cdata, 0);
							b->set_nrrd(cmdata, vr->tex_->nmask());

							vr->ms_pThreadCS->Leave();
						}
						else
						{
							if (!restart)
							{
								restart = !TestTexMemSwap(prim_dev, b, vr->tex_->nmask(), &locked_bricks);
								if (restart)
									SubmitAndRestartCommandBuf(prim_dev, cmdbuf, renderPassBeginInfo);
							}
							msktex = vr->load_brick_mask(prim_dev, &tempbv, 0, rsettings[vr].filter, false, 0, true, false, nullptr, nullptr, false);

							cur_brs[j].frag_const.mask_b_scale_invnz = { 1.0f, 1.0f, 1.0f, 1.0f };
							cur_brs[j].frag_const.mask_b_trans = { 0.0f, 0.0f, 0.0f, 0.0f };
						}
					}

					cur_brs[j].descriptorWrites = rsettings[vr].descriptorWritesBase;
					brktex->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					cur_brs[j].descriptorWrites.push_back(VRayShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 0, &brktex->descriptor));

					if (msktex)
					{
						msktex->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						cur_brs[j].descriptorWrites.push_back(VRayShaderFactory::writeDescriptorSetTex(VK_NULL_HANDLE, 2, &msktex->descriptor));
					}

					Transform mv;
					mv.set_trans(glm::value_ptr(vr->m_mv_mat));
					Transform* tform = vr->tex_->transform();
                    
                    double tmpmat[16];
                    tform->get_trans(tmpmat);
                    Transform tform_tr;
                    double tr_mvmat[16] = {
                        tmpmat[0], tmpmat[1], tmpmat[2], tmpmat[12],
                        tmpmat[4], tmpmat[5], tmpmat[6], tmpmat[13],
                        tmpmat[8], tmpmat[9], tmpmat[10], tmpmat[14],
                        tmpmat[3], tmpmat[7], tmpmat[11], tmpmat[15]
                    };
                    tform_tr.set(tr_mvmat);
                    
					unsigned int slicenum = 0;
					int timax = i, timin = i;
					double vr_dt = b->dt();
					Point p = view_ray.parameter(timin * vr_dt);
					Point p2 = view_ray.parameter(timax * vr_dt);
					Vector dv = view_ray.direction() * vr_dt;
					p = mv.project(tform_tr.project(p));
					p2 = mv.project(tform_tr.project(p2));
					dv = mv.project(tform_tr.project(dv));
					cur_brs[j].frag_const.loc_zmin_zmax_dz = { p.z(), p2.z(), dv.z() };
					cur_brs[j].frag_const.stepnum = slicenum;

					vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, rsettings[vr].pipeline.vkpipeline);

					if (!cur_brs[j].descriptorWrites.empty())
					{
						prim_dev->vkCmdPushDescriptorSetKHR(
							cmdbuf,
							VK_PIPELINE_BIND_POINT_GRAPHICS,
							rsettings[vr].pipelineLayout,
							0,
							cur_brs[j].descriptorWrites.size(),
							cur_brs[j].descriptorWrites.data()
						);
					}

					if ((blend_slices_ && blend_clear) || clear)
					{
						VkClearValue clearValues[1];
						clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

						VkClearAttachment clearAttachments[1] = {};
						clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
						clearAttachments[0].clearValue = clearValues[0];
						clearAttachments[0].colorAttachment = 0;

						VkClearRect clearRect[1] = {};
						clearRect[0].layerCount = 1;
						clearRect[0].rect.offset = { 0, 0 };
						clearRect[0].rect.extent = { w2, h2 };

						vkCmdClearAttachments(
							cmdbuf,
							1,
							clearAttachments,
							1,
							clearRect);

						clear = false;
						blend_clear = false;
					}

					vkCmdPushConstants(
						cmdbuf,
						rsettings[vr].pipelineLayout,
						VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
						0,
						sizeof(VRayShaderFactory::VRayFragShaderBrickConst),
						&cur_brs[j].frag_const);

					VkDeviceSize offsets[1] = { 0 };
					vkCmdBindVertexBuffers(cmdbuf, 0, 1, &valid_vrs[0]->m_vray_vbufs[prim_dev].vertexBuffer.buffer, offsets);
					vkCmdBindIndexBuffer(cmdbuf, valid_vrs[0]->m_vray_vbufs[prim_dev].indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
					vkCmdDrawIndexed(cmdbuf, valid_vrs[0]->m_vray_vbufs[prim_dev].indexCount, 1, 0, 0, 0);

				}//for (int j = 0; j < cur_brs.size(); j++)

				if (blend_slices_ && colormap_mode_ != 2)
				{
					SubmitAndRestartCommandBuf(prim_dev, cmdbuf, renderPassBeginInfo);

					VolumeRenderer::m_v2drender->render(blend_framebuffer_, blend_params);
					blend_params.clear = false;
				}

			}

			vector<MultiVolBrick>::iterator ite = cur_brs.begin();
			while (ite != cur_brs.end())
			{
				if ((order && (*ite).b->timax() <= i) || (!order && (*ite).b->timin() >= i))
				{
					//count up
					if (updating)
						(*ite).b->set_drawn(stmode, true);

					(*ite).b->clear_polygons();
					TextureRenderer::cur_brick_num_++;
					finished_brk++;

					ite = cur_brs.erase(ite);
				}
				else ite++;
			}

			if (TextureRenderer::get_streaming() && fail)
			{
				uint32_t rn_time = GET_TICK_COUNT();
				if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
					break;
			}

		}//for (i = start_i; order?(i <= all_timax):(i >= all_timin); i += order?1:-1)

		vkCmdEndRenderPass(cmdbuf);

		if (depth_peel_ == 1 || depth_peel_ == 3 || depth_peel_ == 4)//draw volume before 15
		{
			dtex_back_->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			vks::tools::setImageLayout(
				cmdbuf,
				dtex_back_->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				dtex_back_->subresourceRange);
		}
		if (depth_peel_ == 2 || depth_peel_ == 3 || depth_peel_ == 4 || depth_peel_ == 5)//draw volume after 14
		{
			dtex_front_->descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			vks::tools::setImageLayout(
				cmdbuf,
				dtex_front_->image,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				dtex_front_->subresourceRange);
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdbuf));

		VkSubmitInfo submitInfo = vks::initializers::submitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmdbuf;

		VK_CHECK_RESULT(vkQueueSubmit(prim_dev->queue, 1, &submitInfo, VK_NULL_HANDLE));
		vkQueueWaitIdle(prim_dev->queue);

		for (auto bb : locked_bricks)
			bb->prevent_tex_deletion(false);


		TextureRenderer::cur_tid_offset_multi_ = i - (order ? all_timin : all_timax);

		if ((order && TextureRenderer::cur_tid_offset_multi_ > all_timax - all_timin) ||
			(!order && TextureRenderer::cur_tid_offset_multi_ < all_timin - all_timax))
		{
			TextureRenderer::cur_tid_offset_multi_ = 0;
		}

		if (TextureRenderer::get_mem_swap() && remain_brk > 0 && finished_brk == remain_brk)
		{
			TextureRenderer::set_clear_chan_buffer(true);
			TextureRenderer::set_save_final_buffer();
			for (i = 0; i < (int)vr_list_.size(); i++)
				vr_list_[i]->done_loop_[stmode] = true;
		}

		if (TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_cur_brick_num() == TextureRenderer::get_total_brick_num())
		{
			TextureRenderer::set_done_update_loop(true);
		}


		//output
		if (blend_num_bits_ > 8)
		{
			ShaderProgram* img_shader = 0;

			if (noise_red_ && colormap_mode_ != 2)
			{
				//FILTERING/////////////////////////////////////////////////////////////////
				filter_size_min_ = valid_vrs[0]->CalcFilterSize(4, w, h, valid_vrs[0]->tex_->nx(), valid_vrs[0]->tex_->ny(), zoom, sfactor_);

				Vulkan2dRender::V2DRenderParams filter_params;
				filter_params.pipeline =
					VolumeRenderer::m_v2drender->preparePipeline(
						IMG_SHDR_FILTER_BLUR,
						V2DRENDER_BLEND_OVER,
						VK_FORMAT_R32G32B32A32_SFLOAT,
						1,
						0,
						filter_buffer_->attachments[0]->is_swapchain_images);

				if (filter_buffer_ &&
					(filter_buffer_->w != w2 || filter_buffer_->h != h2 || filter_buffer_->renderPass != filter_params.pipeline.pass))
				{
					filter_buffer_.reset();
					filter_tex_id_.reset();
				}
				if (!filter_buffer_)
				{
					filter_buffer_ = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
					filter_buffer_->w = w2;
					filter_buffer_->h = h2;
					filter_buffer_->device = prim_dev;

					if (!filter_tex_id_)
						filter_tex_id_ = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, w2, h2);

					filter_buffer_->addAttachment(filter_tex_id_);

					filter_buffer_->setup(filter_params.pipeline.pass);
				}

				filter_params.clear = true;
				filter_params.loc[0] = { filter_size_min_ / w2, filter_size_min_ / h2, 1.0 / w2, 1.0 / h2 };
				filter_params.tex[0] = blend_slices_ ? blend_tex_id_.get() : slice_tex_.get();

				vks::VulkanSemaphoreSettings sem = prim_dev->GetNextRenderSemaphoreSettings();
				filter_params.waitSemaphoreCount = sem.waitSemaphoreCount;
				filter_params.waitSemaphores = sem.waitSemaphores;
				filter_params.signalSemaphoreCount = sem.signalSemaphoreCount;
				filter_params.signalSemaphores = sem.signalSemaphores;

				VolumeRenderer::m_v2drender->render(filter_buffer_, filter_params);
			}

			Vulkan2dRender::V2DRenderParams params;
			vks::VulkanSemaphoreSettings semfinal = prim_dev->GetNextRenderSemaphoreSettings();

			if (noise_red_ && colormap_mode_ != 2)
			{
				params.pipeline =
					VolumeRenderer::m_v2drender->preparePipeline(
						IMG_SHDR_FILTER_SHARPEN,
						V2DRENDER_BLEND_OVER_INV,
						framebuf->attachments[0]->format,
						framebuf->attachments.size(),
						0,
						framebuf->attachments[0]->is_swapchain_images);
				params.tex[0] = filter_tex_id_.get();
				filter_size_shp_ = valid_vrs[0]->CalcFilterSize(3, w, h, valid_vrs[0]->tex_->nx(), valid_vrs[0]->tex_->ny(), zoom, sfactor_);
				params.loc[0] = { filter_size_shp_ / w, filter_size_shp_ / h, 0.0, 0.0 };
			}
			else
			{
				params.pipeline =
					VolumeRenderer::m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP,
						colormap_mode_ != 2 ? V2DRENDER_BLEND_OVER_INV : V2DRENDER_BLEND_OVER,
						framebuf->attachments[0]->format,
						framebuf->attachments.size(),
						0,
						framebuf->attachments[0]->is_swapchain_images);
				params.tex[0] = blend_slices_ ? blend_tex_id_.get() : slice_tex_.get();
			}

			params.waitSemaphoreCount = semfinal.waitSemaphoreCount;
			params.waitSemaphores = semfinal.waitSemaphores;
			params.signalSemaphoreCount = semfinal.signalSemaphoreCount;
			params.signalSemaphores = semfinal.signalSemaphores;

			params.clear = clear_framebuf;
			params.clearColor = clearColor;

			if (!framebuf->renderPass)
				framebuf->replaceRenderPass(params.pipeline.pass);

			VolumeRenderer::m_v2drender->render(framebuf, params);

			VK_CHECK_RESULT(vkQueueWaitIdle(prim_dev->queue));

			//VolumeRenderer::saveScreenshot("E:\\vulkan_screenshot.ppm", framebuf->attachments[0]);
		}

	}

	vector<TextureBrick*>* MultiVolumeRenderer::get_combined_bricks(
		Point& center, Ray& view, bool is_orthographic)
	{
		if (!vr_list_.size())
			return 0;

		if (!vr_list_[0]->tex_->get_sort_bricks())
			return vr_list_[0]->tex_->get_quota_bricks();

		size_t i, j, k;
		vector<TextureBrick*>* bs;
		vector<TextureBrick*>* bs0;
		vector<TextureBrick*>* result;
		Point brick_center;
		double d;

		for (i = 0; i < vr_list_.size(); i++)
		{
			//sort each brick list based on distance to center
			bs = vr_list_[i]->tex_->get_bricks();
			for (j = 0; j < bs->size(); j++)
			{
				brick_center = (*bs)[j]->bbox().center();
				d = (brick_center - center).length();
				(*bs)[j]->set_d(d);
			}
			std::sort((*bs).begin(), (*bs).end(), TextureBrick::sort_dsc);

			//assign indecis so that bricks can be selected later
			for (j = 0; j < bs->size(); j++)
				(*bs)[j]->set_ind(j);
		}

		//generate quota brick list for vr0
		bs0 = vr_list_[0]->tex_->get_bricks();
		result = vr_list_[0]->tex_->get_quota_bricks();
		result->clear();
		int quota = 0;
		int count;
		TextureBrick* pb;
		size_t ind;
		bool found;
		for (i = 0; i < vr_list_.size(); i++)
		{
			//insert nonduplicated bricks into result list
			bs = vr_list_[i]->tex_->get_bricks();
			quota = vr_list_[i]->get_quota_bricks_chan();
			//quota = quota/2+1;
			count = 0;
			for (j = 0; j < bs->size(); j++)
			{
				pb = (*bs)[j];
				if (pb->get_priority() > 0)
					continue;
				ind = pb->get_ind();
				found = false;
				for (k = 0; k < result->size(); k++)
				{
					if (ind == (*result)[k]->get_ind())
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					result->push_back((*bs0)[ind]);
					count++;
					if (count == quota)
						break;
				}
			}
		}
		//reorder result
		for (i = 0; i < result->size(); i++)
		{
			Point minp((*result)[i]->bbox().min());
			Point maxp((*result)[i]->bbox().max());
			Vector diag((*result)[i]->bbox().diagonal());
			minp += diag / 1000.;
			maxp -= diag / 1000.;
			Point corner[8];
			corner[0] = minp;
			corner[1] = Point(minp.x(), minp.y(), maxp.z());
			corner[2] = Point(minp.x(), maxp.y(), minp.z());
			corner[3] = Point(minp.x(), maxp.y(), maxp.z());
			corner[4] = Point(maxp.x(), minp.y(), minp.z());
			corner[5] = Point(maxp.x(), minp.y(), maxp.z());
			corner[6] = Point(maxp.x(), maxp.y(), minp.z());
			corner[7] = maxp;
			double d = 0.0;
			for (unsigned int c = 0; c < 8; c++)
			{
				double dd;
				if (is_orthographic)
				{
					// orthographic: sort bricks based on distance to the view plane
					dd = Dot(corner[c], view.direction());
				}
				else
				{
					// perspective: sort bricks based on distance to the eye point
					dd = (corner[c] - view.origin()).length();
				}
				if (c == 0 || dd < d) d = dd;
			}
			(*result)[i]->set_d(d);
		}
		if (TextureRenderer::get_update_order() == 0)
			std::sort((*result).begin(), (*result).end(), TextureBrick::sort_asc);
		else if (TextureRenderer::get_update_order() == 1)
			std::sort((*result).begin(), (*result).end(), TextureBrick::sort_dsc);
		vr_list_[0]->tex_->reset_sort_bricks();

		//duplicate result into other quota-bricks
		for (i = 1; i < vr_list_.size(); i++)
		{
			bs0 = vr_list_[i]->tex_->get_bricks();
			bs = vr_list_[i]->tex_->get_quota_bricks();
			bs->clear();

			for (j = 0; j < result->size(); j++)
			{
				ind = (*result)[j]->get_ind();
				bs->push_back((*bs0)[ind]);
			}
			vr_list_[i]->tex_->reset_sort_bricks();
		}

		return result;
	}

	void MultiVolumeRenderer::draw_wireframe(bool orthographic_p)
	{
		/*     if (get_vr_num()<=0)
				return;

			 Ray view_ray = vr_list_[0]->compute_view();

			 // Set sampling rate based on interaction.
			 double rate = imode_ ? irate_ : sampling_rate_;
			 Vector diag = bbox_.diagonal();
			 Vector cell_diag(diag.x()/res_.x(),
				   diag.y()/res_.y(),
				   diag.z()/res_.z());
			 double dt = cell_diag.length()/
				vr_list_[0]->compute_rate_scale()/rate;
			 num_slices_ = (int)(diag.length()/dt);

			 vector<double> vertex;
			 vector<double> texcoord;
			 vector<int> size;
			 vertex.reserve(num_slices_*6);
			 texcoord.reserve(num_slices_*6);
			 size.reserve(num_slices_*6);

			 //--------------------------------------------------------------------------
			 // render bricks
			 // Set up transform
			 Transform *tform = vr_list_[0]->tex_->transform();
			 double mvmat[16];
			 tform->get_trans(mvmat);
			 glMatrixMode(GL_MODELVIEW);
			 glPushMatrix();
			 glMultMatrixd(mvmat);

			 glEnable(GL_DEPTH_TEST);
			 GLboolean lighting = glIsEnabled(GL_LIGHTING);
			 glDisable(GL_LIGHTING);
			 glDisable(GL_TEXTURE_3D);
			 glDisable(GL_TEXTURE_2D);
			 glDisable(GL_FOG);

			 vector<TextureBrick*> *bs = vr_list_[0]->tex_->get_sorted_bricks(view_ray, orthographic_p);

			 if (bs)
			 {
				for (unsigned int i=0; i < bs->size(); i++)
				{
				   glColor4d(0.8*(i+1.0)/bs->size(), 0.8*(i+1.0)/bs->size(), 0.8, 1.0);

				   TextureBrick* b = (*bs)[i];
				   if (!vr_list_[0]->test_against_view(b->bbox())) continue; // Clip against view.

				   Point ptmin = b->bbox().min();
				   Point ptmax = b->bbox().max();
				   Point &pmin(ptmin);
				   Point &pmax(ptmax);
				   Point corner[8];
				   corner[0] = pmin;
				   corner[1] = Point(pmin.x(), pmin.y(), pmax.z());
				   corner[2] = Point(pmin.x(), pmax.y(), pmin.z());
				   corner[3] = Point(pmin.x(), pmax.y(), pmax.z());
				   corner[4] = Point(pmax.x(), pmin.y(), pmin.z());
				   corner[5] = Point(pmax.x(), pmin.y(), pmax.z());
				   corner[6] = Point(pmax.x(), pmax.y(), pmin.z());
				   corner[7] = pmax;

				   glBegin(GL_LINES);
				   {
					  for(int i=0; i<4; i++) {
						 glVertex3d(corner[i].x(), corner[i].y(), corner[i].z());
						 glVertex3d(corner[i+4].x(), corner[i+4].y(), corner[i+4].z());
					  }
				   }
				   glEnd();
				   glBegin(GL_LINE_LOOP);
				   {
					  glVertex3d(corner[0].x(), corner[0].y(), corner[0].z());
					  glVertex3d(corner[1].x(), corner[1].y(), corner[1].z());
					  glVertex3d(corner[3].x(), corner[3].y(), corner[3].z());
					  glVertex3d(corner[2].x(), corner[2].y(), corner[2].z());
				   }
				   glEnd();
				   glBegin(GL_LINE_LOOP);
				   {
					  glVertex3d(corner[4].x(), corner[4].y(), corner[4].z());
					  glVertex3d(corner[5].x(), corner[5].y(), corner[5].z());
					  glVertex3d(corner[7].x(), corner[7].y(), corner[7].z());
					  glVertex3d(corner[6].x(), corner[6].y(), corner[6].z());
				   }
				   glEnd();

				   glColor4d(0.4, 0.4, 0.4, 1.0);

				   vertex.clear();
				   texcoord.clear();
				   size.clear();

				   // Scale out dt such that the slices are artificially further apart.
				   b->compute_polygons(view_ray, dt * 10, vertex, texcoord, size);
				   vr_list_[0]->draw_polygons_wireframe(vertex, texcoord, size, false);
				}
			 }

			 if(lighting) glEnable(GL_LIGHTING);
			 glMatrixMode(GL_MODELVIEW);
			 glPopMatrix();
	   */
	}

	double MultiVolumeRenderer::num_slices_to_rate(int num_slices)
	{
		if (!bbox_.valid())
			return 1.0;
		Vector diag = bbox_.diagonal();
		Vector cell_diag(diag.x()/*/tex_->nx()*/,
			diag.y()/*/tex_->ny()*/,
			diag.z()/*/tex_->nz()*/);
		double dt = diag.length() / num_slices;
		double rate = cell_diag.length() / dt;

		return rate;
	}

	void MultiVolumeRenderer::resize()
	{

	}

} // namespace FLIVR
