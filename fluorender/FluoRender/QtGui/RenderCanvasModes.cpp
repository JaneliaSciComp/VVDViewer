// Volume blend modes, depth peeling and depth attenuation of the Qt render
// canvas.
//
// Port of the VRenderVulkanView (VRenderView.cpp) code behind the
// "Layered / Depth / Composite" radio buttons and the "Depth Int." column:
//   SetVolMethod / SetFog / SetFogIntensity / SetResMode
//   CalcFogRange                  -> RenderCanvas::CalcFogRange
//   DrawVolumes (setup part)      -> DrawVolumesByMethod
//   DrawVolumesComp               -> DrawVolumesComp
//   DrawOVER                      -> DrawVolumeOver (RenderCanvasStreaming.cpp)
//   DrawMIP                       -> DrawMIP
//   DrawVolumesMulti              -> DrawVolumesMulti (MultiVolumeRenderer)
//   DrawVolumesDP                 -> DrawVolumesDP (Depth mode with meshes)
//   PopVolumeList                 -> PopVolumeList
//
// How the modes differ (as in wx):
//   Layered   (VOL_METHOD_SEQ)   each volume rendered on its own and blended
//                                OVER the previous ones (first volume on top)
//   Composite (VOL_METHOD_COMP)  each volume rendered on its own and ADDed
//   Depth     (VOL_METHOD_MULTI) all volumes in one MultiVolumeRenderer pass
//                                (correct depth intermixing). With meshes,
//                                StartLoopUpdate sets m_dpeel and the frame
//                                goes through DrawVolumesDP: the mesh depth
//                                layers split the volume pass into slabs
//                                (peel 1/2/3) and the mesh colour layers are
//                                composited between them, so meshes and
//                                volumes occlude each other.
//
// Volumes are drawn per data group as in wx (RenderCanvasLayers.cpp
// DrawLayerVolumes: layers last to first, a group's volumes last to first,
// each group with its own blend mode). Volume shadows: DrawOLShadows (called
// from DrawOVER / DrawMIP / DrawVolumesMulti as in wx). Not ported: the
// shading pass DrawOLShading (wx calls it nowhere) and the 4D-script label
// masking of DrawVolumesComp.

#include "RenderCanvas.h"

#include <limits>
#include <algorithm>
#include <cmath>

#include <glm/gtc/type_ptr.hpp>

#include "VVulkan.h"
#include "Vulkan/Vulkan2dRender.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MultiVolumeRenderer.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/Transform.h"
#include "DataManager.h"
#include "compatibility.h"

using namespace FLIVR;

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

// wx VRenderVulkanView::SetVolMethod (+ OnVolumeMethodCheck's RefreshGL)
void RenderCanvas::SetVolMethod(int method)
{
	if (method != VolMethodSeq && method != VolMethodMulti && method != VolMethodComp)
		return;
	m_vol_method = method;
	// DrawOVER / DrawMIP final composition:
	// m_vol_method == VOL_METHOD_COMP ? V2DRENDER_BLEND_ADD : V2DRENDER_BLEND_OVER
	m_vol_method_blend = method == VolMethodComp ? V2DRENDER_BLEND_ADD : V2DRENDER_BLEND_OVER;
	Q_EMIT volMethodChanged(method);
	RequestRender();
}

void RenderCanvas::SetFog(bool b)
{
	m_use_fog = b;
	Q_EMIT fogChanged(m_use_fog, m_fog_intensity);
	RequestRender();
}

void RenderCanvas::SetFogIntensity(double i)
{
	m_fog_intensity = i;
	Q_EMIT fogChanged(m_use_fog, m_fog_intensity);
	RequestRender();
}

// wx VRenderView::SetResMode (combo has 6 entries)
void RenderCanvas::SetResMode(int mode)
{
	if (mode < 0 || mode > 5) return;
	m_res_mode = mode;
	Q_EMIT resModeChanged(mode);
	RequestRender();
}

// wx m_gamma / m_brightness / m_hdr of the view (AdjustPanel with the view
// selected; members and Get2DAdjust in RenderCanvasCamera.cpp)
void RenderCanvas::ViewAdjustment(Color& gamma, Color& brightness, Color& hdr) const
{
	gamma = m_gamma;
	brightness = m_brightness;
	hdr = m_hdr;
}

void RenderCanvas::ResizeMultiVolumeRenderer()
{
	if (m_mvr)
		m_mvr->resize();
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

// verbatim from VRenderVulkanView::CalcFogRange (m_mv_mat is the object
// model-view at this point, as in wx Draw())
void RenderCanvas::CalcFogRange()
{
	BBox bbox;
	bool use_box = false;
	if (m_cur_vol)
	{
		bbox = m_cur_vol->GetBounds();
		use_box = true;
	}
	else if (!m_md_pop_list.empty())
	{
		for (size_t i = 0; i < m_md_pop_list.size(); ++i)
		{
			if (m_md_pop_list[i]->GetDisp())
			{
				bbox.extend(m_md_pop_list[i]->GetBounds());
				use_box = true;
			}
		}
	}

	if (use_box)
	{
		Transform mv;
		mv.set(glm::value_ptr(m_mv_mat));

		double minz, maxz;
		minz = std::numeric_limits<double>::max();
		maxz = -std::numeric_limits<double>::max();

		std::vector<Point> points;
		points.push_back(Point(bbox.min().x(), bbox.min().y(), bbox.min().z()));
		points.push_back(Point(bbox.min().x(), bbox.min().y(), bbox.max().z()));
		points.push_back(Point(bbox.min().x(), bbox.max().y(), bbox.min().z()));
		points.push_back(Point(bbox.min().x(), bbox.max().y(), bbox.max().z()));
		points.push_back(Point(bbox.max().x(), bbox.min().y(), bbox.min().z()));
		points.push_back(Point(bbox.max().x(), bbox.min().y(), bbox.max().z()));
		points.push_back(Point(bbox.max().x(), bbox.max().y(), bbox.min().z()));
		points.push_back(Point(bbox.max().x(), bbox.max().y(), bbox.max().z()));

		Point p;
		for (size_t i = 0; i < points.size(); ++i)
		{
			p = mv.transform(points[i]);
			minz = p.z() < minz ? p.z() : minz;
			maxz = p.z() > maxz ? p.z() : maxz;
		}

		minz = std::fabs(minz);
		maxz = std::fabs(maxz);
		m_fog_start = minz < maxz ? minz : maxz;
		m_fog_end = maxz > minz ? maxz : minz;
	}
	else
	{
		m_fog_start = m_distance - m_radius / 2.0;
		m_fog_start = m_fog_start < 0.0 ? 0.0 : m_fog_start;
		m_fog_end = m_distance + m_radius / 4.0;
	}
}

// the depth-peeling layer buffers (colour + depth) of wx DrawVolumes /
// DrawVolumesDP; m_resize there == size change here
void RenderCanvas::EnsureMeshLayerBuffers(int count, int nx, int ny)
{
	vks::VulkanDevice* prim_dev = m_vulkan->devices[0];
	if (!m_dp_fbo_list.empty() && m_dp_fbo_list[0] &&
		(m_dp_fbo_list[0]->w != nx || m_dp_fbo_list[0]->h != ny))
	{
		m_dp_fbo_list.clear();
		m_dp_ctex_list.clear();
		m_dp_tex_list.clear();
		m_peeling_layer_progress = 0;
	}
	if ((int)m_dp_fbo_list.size() < count)
	{
		m_dp_fbo_list.resize(count);
		m_dp_ctex_list.resize(count);
		m_dp_tex_list.resize(count);
	}
	for (int i = 0; i < count; i++)
	{
		if (m_dp_fbo_list[i])
			continue;
		m_dp_fbo_list[i] = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_dp_fbo_list[i]->w = nx;
		m_dp_fbo_list[i]->h = ny;
		m_dp_fbo_list[i]->device = prim_dev;

		m_dp_ctex_list[i] = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_dp_fbo_list[i]->addAttachment(m_dp_ctex_list[i]);

		m_dp_tex_list[i] = prim_dev->GenTexture2D(
			m_vulkan->depthFormat,
			VK_FILTER_LINEAR,
			nx, ny,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		);
		m_dp_fbo_list[i]->addAttachment(m_dp_tex_list[i]);
	}
}

namespace
{
	// m_fbo_temp / m_fbo_temp_restore as created by wx DrawOVER /
	// DrawVolumesMulti / DrawVolumesDP (the restore target aliases the
	// final buffer's colour texture)
	void EnsureTempBuffers(vks::VulkanDevice* prim_dev, int nx, int ny,
		std::unique_ptr<vks::VFrameBuffer>& fbo_temp, std::shared_ptr<vks::VTexture>& tex_temp,
		std::unique_ptr<vks::VFrameBuffer>& fbo_temp_restore, const std::unique_ptr<vks::VFrameBuffer>& fbo_final)
	{
		if (fbo_temp && (fbo_temp->w != nx || fbo_temp->h != ny))
		{
			fbo_temp.reset();
			tex_temp.reset();
			fbo_temp_restore.reset();
		}
		if (fbo_temp_restore && fbo_temp_restore->attachments.size() > 0 &&
			fbo_temp_restore->attachments[0] != fbo_final->attachments[0])
		{
			fbo_temp.reset();
			tex_temp.reset();
			fbo_temp_restore.reset();
		}
		if (!fbo_temp)
		{
			fbo_temp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			fbo_temp->w = nx;
			fbo_temp->h = ny;
			fbo_temp->device = prim_dev;

			tex_temp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
			fbo_temp->addAttachment(tex_temp);

			fbo_temp_restore = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			fbo_temp_restore->w = nx;
			fbo_temp_restore->h = ny;
			fbo_temp_restore->device = prim_dev;
			fbo_temp_restore->addAttachment(fbo_final->attachments[0]);
		}
	}
}

// ---------------------------------------------------------------------------
// wx DrawVolumes, "handle intermixing modes"
// ---------------------------------------------------------------------------

void RenderCanvas::DrawVolumesByMethod(int peel)
{
	std::vector<VolumeData*> pop = PopVolumeList();
	if (pop.empty())
		return;
	bool dp = !m_md_pop_list.empty();

	if (m_vol_method == VolMethodMulti)
	{
		if (dp && !m_dp_tex_list.empty() && m_dp_tex_list[0])
		{
			m_vol_dp_bk_tex = m_dp_tex_list[0];
			DrawVolumesMulti(pop, 1);
		}
		else
			DrawVolumesMulti(pop, peel);

		//draw masks
		if (m_draw_mask)
			DrawVolumesComp(pop, true, peel);
	}
	else
		DrawLayerVolumes(peel, false);   // per group, RenderCanvasLayers.cpp
}

// wx DrawVolumesComp (no script-mode masking: the Qt port has no 4D script
// runner). mask == true is the selection pass: every displayed volume with a
// mask (and hide mode "none") is drawn again in mask mode 1 and ADDed, with
// the wx "Enhance selection" boost. The selected volume's own image goes to
// m_fbo_wt2 so that the brush's 2D weight (GetVolTex) excludes the mask pass.
// wx DrawOVER / DrawMIP: the textures of the shared label / mask volumes
void RenderCanvas::SharedMaskLabel(VolumeData* vd, Texture*& ext_msk, Texture*& ext_lbl) const
{
	ext_msk = nullptr;
	ext_lbl = nullptr;
	if (!vd || !m_volume_lookup)
		return;
	if (!vd->GetSharedLabelName().IsEmpty())
		if (VolumeData* lbl = m_volume_lookup(vd->GetSharedLabelName()))
			ext_lbl = lbl->GetTexture();
	if (!vd->GetSharedMaskName().IsEmpty())
		if (VolumeData* msk = m_volume_lookup(vd->GetSharedMaskName()))
			ext_msk = msk->GetTexture();
}

void RenderCanvas::EnsureWt2Buffer(int nx, int ny)
{
	if (m_fbo_wt2 && (m_fbo_wt2->w != nx || m_fbo_wt2->h != ny))
	{
		m_fbo_wt2.reset();
		m_tex_wt2.reset();
	}
	if (!m_fbo_wt2)
	{
		vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
		m_fbo_wt2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_wt2->w = nx;
		m_fbo_wt2->h = ny;
		m_fbo_wt2->device = prim_dev;

		m_tex_wt2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_wt2->addAttachment(m_tex_wt2);
	}
}

void RenderCanvas::DrawVolumesComp(const std::vector<VolumeData*>& list, bool mask, int peel)
{
	if (list.empty())
		return;

	//count volumes with mask
	int cnt_mask = 0;
	bool use_tex_wt2 = false;
	double sampling_frq_fac = 2 / std::min(m_ortho_right - m_ortho_left, m_ortho_top - m_ortho_bottom);
	for (VolumeData* vd : list)
	{
		if (!vd || !vd->GetDisp())
			continue;
		Texture* tex = vd->GetTexture();
		if (tex && (tex->nmask() != -1 || !vd->GetSharedMaskName().IsEmpty()) && vd->GetMaskHideMode() == VOL_MASK_HIDE_NONE)
		{
			cnt_mask++;
			if (vd == m_cur_vol && !mask)
				use_tex_wt2 = true;
		}
	}

	if (mask && cnt_mask == 0)
		return;

	// m_fbo: PrepBuffers
	if (use_tex_wt2)
		EnsureWt2Buffer(m_fbo->w, m_fbo->h);

	//draw each volume to fbo
	for (VolumeData* vd : list)
	{
		if (!vd || !vd->GetDisp() || !vd->GetVR())
			continue;
		if (mask)
		{
			if (vd->GetMaskHideMode() != VOL_MASK_HIDE_NONE)
				continue;
			if (vd->GetTexture() && (vd->GetTexture()->nmask() != -1 || !vd->GetSharedMaskName().IsEmpty()))
			{
				Color tmp_hdr;
				Color tmp_brightness;
				if (m_enhance_sel)
				{
					double mask_alpha = vd->GetMaskAlpha();
					Color mask_color = vd->GetMaskColor();
					double hdr_r = 0.0;
					double hdr_g = 0.0;
					double hdr_b = 0.0;
					if (mask_color.r() > 0.0)
						hdr_r = 0.4 * mask_alpha;
					if (mask_color.g() > 0.0)
						hdr_g = 0.4 * mask_alpha;
					if (mask_color.b() > 0.0)
						hdr_b = 0.4 * mask_alpha;
					Color hdr_color = Color(hdr_r, hdr_g, hdr_b);
					tmp_hdr = vd->GetHdr();
					vd->SetHdr(hdr_color);

					tmp_brightness = vd->GetBrightness();
					vd->SetBrightness(tmp_brightness * mask_alpha);
				}

				vd->SetMaskMode(1);
				int blend = m_vol_method_blend;
				m_vol_method_blend = V2DRENDER_BLEND_ADD;   // wx: m_vol_method = VOL_METHOD_COMP
				if (vd->GetMode() == 1)
					DrawMIP(vd, m_fbo, peel, sampling_frq_fac);
				else
					DrawVolumeOver(vd, m_fbo, sampling_frq_fac, peel);
				vd->SetMaskMode(0);
				m_vol_method_blend = blend;

				if (m_enhance_sel)
				{
					vd->SetHdr(tmp_hdr);
					vd->SetBrightness(tmp_brightness);
				}
			}
		}
		else
		{
			bool wt2_enable = vd == m_cur_vol && vd->GetTexture() && use_tex_wt2;
			if (vd->GetBlendMode() != 2)
			{
				if (vd->GetMode() == 1)
					DrawMIP(vd, wt2_enable ? m_fbo_wt2 : m_fbo, peel, sampling_frq_fac);
				else
					DrawVolumeOver(vd, wt2_enable ? m_fbo_wt2 : m_fbo, sampling_frq_fac, peel);
			}
		}
	}
}

// wx DrawMIP (the wx shading pass DrawOLShading is not called from DrawMIP
// either; volume shadows: DrawOLShadows)
void RenderCanvas::DrawMIP(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, int peel, double sampling_frq_fac)
{
	bool do_mip = true;
	const int nx = m_fbo_final->w, ny = m_fbo_final->h;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (TextureRenderer::get_mem_swap())
		EnsureTempBuffers(prim_dev, nx, ny, m_fbo_temp, m_tex_temp, m_fbo_temp_restore, m_fbo_final);

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		if (vd->GetVR()->get_done_loop(1) &&
			(!vd->isActiveMask() || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_MASK)) &&
			(!vd->isActiveLabel() || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_LABEL)))
			do_mip = false;

		if (!do_mip && !(vd->GetShadow() || !vd->GetVR()->get_done_loop(3)) && !(vd->GetShading() || !vd->GetVR()->get_done_loop(2)))
			return;

		if (do_mip)
		{
			//before rendering this channel, save m_fbo_final to m_fbo_temp
			if (TextureRenderer::get_mem_swap() &&
				TextureRenderer::get_start_update_loop() &&
				TextureRenderer::get_save_final_buffer())
			{
				TextureRenderer::reset_save_final_buffer();
				TextureRenderer::validate_temp_final_buffer();

				Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

				params.clear = true;
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP,
						V2DRENDER_BLEND_DISABLE,
						m_fbo_temp->attachments[0]->format,
						(int)m_fbo_temp->attachments.size(),
						0,
						m_fbo_temp->attachments[0]->is_swapchain_images);
				params.tex[0] = m_tex_final.get();

				if (!m_fbo_temp->renderPass)
					m_fbo_temp->replaceRenderPass(params.pipeline.pass);
				m_v2drender->render(m_fbo_temp, params);
			}
		}

		//time budget used up (a frame that has drawn nothing yet still draws one brick)
		if (TextureRenderer::frame_budget_exhausted())
			return;
	}

	int color_mode = vd->GetColormapMode();
	bool enable_alpha = vd->GetEnableAlpha();

	if (do_mip)
	{
		if (m_fbo_ol1 && (m_fbo_ol1->w != nx || m_fbo_ol1->h != ny))
		{
			m_fbo_ol1.reset();
			m_tex_ol1.reset();
		}
		if (!m_fbo_ol1)
		{
			m_fbo_ol1 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_fbo_ol1->w = nx;
			m_fbo_ol1->h = ny;
			m_fbo_ol1->device = prim_dev;

			m_tex_ol1 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
			m_fbo_ol1->addAttachment(m_tex_ol1);
		}

		//bind the fbo
		bool clear = false;
		if (!TextureRenderer::get_mem_swap() ||
			(TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_clear_chan_buffer()))
		{
			TextureRenderer::reset_clear_chan_buffer();
			clear = true;
		}

		vd->GetVR()->set_depth_peel(peel);
		if (color_mode == 1)
		{
			vd->SetMode(3);
			vd->SetFog(false, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
		}
		else
		{
			vd->SetMode(1);
			vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
		}

		VkClearColorValue clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };

		//draw
		vd->SetStreamMode(1);
		vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
		Texture* ext_msk = nullptr;
		Texture* ext_lbl = nullptr;
		SharedMaskLabel(vd, ext_msk, ext_lbl);
		vd->Draw(m_fbo_ol1, clear, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color, ext_msk, ext_lbl);
		//
		if (color_mode == 1)
		{
			vd->RestoreMode();
			//restore alpha
			vd->SetEnableAlpha(enable_alpha);
		}

		//bind fbo for final composition
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.clear = true;

		if (color_mode == 1)
		{
			//2d adjustment
			params.pipeline =
				m_v2drender->preparePipeline(
					IMG_SHDR_GRADIENT_MAP,
					V2DRENDER_BLEND_OVER,
					fb->attachments[0]->format,
					(int)fb->attachments.size(),
					vd->GetColormap(),
					fb->attachments[0]->is_swapchain_images);
			double lo, hi;
			vd->GetColormapValues(lo, hi);
			params.loc[0] = glm::vec4((float)lo, (float)hi, (float)(hi - lo), enable_alpha ? 0.0f : 1.0f);
		}
		else
		{
			params.pipeline =
				m_v2drender->preparePipeline(
					IMG_SHADER_TEXTURE_LOOKUP,
					V2DRENDER_BLEND_OVER,
					fb->attachments[0]->format,
					(int)fb->attachments.size(),
					0,
					fb->attachments[0]->is_swapchain_images);
		}
		params.tex[0] = m_tex_ol1.get();
		if (!fb->renderPass)
			fb->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(fb, params);
	}

	if (vd->GetShadow() && vd->GetVR()->get_done_loop(1))
		DrawOLShadows({ vd }, fb);

	//bind fbo for final composition
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && m_tex_temp && TextureRenderer::isvalid_temp_final_buffer())
	{
		//restore m_fbo_temp to m_fbo_final
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = m_clear_final_buffer;
		m_clear_final_buffer = false;

		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_final->attachments[0]->format,
				(int)m_fbo_final->attachments.size(),
				0,
				m_fbo_final->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_temp.get();

		if (!m_fbo_final->renderPass)
			m_fbo_final->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_final, params);
	}

	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	// wx: IMG_SHDR_LEVELS in easy 2D adjust mode (RenderCanvasCamera.cpp)
	glm::vec4 adjust_loc[3];
	const int adjust_shader = Get2DAdjust(vd, adjust_loc);
	params_final.pipeline =
		m_v2drender->preparePipeline(
			adjust_shader,
			m_vol_method_blend,
			m_fbo_final->attachments[0]->format,
			(int)m_fbo_final->attachments.size(),
			0,
			m_fbo_final->attachments[0]->is_swapchain_images);
	params_final.tex[0] = fb->attachments[0].get();
	params_final.loc[0] = adjust_loc[0];
	params_final.loc[1] = adjust_loc[1];
	params_final.loc[2] = adjust_loc[2];
	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);

	vd->SetColormapMode(color_mode);
}

// wx DrawVolumesMulti
void RenderCanvas::DrawVolumesMulti(const std::vector<VolumeData*>& list, int peel)
{
	if (list.empty())
		return;

	if (!m_mvr)
		m_mvr = std::make_shared<MultiVolumeRenderer>();
	if (!m_mvr)
		return;

	m_mvr->set_blend_slices(m_blend_slices);

	bool doMulti = false;
	bool use_tex_wt2 = false;
	bool shadow = false;
	bool doShadow = false;
	m_mvr->clear_vr();
	VolumeRenderer* vrfirst = nullptr;
	for (VolumeData* vd : list)
	{
		if (!vd || !vd->GetDisp())
			continue;
		shadow |= vd->GetShadow();
		VolumeRenderer* vr = vd->GetVR();
		if (vr)
		{
			vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
			vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
			m_mvr->add_vr(vr);
			if (!vrfirst) vrfirst = vr;
			m_mvr->set_sampling_rate(vr->get_sampling_rate());
			m_mvr->SetNoiseRed(vr->GetNoiseRed());

			if (TextureRenderer::get_mem_swap() &&
				TextureRenderer::get_start_update_loop() &&
				!TextureRenderer::get_done_update_loop())
			{
				doMulti |= !vr->get_done_loop(0);
				doShadow |= !vr->get_done_loop(3);
			}
		}
		if (vd->GetTexture() && vd->GetTexture()->nmask() != -1 && vd == m_cur_vol)
			use_tex_wt2 = true;
	}

	if (!TextureRenderer::get_mem_swap())
	{
		doMulti = true;
		doShadow = true;
	}

	if (m_mvr->get_vr_num() <= 0)
		return;

	const int nx = m_fbo_final->w, ny = m_fbo_final->h;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (TextureRenderer::get_mem_swap())
		EnsureTempBuffers(prim_dev, nx, ny, m_fbo_temp, m_tex_temp, m_fbo_temp_restore, m_fbo_final);

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		TextureRenderer::get_save_final_buffer())
	{
		TextureRenderer::reset_save_final_buffer();
		TextureRenderer::validate_temp_final_buffer();

		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = true;
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_temp->attachments[0]->format,
				(int)m_fbo_temp->attachments.size(),
				0,
				m_fbo_temp->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();

		if (!m_fbo_temp->renderPass)
			m_fbo_temp->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_temp, params);
	}

	if (!doMulti && !(shadow && doShadow))
		return;

	m_mvr->set_depth_peel(peel);
	if (peel > 0)
	{
		m_mvr->set_front_depth_tex(m_vol_dp_fr_tex);
		m_mvr->set_back_depth_tex(m_vol_dp_bk_tex);
	}

	// m_fbo: PrepBuffers
	if (use_tex_wt2)
		EnsureWt2Buffer(m_fbo->w, m_fbo->h);

	if (doMulti)
	{
		//bind the fbo
		bool clear = false;
		if (!TextureRenderer::get_mem_swap() ||
			(TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_clear_chan_buffer()))
		{
			clear = true;
			TextureRenderer::reset_clear_chan_buffer();
		}

		//draw multiple volumes at the same time
		double sampling_frq_fac = 2 / std::min(m_ortho_right - m_ortho_left, m_ortho_top - m_ortho_bottom);
		m_mvr->set_main_membuf_size((long long)TextureRenderer::get_mainmem_buf_size() * 1024LL * 1024LL);
		m_mvr->draw(use_tex_wt2 ? m_fbo_wt2 : m_fbo, clear, m_interactive, !m_persp, m_scale_factor, m_intp, sampling_frq_fac);
	}

	if (shadow && doShadow && vrfirst && vrfirst->get_done_loop(0))
	{
		//draw shadows
		DrawOLShadows(list, use_tex_wt2 ? m_fbo_wt2 : m_fbo);
	}

	//bind fbo for final composition
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && m_tex_temp && TextureRenderer::isvalid_temp_final_buffer())
	{
		//restore m_fbo_temp to m_fbo_final
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params.clear = m_clear_final_buffer;
		m_clear_final_buffer = false;

		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_temp_restore->attachments[0]->format,
				(int)m_fbo_temp_restore->attachments.size(),
				0,
				m_fbo_temp_restore->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_temp.get();

		if (!m_fbo_temp_restore->renderPass)
			m_fbo_temp_restore->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_temp_restore, params);
	}

	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

	int blend_method = V2DRENDER_BLEND_ADD;

	if (m_dpeel)
		blend_method = V2DRENDER_BLEND_OVER_INV;
	else
	{
		if (m_vol_method == VolMethodComp)
			blend_method = V2DRENDER_BLEND_ADD;
		else if (m_vol_method == VolMethodSeq)
			blend_method = V2DRENDER_BLEND_OVER;
	}

	// wx: the view's own 2D adjustment (m_gamma / m_brightness / m_hdr /
	// m_levels, edited by the Output Adjustments pane) in Depth mode, else
	// the first volume's; IMG_SHDR_LEVELS in easy mode (RenderCanvasCamera.cpp)
	glm::vec4 adjust_loc[3];
	const int adjust_shader = Get2DAdjust(m_vol_method == VolMethodMulti ? nullptr : list[0], adjust_loc);
	params_final.pipeline =
		m_v2drender->preparePipeline(
			adjust_shader,
			blend_method,
			m_fbo_final->attachments[0]->format,
			(int)m_fbo_final->attachments.size(),
			0,
			m_fbo_final->attachments[0]->is_swapchain_images);
	params_final.tex[0] = use_tex_wt2 ? m_fbo_wt2->attachments[0].get() : m_fbo->attachments[0].get();
	params_final.loc[0] = adjust_loc[0];
	params_final.loc[1] = adjust_loc[1];
	params_final.loc[2] = adjust_loc[2];

	params_final.clear = m_clear_final_buffer;
	m_clear_final_buffer = false;

	if (!m_fbo_final->renderPass)
		m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
	m_v2drender->render(m_fbo_final, params_final);
}

// ---------------------------------------------------------------------------
// wx DrawVolumesDP: Depth mode with meshes. The volume pass is split into
// peeling_layers + 1 slabs by the mesh depth layers; after each slab's brick
// loop completes the next mesh colour layer is composited behind it
// (OVER_INV) and the loop restarts for the next slab (StartLoopUpdate(false)).
// ---------------------------------------------------------------------------

void RenderCanvas::DrawVolumesDP(int nx, int ny)
{
	int finished_bricks = 0;
	if (TextureRenderer::get_mem_swap())
	{
		finished_bricks = TextureRenderer::get_finished_bricks();
		TextureRenderer::reset_finished_bricks();
	}
	(void)finished_bricks;

	int peeling_layers;
	if (!m_mdtrans || m_peeling_layers <= 0) peeling_layers = 0;
	else peeling_layers = m_peeling_layers;

	// PrepFinalBuffer: PrepBuffers (DrawScene)

	if (TextureRenderer::get_mem_swap())
	{
		//set start time for the texture renderer
		TextureRenderer::set_st_time(GET_TICK_COUNT());

		TextureRenderer::set_interactive(m_interactive);
	}

	//draw (the Qt canvas is always in the viewing mode, wx m_int_mode 1)
	{
		m_updating = false;

		for (int s = m_finished_peeling_layer; s <= peeling_layers; s++)
		{
			if (s == 0 || (TextureRenderer::get_mem_swap() && TextureRenderer::get_total_brick_num() > 0))
				m_clear_final_buffer = true;   // ClearFinalBuffer()

			int dptexnum = peeling_layers > 0 ? peeling_layers : 1;
			if (m_finished_peeling_layer == 0 && TextureRenderer::get_cur_brick_num() == 0 && m_md_pop_list.size() > 0)
			{
				EnsureMeshLayerBuffers(dptexnum, nx, ny);

				for (int i = m_peeling_layer_progress; i < dptexnum; i++)
				{
					bool finished = false;
					if (i == 0)
						finished = DrawMeshes(m_dp_fbo_list[i], true, 0);
					else
						finished = DrawMeshes(m_dp_fbo_list[i], true, 1, m_dp_tex_list[i - 1]);

					if (finished)
						m_peeling_layer_progress = i + 1;
					else
						break;
				}
			}

			if (m_peeling_layer_progress < dptexnum)
				break;

			int peel = 0;
			if ((int)m_dp_tex_list.size() >= peeling_layers && !m_dp_tex_list.empty() && m_md_pop_list.size() > 0)
			{
				if (peeling_layers == 0)
				{
					// opaque meshes: one slab in front of the mesh surface.
					// (wx with update order 0 indexes m_dp_tex_list[-1] here)
					peel = 1;
					m_vol_dp_bk_tex = m_dp_tex_list[0];
				}
				else if (TextureRenderer::get_update_order() == 0)
				{
					if (s == 0)
					{
						peel = 2;
						m_vol_dp_fr_tex = m_dp_tex_list[(peeling_layers - 1) - s];
					}
					else if (s == peeling_layers)
					{
						peel = 1;
						m_vol_dp_bk_tex = m_dp_tex_list[(peeling_layers - 1) - (s - 1)];
					}
					else
					{
						peel = 3;
						m_vol_dp_fr_tex = m_dp_tex_list[(peeling_layers - 1) - s];
						m_vol_dp_bk_tex = m_dp_tex_list[(peeling_layers - 1) - (s - 1)];
					}
				}
				else
				{
					if (s == 0)
					{
						peel = 1;
						m_vol_dp_bk_tex = m_dp_tex_list[s];
					}
					else if (s == peeling_layers)
					{
						peel = 2;
						m_vol_dp_fr_tex = m_dp_tex_list[s - 1];
					}
					else
					{
						peel = 3;
						m_vol_dp_fr_tex = m_dp_tex_list[s - 1];
						m_vol_dp_bk_tex = m_dp_tex_list[s];
					}
				}
			}

			//setup
			std::vector<VolumeData*> pop = PopVolumeList();

			//handle intermixing modes
			if (m_vol_method == VolMethodMulti)
			{
				DrawVolumesMulti(pop, peel);
				//draw masks
				if (m_draw_mask)
					DrawVolumesComp(pop, true, peel);
			}
			else
				DrawLayerVolumes(peel, true);

			EnsureTempBuffers(m_vulkan->vulkanDevice, nx, ny, m_fbo_temp, m_tex_temp, m_fbo_temp_restore, m_fbo_final);

			int texid = TextureRenderer::get_update_order() == 0 ? (peeling_layers - 1) - s : s;
			if (peeling_layers == 0)
				texid = s;
			if (texid >= 0 && (int)m_dp_ctex_list.size() > texid && m_md_pop_list.size() > 0 &&
				((TextureRenderer::get_start_update_loop() && TextureRenderer::get_done_update_loop()) || !TextureRenderer::get_mem_swap() || TextureRenderer::get_total_brick_num() == 0))
			{
				Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

				params.clear = m_clear_final_buffer;
				m_clear_final_buffer = false;
				params.pipeline =
					m_v2drender->preparePipeline(
						IMG_SHADER_TEXTURE_LOOKUP_BLEND,
						V2DRENDER_BLEND_OVER_INV,
						m_fbo_temp_restore->attachments[0]->format,
						(int)m_fbo_temp_restore->attachments.size(),
						0,
						m_fbo_temp_restore->attachments[0]->is_swapchain_images);
				params.tex[0] = m_dp_ctex_list[texid].get();

				if (!m_fbo_temp_restore->renderPass)
					m_fbo_temp_restore->replaceRenderPass(params.pipeline.pass);
				m_v2drender->render(m_fbo_temp_restore, params);
			}

			if (TextureRenderer::get_mem_swap())
			{
				unsigned int rn_time = GET_TICK_COUNT();
				TextureRenderer::set_consumed_time(rn_time - TextureRenderer::get_st_time());

				if (TextureRenderer::get_total_brick_num() == 0 && m_peeling_layer_progress >= dptexnum)
					TextureRenderer::set_done_update_loop(true);

				if (TextureRenderer::get_start_update_loop() && !TextureRenderer::get_done_update_loop())
					break;

				if (TextureRenderer::get_start_update_loop() && TextureRenderer::get_done_update_loop())
				{
					m_finished_peeling_layer++;
					if (m_finished_peeling_layer <= peeling_layers)
					{
						// The next slab is drawn in this same call with the frame's
						// matrices. wx StartLoopUpdate leaves m_mv_mat as the full
						// (camera + object) matrix it computed; the Qt one restores
						// the camera-only matrix at its end, which put the volume of
						// every slab after the first at the wrong place (the slabs
						// behind / inside a transparent mesh came out empty).
						const glm::mat4 frame_mv = m_mv_mat, frame_proj = m_proj_mat;
						StartLoopUpdate(false);
						m_mv_mat = frame_mv;
						m_proj_mat = frame_proj;
						finished_bricks = TextureRenderer::get_finished_bricks();
						TextureRenderer::reset_finished_bricks();

						if (TextureRenderer::get_mem_swap() &&
							TextureRenderer::get_start_update_loop())
						{
							TextureRenderer::validate_temp_final_buffer();

							Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();

							params.clear = true;
							params.pipeline =
								m_v2drender->preparePipeline(
									IMG_SHADER_TEXTURE_LOOKUP,
									V2DRENDER_BLEND_DISABLE,
									m_fbo_temp->attachments[0]->format,
									(int)m_fbo_temp->attachments.size(),
									0,
									m_fbo_temp->attachments[0]->is_swapchain_images);
							params.tex[0] = m_tex_final.get();

							if (!m_fbo_temp->renderPass)
								m_fbo_temp->replaceRenderPass(params.pipeline.pass);
							m_v2drender->render(m_fbo_temp, params);
						}
					}
					else
						TextureRenderer::reset_update_loop();
				}

				if (rn_time - TextureRenderer::get_st_time() > TextureRenderer::get_up_time())
					break;
			}
			else
				m_finished_peeling_layer++;
		}
	}

	//draw the final buffer to the windows buffer (while the pass is still
	//running, with the mesh layers of the unfinished slabs behind it)
	vks::VTexture* present = nullptr;
	if (TextureRenderer::get_mem_swap() && TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop() && !m_tile_rendering)
		present = DrawDPMeshPreview(nx, ny, peeling_layers);
	DrawFinalBuffer(true, present);

	// still interacting: stay at the reduced rate; the settle timer
	// (RenderCanvas::OnInteractionSettleTimer) starts the full-quality pass
	const bool int_settled = InteractionSettled();
	if (m_interactive && int_settled)
	{
		m_interactive = false;
		m_refresh_start_loop = true;
	}

	if (m_int_res && int_settled)
	{
		m_int_res = false;
		m_refresh_start_loop = true;
	}

	// wx: the animated camera move steps on in the next frame (PreDraw)
	if (m_manip)
	{
		m_pre_draw = true;
		m_refresh_start_loop = true;
	}
}

// Mid-pass frames of DrawVolumesDP: wx composites a slab's mesh colour layer
// into the final buffer only when that slab's brick loop is done, so while
// bricks stream the presented frame has no meshes in front of / inside the
// unfinished slabs. The presented image is a copy of the final buffer with the
// mesh layers of the remaining slabs composited behind it (OVER_INV, in slab
// order, exactly as the loop above will add them); the final buffer and the
// loop state are untouched, so the completed pass is unchanged.
vks::VTexture* RenderCanvas::DrawDPMeshPreview(int nx, int ny, int peeling_layers)
{
	const int dptexnum = peeling_layers > 0 ? peeling_layers : 1;
	if (m_md_pop_list.empty() || m_peeling_layer_progress < dptexnum || !m_fbo_final || !m_tex_final)
		return nullptr;
	std::vector<int> texids;
	for (int s = m_finished_peeling_layer; s <= peeling_layers; s++)
	{
		int texid = TextureRenderer::get_update_order() == 0 ? (peeling_layers - 1) - s : s;
		if (peeling_layers == 0)
			texid = s;
		if (texid >= 0 && texid < (int)m_dp_ctex_list.size() && m_dp_ctex_list[texid])
			texids.push_back(texid);
	}
	if (texids.empty())
		return nullptr;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (m_fbo_dp_preview && (m_fbo_dp_preview->w != nx || m_fbo_dp_preview->h != ny))
	{
		m_fbo_dp_preview.reset();
		m_tex_dp_preview.reset();
	}
	if (!m_fbo_dp_preview)
	{
		m_fbo_dp_preview = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_dp_preview->w = nx;
		m_fbo_dp_preview->h = ny;
		m_fbo_dp_preview->device = prim_dev;
		m_tex_dp_preview = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_dp_preview->addAttachment(m_tex_dp_preview);
	}

	{
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.clear = true;
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_dp_preview->attachments[0]->format,
				(int)m_fbo_dp_preview->attachments.size(),
				0,
				m_fbo_dp_preview->attachments[0]->is_swapchain_images);
		params.tex[0] = m_tex_final.get();
		if (!m_fbo_dp_preview->renderPass)
			m_fbo_dp_preview->replaceRenderPass(params.pipeline.pass);
		m_v2drender->render(m_fbo_dp_preview, params);
	}
	for (int texid : texids)
	{
		Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
		params.clear = false;
		params.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHADER_TEXTURE_LOOKUP_BLEND,
				V2DRENDER_BLEND_OVER_INV,
				m_fbo_dp_preview->attachments[0]->format,
				(int)m_fbo_dp_preview->attachments.size(),
				0,
				m_fbo_dp_preview->attachments[0]->is_swapchain_images);
		params.tex[0] = m_dp_ctex_list[texid].get();
		m_v2drender->render(m_fbo_dp_preview, params);
	}
	return m_tex_dp_preview.get();
}

// copy of the final buffer into m_fbo_temp (wx DrawOVER's "save m_fbo_final
// to m_fbo_temp"); see RenderCanvas.h
void RenderCanvas::SaveFinalToTemp(int nx, int ny)
{
	if (!m_fbo_final || !m_tex_final)
		return;
	EnsureTempBuffers(m_vulkan->vulkanDevice, nx, ny, m_fbo_temp, m_tex_temp, m_fbo_temp_restore, m_fbo_final);

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params.clear = true;
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHADER_TEXTURE_LOOKUP,
			V2DRENDER_BLEND_DISABLE,
			m_fbo_temp->attachments[0]->format,
			(int)m_fbo_temp->attachments.size(),
			0,
			m_fbo_temp->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_final.get();
	if (!m_fbo_temp->renderPass)
		m_fbo_temp->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(m_fbo_temp, params);
}

// ---------------------------------------------------------------------------
// wx DrawOLShadows: the depth map of the shadow-casting volumes (colormap
// mode 2, stream mode 3) into m_fbo_ol1, its gradient (IMG_SHDR_DEPTH_TO_
// GRADIENT, offset by the Settings shadow direction) into m_fbo_ol2, which
// darkens fb (IMG_SHDR_GRADIENT_TO_SHADOW, V2DRENDER_BLEND_SHADE_SHADOW) by the
// first volume's shadow intensity. While streaming the depth pass is its own
// brick loop (TEXTURE_RENDER_MODE 3; StartLoopUpdate counts the bricks) and
// fb is darkened when it is complete. Verbatim, including the wx use of
// vlist[0] for the "shadow loop done" test.
// ---------------------------------------------------------------------------

void RenderCanvas::DrawOLShadows(const std::vector<VolumeData*>& vlist, std::unique_ptr<vks::VFrameBuffer>& fb)
{
	if (vlist.empty() || !fb)
		return;

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop())
	{
		if (TextureRenderer::get_done_update_loop())
			return;
		else
		{
			bool doShadow = false;
			for (size_t i = 0; i < vlist.size(); i++)
			{
				if (vlist[0]->GetVR())
					doShadow |= !(vlist[0]->GetVR()->get_done_loop(3));
			}
			if (!doShadow)
				return;
		}

		if (TextureRenderer::get_save_final_buffer())
			TextureRenderer::reset_save_final_buffer();
	}

	double sampling_frq_fac = 2 / std::min(m_ortho_right - m_ortho_left, m_ortho_top - m_ortho_bottom);

	// wx m_nx / m_ny
	const int nx = fb->w, ny = fb->h;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;

	//gradient pass
	if (m_fbo_ol1 && (m_fbo_ol1->w != nx || m_fbo_ol1->h != ny))
	{
		m_fbo_ol1.reset();
		m_tex_ol1.reset();
	}
	if (!m_fbo_ol1)
	{
		m_fbo_ol1 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_ol1->w = nx;
		m_fbo_ol1->h = ny;
		m_fbo_ol1->device = prim_dev;

		m_tex_ol1 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_ol1->addAttachment(m_tex_ol1);
	}

	bool clear = false;
	if (!TextureRenderer::get_mem_swap() ||
		(TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_clear_chan_buffer()))
	{
		TextureRenderer::reset_clear_chan_buffer();
		clear = true;
	}

	double shadow_darkness = 0.0;
	VkClearColorValue clear_color = { 1.0f, 1.0f, 1.0f, 1.0f };

	if (vlist.size() == 1 && vlist[0]->GetShadow())
	{
		VolumeData* vd = vlist[0];

		if (vd->GetTexture() && vd->GetVR())
		{
			//save
			int colormode = vd->GetColormapMode();
			bool shading = vd->GetVR()->get_shading();
			//set to draw depth
			vd->GetVR()->set_shading(false);
			vd->SetMode(0);
			vd->SetColormapMode(2);
			vd->Set2dDmap(m_tex_ol1);
			//draw
			vd->SetStreamMode(3);
			// no depth peeling in the depth-map pass: the VRay shader cannot
			// combine both (it declares dmap_ray / dmap_st twice and fails to
			// compile; wx draws nothing then and its brick loop never ends)
			const int peel = vd->GetVR()->get_depth_peel();
			vd->GetVR()->set_depth_peel(0);
			int uporder = TextureRenderer::get_update_order();
			TextureRenderer::set_update_order(0);
			vd->GetTexture()->set_sort_bricks();
			vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
			vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
			vd->Draw(m_fbo_ol1, clear, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color);
			//restore
			vd->RestoreMode();
			vd->SetColormapMode(colormode);
			vd->GetVR()->set_shading(shading);
			vd->GetShadowParams(shadow_darkness);
			TextureRenderer::set_update_order(uporder);
			vd->GetTexture()->set_sort_bricks();
			vd->GetVR()->set_depth_peel(peel);
		}
	}
	else
	{
		std::vector<int> colormodes;
		std::vector<bool> shadings;
		std::vector<VolumeData*> list;

		int uporder = TextureRenderer::get_update_order();
		TextureRenderer::set_update_order(0);

		//generate list
		for (VolumeData* vd : vlist)
		{
			if (vd && vd->GetShadow() && vd->GetTexture() && vd->GetVR())
			{
				colormodes.push_back(vd->GetColormapMode());
				shadings.push_back(vd->GetVR()->get_shading());
				list.push_back(vd);
			}
		}
		if (!list.empty())
		{
			if (!m_mvr)
				m_mvr = std::make_shared<MultiVolumeRenderer>();
			m_mvr->clear_vr();
			m_mvr->set_main_membuf_size((long long)TextureRenderer::get_mainmem_buf_size() * 1024LL * 1024LL);
			for (VolumeData* vd : list)
			{
				vd->GetVR()->set_shading(false);
				vd->SetMode(0);
				vd->SetColormapMode(2);
				vd->Set2dDmap(m_tex_ol1);
				vd->GetTexture()->set_sort_bricks();
				VolumeRenderer* vr = vd->GetVR();
				vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
				vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
				m_mvr->add_vr(vr);
				m_mvr->set_sampling_rate(vr->get_sampling_rate());
				m_mvr->SetNoiseRed(vr->GetNoiseRed());
			}
			m_mvr->set_colormap_mode(2);
			// see above: no depth peeling in the depth-map pass (wx keeps the
			// peel of the Depth-mode pass with meshes here: compile error)
			const int peel = m_mvr->get_depth_peel();
			m_mvr->set_depth_peel(0);
			//draw
			m_mvr->draw(m_fbo_ol1, clear, m_interactive, !m_persp, m_scale_factor, m_intp, sampling_frq_fac, clear_color);
			m_mvr->set_depth_peel(peel);
			//restore
			m_mvr->set_colormap_mode(0);
			for (size_t i = 0; i < list.size(); i++)
			{
				VolumeData* vd = list[i];
				vd->RestoreMode();
				vd->SetColormapMode(colormodes[i]);
				vd->GetVR()->set_shading(shadings[i]);
				vd->GetTexture()->set_sort_bricks();
			}
			list[0]->GetShadowParams(shadow_darkness);
		}

		TextureRenderer::set_update_order(uporder);
	}

	//
	if (!TextureRenderer::get_mem_swap() ||
		(TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_clear_chan_buffer()))
	{
		if (m_fbo_ol2 && (m_fbo_ol2->w != nx || m_fbo_ol2->h != ny))
		{
			m_fbo_ol2.reset();
			m_tex_ol2.reset();
		}
		if (!m_fbo_ol2)
		{
			m_fbo_ol2 = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
			m_fbo_ol2->w = nx;
			m_fbo_ol2->h = ny;
			m_fbo_ol2->device = prim_dev;

			m_tex_ol2 = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
			m_fbo_ol2->addAttachment(m_tex_ol2);
		}

		//bind the fbo
		Vulkan2dRender::V2DRenderParams params_ol2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params_ol2.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_DEPTH_TO_GRADIENT,
				V2DRENDER_BLEND_DISABLE,
				m_fbo_ol2->attachments[0]->format,
				(int)m_fbo_ol2->attachments.size(),
				0,
				m_fbo_ol2->attachments[0]->is_swapchain_images);
		params_ol2.tex[0] = m_tex_ol1.get();

		// SettingDlg GetShadowDir
		const double dir_x = m_shadow_dir_x, dir_y = m_shadow_dir_y;

		params_ol2.loc[0] = { 1.0f / nx, 1.0f / ny, m_persp ? 2e10f : 1e6f, 0.0f };
		params_ol2.loc[1] = { (float)dir_x, (float)dir_y, 0.0f, 0.0f };

		params_ol2.clear = true;
		params_ol2.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };

		m_fbo_ol2->replaceRenderPass(params_ol2.pipeline.pass);
		m_v2drender->render(m_fbo_ol2, params_ol2);

		//bind fbo for final composition
		Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		params_final.pipeline =
			m_v2drender->preparePipeline(
				IMG_SHDR_GRADIENT_TO_SHADOW,
				V2DRENDER_BLEND_SHADE_SHADOW,
				fb->attachments[0]->format,
				(int)fb->attachments.size(),
				0,
				fb->attachments[0]->is_swapchain_images);
		params_final.tex[0] = m_tex_ol2.get();
		params_final.tex[1] = fb->attachments[0].get();	// as wx (reads the target; see the TODO there)
		params_final.loc[0] = { 1.0f / nx, 1.0f / ny, (float)std::max(m_scale_factor, 1.0), 0.0f };
		params_final.loc[1] = { (float)shadow_darkness, 0.0f, 0.0f, 0.0f };

		if (!fb->renderPass)
			fb->replaceRenderPass(params_final.pipeline.pass);
		m_v2drender->render(fb, params_final);
	}
}
