// Memory swap / streaming update loop of the Qt render canvas.
//
// Port of the VRenderVulkanView machinery (VRenderView.cpp) that renders a
// volume a few bricks at a time and accumulates the result over several
// frames:
//   StartLoopUpdate  -> RenderCanvas::StartLoopUpdate (brick sorting / drawn
//                       flags / per-volume brick counts, loader + empty
//                       block detector queues, set_update_loop ...)
//   Draw()           -> StreamingBeginFrame (restart the loop when the last
//                       one is done) + DrawScene's overlays-only branch
//   DrawVolumes()    -> DrawScene (composite mode, no tiles)
//   DrawOVER()       -> DrawVolumeOver (m_fbo_temp save/restore of the final
//                       buffer, per-channel accumulation buffer)
//   OnIdle/RefreshGL -> StreamingIdle / RequestRender
//   RefreshGLOverlays-> RefreshOverlays
//   switchLevel / SetSortBricks / HaltLoopUpdate: verbatim.
//
// Note that TextureRenderer::mem_swap_ is always true in this engine; the
// settings' "mem swap" toggles TextureRenderer::streaming_ (the per-frame
// time budget). Without streaming the loop completes in one frame.

#include "RenderCanvas.h"

#include <QWindow>
#include <thread>
#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "VVulkan.h"
#include "Vulkan/Vulkan2dRender.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/TextureBrick.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/Transform.h"
#include "Core/Thread.h"
#include "DataManager.h"
#include <QDateTime>
#include "compatibility.h"
#include "utility.h"

using namespace FLIVR;

namespace
{
	int CpuCount()
	{
		unsigned n = std::thread::hardware_concurrency();
		return n > 0 ? (int)n : 1;
	}

	// wx: VRenderFrame::ms_criticalSection, handed to TextureRenderer,
	// VolumeLoader and EmptyBlockDetector at start-up
	vvd::CriticalSection& StreamingCriticalSection()
	{
		static vvd::CriticalSection cs;
		return cs;
	}
}

// ---------------------------------------------------------------------------
// set-up / tear-down
// ---------------------------------------------------------------------------

void RenderCanvas::InitStreaming()
{
	if (m_streaming_init) return;
	m_streaming_init = true;
	// wx VRenderFrame ctor: TextureRenderer::setCriticalSection and
	// VRenderVulkanView::setCriticalSection (VolumeLoader + EmptyBlockDetector)
	vvd::CriticalSection* cs = &StreamingCriticalSection();
	TextureRenderer::setCriticalSection(cs);
	VolumeLoader::setCriticalSection(cs);
	EmptyBlockDetector::setCriticalSection(cs);
	if (!m_loader) m_loader = std::make_unique<VolumeLoader>();
	if (!m_ebd) m_ebd = std::make_unique<EmptyBlockDetector>();
}

void RenderCanvas::ShutdownStreaming()
{
	// wx ~VRenderVulkanView
	m_deferred_load.reset();
	if (m_loader) m_loader->StopAll();
	if (m_ebd)
	{
		m_ebd->ClearQueues();
		m_ebd->Join();
	}
	m_fbo_temp.reset();
	m_tex_temp.reset();
	m_fbo_temp_restore.reset();
}

// Testing aid: VVD_QT_STREAM_DEBUG=1 prints, once per second, the frames
// drawn, the bricks rendered and the brick loop progress.
namespace
{
	bool s_loop_timing = false;
	unsigned long s_loop_t0 = 0;
	int s_loop_frames = 0;
	long long s_loop_bricks = 0;
}

void RenderCanvas::StreamingDebugFrame()
{
	static const bool on = qEnvironmentVariableIsSet("VVD_QT_STREAM_DEBUG");
	if (!on) return;
	static unsigned long t0 = GET_TICK_COUNT(), last = t0;
	static int frames = 0;
	static long long bricks = 0;
	frames++;
	const int fb = TextureRenderer::get_finished_bricks();
	bricks += fb;
	TextureRenderer::set_finished_bricks(0);   // overlay-only frames do not reset it
	unsigned long now = GET_TICK_COUNT();
	// one line per completed brick loop: wall time from the frame that
	// started it (StartLoopUpdate) to the frame that finished it
	if (s_loop_timing)
	{
		s_loop_frames++;
		s_loop_bricks += fb;
		if (TextureRenderer::get_done_update_loop() || !TextureRenderer::get_start_update_loop())
		{
			fprintf(stderr, "[stream] loop done: %.3f s, %d frames, %lld bricks drawn, %d bricks total (t=%.1fs)\n",
				(now - s_loop_t0) / 1000.0, s_loop_frames, s_loop_bricks, TextureRenderer::get_total_brick_num(), (now - t0) / 1000.0);
			s_loop_timing = false;
		}
	}
	if (now - last >= 1000)
	{
		fprintf(stderr, "[stream] t=%.1fs frames=%d bricks=%lld cur=%d total=%d start=%d done=%d avail_mem=%.0f/%.0f pool=%zu\n",
			(now - t0) / 1000.0, frames, bricks, TextureRenderer::get_cur_brick_num(), TextureRenderer::get_total_brick_num(),
			(int)TextureRenderer::get_start_update_loop(), (int)TextureRenderer::get_done_update_loop(),
			m_vulkan ? m_vulkan->vulkanDevice->available_mem : -1.0, m_vulkan ? m_vulkan->vulkanDevice->mem_limit : -1.0, m_vulkan ? m_vulkan->vulkanDevice->tex_pool.size() : 0);
		if (m_vulkan)
		{
			long long* w = m_vulkan->vulkanDevice->m_texwait_stats;
			fprintf(stderr, "[stream]   texture reuse waits: none=%lld frame=%lld drain=%lld\n", w[0], w[1], w[2]);
		}
		frames = 0; bricks = 0; last = now;
	}
}

bool RenderCanvas::IsUpdateLoopDone() const
{
	if (!TextureRenderer::get_mem_swap()) return true;
	// another canvas's loop state is in the statics: this canvas's own copy
	if (s_loop_owner && s_loop_owner != this)
		return m_loop_state.done_update_loop;
	return TextureRenderer::get_done_update_loop();
}

// ---------------------------------------------------------------------------
// refresh entry points (wx RefreshGL / RefreshGLOverlays / OnIdle)
// ---------------------------------------------------------------------------

// wx RefreshGL(erase, start_loop = true). The StartLoopUpdate call is
// deferred to the next frame (StreamingBeginFrame) so that a burst of
// camera/property changes between two frames re-sorts the bricks once.
void RenderCanvas::RequestRender()
{
	if (m_shutdown) return;
	m_draw_overlays_only = false;
	m_updating = true;
	m_start_loop_pending = true;
	SetSortBricks();
	// QWindow::requestUpdate coalesces like QWidget::update did.
	RequestFrame();
}

// wx RefreshGL(false, false)
void RenderCanvas::RefreshNoLoopRestart()
{
	if (m_shutdown) return;
	m_draw_overlays_only = false;
	m_updating = true;
	SetSortBricks();
	RequestFrame();
}

// wx RefreshGLOverlays
void RenderCanvas::RefreshOverlays()
{
	if (m_shutdown) return;
	if (m_updating) return;
	if (!IsUpdateLoopDone()) return;
	m_draw_overlays_only = true;
	SetSortBricks();
	RequestFrame();
}

void RenderCanvas::RefreshOverlayChange()
{
	if (m_shutdown) return;
	if (!IsUpdateLoopDone())
		RequestFrame();
	else if (!m_updating && m_frame_presented && m_fbo_final)
		RefreshOverlays();
	else
		RequestRender();
}

// the loader queue StartLoopUpdate kept while the view was interactive
void RenderCanvas::StartDeferredLoad()
{
	if (!m_deferred_load || !m_loader)
		return;
	// only for the loop that built the queue, while it runs
	if (!(TextureRenderer::get_mem_swap() && TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop()))
	{
		m_deferred_load.reset();
		return;
	}
	// the input has paused for about one wx idle frame (its 50 ms timer)
	if (m_interactive && m_last_interaction_ms >= 0 &&
		QDateTime::currentMSecsSinceEpoch() - m_last_interaction_ms < 50)
		return;
	// the queue holds raw pointers: only volumes the view still has
	std::vector<VolumeLoaderData> q;
	for (const VolumeLoaderData& d : *m_deferred_load)
		if (d.vd && std::find(m_volumes.begin(), m_volumes.end(), d.vd) != m_volumes.end())
			q.push_back(d);
	m_deferred_load.reset();
	bool runvl = false;
	for (const VolumeLoaderData& d : q)
	{
		if ((d.brick && !d.brick->isLoaded() && !d.brick->isLoading()) ||
			(d.vd && !d.vd->isBrxml()))
		{
			runvl = true;
			break;
		}
	}
	if (!runvl)
		return;
	m_loader->Set(q);
	m_loader->SetMemoryLimitByte((long long)TextureRenderer::get_mainmem_buf_size() * 1024LL * 1024LL);
	TextureRenderer::set_load_on_main_thread(false);
	const int cpus = CpuCount();
	m_loader->SetMaxThreadNum(std::max(1, (cpus - 1) - (m_ebd ? m_ebd->GetMaxThreadNum() : 0)));
	m_loader->Run();
}

// wx OnIdle (the memory swap part), run after every presented frame
void RenderCanvas::StreamingIdle()
{
	// end of wx Draw()
	m_draw_overlays_only = false;
	if (m_shutdown) return;
	StartDeferredLoad();

	bool refresh = m_refresh_start_loop;
	bool start_loop = true;

	//check memory swap status
	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		refresh = true;
		start_loop = m_refresh_start_loop;
	}
	m_refresh_start_loop = false;

	// wx OnIdle: m_capture_rotat / m_capture_tsequ / m_capture_param /
	// m_test_speed (Settings speed test mode) redraw on every idle
	if (m_test_speed)
		refresh = true;

	if (refresh)
	{
		if (start_loop)
			RequestRender();
		else
			RefreshNoLoopRestart();
	}
}

//halt loop update
void RenderCanvas::HaltLoopUpdate()
{
	if (TextureRenderer::get_mem_swap())
	{
		TextureRenderer::reset_update_loop();
	}
}

void RenderCanvas::SetSortBricks()
{
	for (VolumeData* vd : PopVolumeList())   // wx: m_vd_pop_list
	{
		if (vd && vd->GetDisp() && vd->GetTexture())
			vd->GetTexture()->set_sort_bricks();
	}
}

// ---------------------------------------------------------------------------
// frame
// ---------------------------------------------------------------------------

// wx Draw(): "if (get_done_update_loop() && !m_draw_overlays_only) StartLoopUpdate();"
// plus the StartLoopUpdate that wx RefreshGL(.., true) runs immediately.
void RenderCanvas::StreamingBeginFrame()
{
	if (!m_streaming_init) InitStreaming();
	// A frame nobody asked to redraw (expose / display-link / RenderNow
	// while the scene is unchanged) re-presents the retained final buffer
	// with fresh overlays, like wx RefreshGLOverlays. wx only repaints on
	// RefreshGL, but the Qt window delivers expose-driven frames
	// continuously, and restarting the brick loop on each of them would
	// clear the accumulated image of a streamed volume over and over.
	if (!m_start_loop_pending && !m_updating && TextureRenderer::get_done_update_loop() &&
		m_fbo_final && m_fbo_final->w == RenderSize().width() && m_fbo_final->h == RenderSize().height())
		m_draw_overlays_only = true;
	bool start = m_start_loop_pending;
	m_start_loop_pending = false;
	if (TextureRenderer::get_done_update_loop() && !m_draw_overlays_only)
		start = true;
	if (start)
		StartLoopUpdate();
}

// wx Draw() (volume part) + DrawVolumes() in composite mode
void RenderCanvas::DrawScene(int nx, int ny)
{
	PrepBuffers(nx, ny);

	// wx Draw(): overlays only -> present the retained final buffer
	if (TextureRenderer::get_done_update_loop() && m_draw_overlays_only)
	{
		DrawFinalBuffer(true);
		return;
	}

	// wx Draw(): if (m_use_fog) CalcFogRange();
	if (m_use_fog)
		CalcFogRange();

	// wx Draw(): else if (!m_dpeel) DrawVolumes(); else DrawVolumesDP();
	// (Depth mode with meshes: volumes and mesh layers interleaved by depth,
	// RenderCanvasModes.cpp; it presents the final buffer itself)
	if (m_dpeel)
	{
		DrawVolumesDP(nx, ny);
		return;
	}

	int finished_bricks = 0;
	if (TextureRenderer::get_mem_swap())
	{
		finished_bricks = TextureRenderer::get_finished_bricks();
		TextureRenderer::reset_finished_bricks();
	}
	(void)finished_bricks;

	// wx: (!m_drawing_coord && m_int_mode != 7 && m_updating) || m_int_mode == 1 ...
	// The Qt canvas is always in the viewing mode (tools draw overlays only),
	// so the scene is redrawn on every frame like wx m_int_mode 1.
	{
		m_updating = false;

		if (TextureRenderer::get_mem_swap())
		{
			//set start time for the texture renderer
			TextureRenderer::set_st_time(GET_TICK_COUNT());

			TextureRenderer::set_interactive(m_interactive);
		}

		m_clear_final_buffer = true;   // wx ClearFinalBuffer()

		if (TextureRenderer::get_cur_brick_num() == 0 && !m_md_pop_list.empty())
		{
			DrawMeshLayers(nx, ny);

			// The meshes are drawn only in the first frame of a pass; the
			// later frames clear the final buffer and composite the volume
			// channels again, so (as in wx) a pass that needs more than one
			// frame lost its meshes. Keep this mesh image as the base the
			// first channel is restored onto (m_fbo_temp); it is validated
			// after this frame's volumes so this frame is drawn unchanged.
			m_mesh_base_saved = false;
			if (TextureRenderer::get_mem_swap() && TextureRenderer::get_start_update_loop() &&
				!TextureRenderer::get_done_update_loop() && TextureRenderer::get_total_brick_num() > 0)
			{
				SaveFinalToTemp(nx, ny);
				m_mesh_base_saved = true;
			}

			int peeling_layers = m_mdtrans ? m_peeling_layers : 1;
			if (TextureRenderer::get_total_brick_num() == 0 && TextureRenderer::get_mem_swap() &&
				m_finished_peeling_layer >= peeling_layers)
			{
				TextureRenderer::set_done_update_loop(true);
			}
		}

		DrawVolumes(nx, ny);

		// the pass goes on in the next frame: from there on the first channel
		// starts from the mesh image (a channel that has already saved the
		// final buffer itself has overwritten m_fbo_temp, which is then valid)
		if (m_mesh_base_saved)
		{
			m_mesh_base_saved = false;
			if (TextureRenderer::get_start_update_loop() && !TextureRenderer::get_done_update_loop())
				TextureRenderer::validate_temp_final_buffer();
		}
	}

	if (m_clear_final_buffer && m_fbo_final)
	{
		Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();

		if (!m_fbo_final->renderPass)
		{
			params_final.pipeline =
				m_v2drender->preparePipeline(
					IMG_SHDR_BRIGHTNESS_CONTRAST_HDR,
					V2DRENDER_BLEND_ADD,
					m_fbo_final->attachments[0]->format,
					(int)m_fbo_final->attachments.size(),
					0,
					m_fbo_final->attachments[0]->is_swapchain_images);
			m_fbo_final->replaceRenderPass(params_final.pipeline.pass);
		}
		params_final.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_clear_final_buffer = false;
		m_v2drender->clear(m_fbo_final, params_final);
	}

	//draw the final buffer to the windows buffer
	DrawFinalBuffer(true);

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

	if (TextureRenderer::get_mem_swap())
	{
		TextureRenderer::set_consumed_time(GET_TICK_COUNT() - TextureRenderer::get_st_time());
		if (TextureRenderer::get_start_update_loop() &&
			TextureRenderer::get_done_update_loop())
		{
			TextureRenderer::reset_update_loop();
			// a capture reads this frame back right after the loop completes;
			// SaveRecordedFrame waits for the queue
		}
	}
}

// wx DrawOVER (+ DrawOLShadows). The final-composition blend m_vol_method_blend is ADD in
// Composite mode and OVER otherwise (SetVolMethod).
void RenderCanvas::DrawVolumeOver(VolumeData* vd, std::unique_ptr<vks::VFrameBuffer>& fb, double sampling_frq_fac, int peel)
{
	bool do_over = true;
	const int nx = m_fbo_final->w, ny = m_fbo_final->h;

	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (TextureRenderer::get_mem_swap() && m_fbo_temp && (m_fbo_temp->w != nx || m_fbo_temp->h != ny))
	{
		m_fbo_temp.reset();
		m_tex_temp.reset();
		m_fbo_temp_restore.reset();
	}
	// the restore target aliases the final buffer's texture: rebuild it when
	// the final buffer was recreated
	if (m_fbo_temp_restore && m_fbo_temp_restore->attachments.size() > 0 &&
		m_fbo_temp_restore->attachments[0] != m_fbo_final->attachments[0])
	{
		m_fbo_temp.reset();
		m_tex_temp.reset();
		m_fbo_temp_restore.reset();
	}
	if (TextureRenderer::get_mem_swap() && !m_fbo_temp)
	{
		m_fbo_temp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp->w = nx;
		m_fbo_temp->h = ny;
		m_fbo_temp->device = prim_dev;

		m_tex_temp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		m_fbo_temp->addAttachment(m_tex_temp);

		m_fbo_temp_restore = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		m_fbo_temp_restore->w = nx;
		m_fbo_temp_restore->h = ny;
		m_fbo_temp_restore->device = prim_dev;
		m_fbo_temp_restore->addAttachment(m_fbo_final->attachments[0]);
	}

	if (TextureRenderer::get_mem_swap() &&
		TextureRenderer::get_start_update_loop() &&
		!TextureRenderer::get_done_update_loop())
	{
		if (vd->GetVR()->get_done_loop(0) &&
			(!vd->isActiveMask() || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_MASK)) &&
			(!vd->isActiveLabel() || vd->GetVR()->get_done_loop(TEXTURE_RENDER_MODE_LABEL)))
			do_over = false;

		if (!do_over && !(vd->GetShadow() || !vd->GetVR()->get_done_loop(3)))
			return;

		if (do_over)
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

	if (do_over)
	{
		bool clear = false;
		if (!TextureRenderer::get_mem_swap() ||
			(TextureRenderer::get_mem_swap() &&
			TextureRenderer::get_clear_chan_buffer()))
		{
			//the draw returns at once when the frame's time is used up: keep
			//the flag for the frame that draws (a pass that took it and drew
			//nothing left its buffer uncleared, e.g. a stale shadow map)
			if (!TextureRenderer::frame_budget_exhausted())
				TextureRenderer::reset_clear_chan_buffer();
			clear = true;
		}

		vd->GetVR()->set_depth_peel(peel);
		vd->GetVR()->set_depth_textures(peel ? m_vol_dp_fr_tex : nullptr, peel ? m_vol_dp_bk_tex : nullptr);
		vd->SetStreamMode(0);
		vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);
		vd->SetFog(m_use_fog, m_fog_intensity, m_fog_start, m_fog_end, m_bg_color);
		VkClearColorValue clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
		Texture* ext_msk = nullptr;
		Texture* ext_lbl = nullptr;
		SharedMaskLabel(vd, ext_msk, ext_lbl);
		vd->Draw(fb, clear, !m_persp, m_interactive, m_scale_factor, sampling_frq_fac, clear_color, ext_msk, ext_lbl);
	}

	if (vd->GetShadow() && vd->GetVR()->get_done_loop(0))
		DrawOLShadows({ vd }, fb);   // RenderCanvasModes.cpp

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

	// per-volume brightness/contrast/hdr, accumulated into the final buffer
	Vulkan2dRender::V2DRenderParams params_final = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	// gamma/brightness/hdr, or gamma/levels in easy 2D adjust mode
	// (RenderCanvasCamera.cpp Get2DAdjust)
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
}

// ---------------------------------------------------------------------------
// pyramid level (verbatim from VRenderVulkanView::switchLevel)
// ---------------------------------------------------------------------------

void RenderCanvas::switchLevel(VolumeData* vd)
{
	if (!vd) return;

	QSize s = RenderSize();   // wx m_nx/m_ny
	int nx = std::max(1, s.width());
	int ny = std::max(1, s.height());

	Texture* vtex = vd->GetTexture();
	if (vtex && vtex->isBrxml())
	{
		int prev_lv = vd->GetLevel();
		int new_lv = 0;

		if (m_res_mode == 5)
			new_lv = vtex->GetLevelNum() - 1;

		if (m_res_mode > 0 && m_res_mode < 5)
		{
			double res_scale = 1.0;
			switch (m_res_mode)
			{
			case 1:
				res_scale = 1;
				break;
			case 2:
				res_scale = 1.5;
				break;
			case 3:
				res_scale = 2.0;
				break;
			case 4:
				res_scale = 3.0;
				break;
			default:
				res_scale = 1.0;
			}
			std::vector<double> sfs;
			int lvnum = vtex->GetLevelNum();
			for (int i = 0; i < lvnum; i++)
			{
				double aspect = (double)nx / (double)ny;

				double spc_x;
				double spc_y;
				double spc_z;
				vtex->get_spacings(spc_x, spc_y, spc_z, i);
				spc_x = spc_x < EPS ? 1.0 : spc_x;
				spc_y = spc_y < EPS ? 1.0 : spc_y;

				double sf;
				if (aspect > 1.0)
					sf = 2.0 * m_radius * res_scale / spc_y / double(ny);
				else
					sf = 2.0 * m_radius * res_scale / spc_x / double(nx);
				sfs.push_back(sf);
			}

			int lv = lvnum - 1;
			// the coarsest level during an animated camera move (StartManipulation)
			if (!m_manip)
			{
				for (int i = lvnum - 1; i >= 0; i--)
				{
					if (m_scale_factor > (m_int_res ? sfs[i] * 16.0 : sfs[i])) lv = i - 1;
				}
			}
			if (lv < 0) lv = 0;
			if (lv >= lvnum) lv = lvnum - 1;
			new_lv = lv;

			vd->GetVR()->set_buffer_scale(1.0);
		}
		if (prev_lv != new_lv)
		{
			std::vector<TextureBrick*>* bricks = vtex->get_bricks();
			if (bricks)
			{
				for (size_t i = 0; i < bricks->size(); i++)
					(*bricks)[i]->set_disp(false);
			}
			vd->SetLevel(new_lv);
			vtex->set_sort_bricks();
		}
	}

	if (vtex && !vtex->isBrxml())
		vd->GetVR()->set_buffer_scale(1.0);
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::StartLoopUpdate (layer list as in wx: Depth-blend
// groups switch the frame to DrawVolumesDP when meshes are shown). Volume shadows are not drawn by the Qt
// canvas, so their bricks are not counted (wx counts them because DrawOVER
// draws DrawOLShadows); masks are counted with m_draw_mask (the selection
// pass, on unless V is held) and labels with m_draw_label, as in wx.
// ---------------------------------------------------------------------------

void RenderCanvas::StartLoopUpdate(bool reset_peeling_layer)
{
	if (!m_vulkan) return;
	if (!m_streaming_init) InitStreaming();

	if (reset_peeling_layer)
	{
		m_finished_peeling_layer = 0;
		m_peeling_layer_progress = 0;
	}

	SetSortBricks();

	QSize s = RenderSize();   // wx m_nx/m_ny (tile size while tile rendering)
	int nx = std::max(1, s.width());
	int ny = std::max(1, s.height());

	//projection
	HandleProjection(nx, ny);
	//Transformation
	HandleCamera();

	glm::mat4 mv_temp = m_mv_mat;
	//translate object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	m_mv_mat = glm::rotate(m_mv_mat, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	//center object
	m_mv_mat = glm::translate(m_mv_mat, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	PopMeshList();
	// wx SetBufferScale(m_res_mode): m_res_scale is not read anywhere else

	size_t i, j, k;
	m_dpeel = false;
	m_mdtrans = false;
	for (i = 0; i < m_md_pop_list.size(); i++)
	{
		MeshData* md = m_md_pop_list[i];
		if (!md) continue;
		Color amb, diff, spec;
		double shine, alpha;
		md->GetMaterial(amb, diff, spec, shine, alpha);
		if (alpha < 1.0) m_mdtrans = true;
	}

	int total_num = 0;
	int num_chan;
	//reset drawn status for all bricks

	std::vector<EmptyBlockDetectorQueue> ebd_queues;

	// wx: the displayed volumes of the displayed Depth-blend (MULTI) groups
	// that have bricks, layers walked from last to first
	std::vector<VolumeData*> displist;
	for (int li = (int)m_layer_list.size() - 1; li >= 0; li--)
	{
		TreeLayer* layer = m_layer_list[li];
		if (!layer || layer->IsA() != 5)
			continue;
		DataGroup* group = (DataGroup*)layer;
		if (!group->GetDisp() || group->GetBlendMode() != VolMethodMulti)
			continue;
		for (int gj = group->GetVolumeNum() - 1; gj >= 0; gj--)
		{
			VolumeData* vd = group->GetVolumeData(gj);
			if (!vd || !vd->GetDisp())
				continue;
			Texture* tex = vd->GetTexture();
			if (!tex || !vd->GetVR())
				continue;
			Ray view_ray = vd->GetVR()->compute_view();
			std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks(view_ray, !m_persp);
			if (!bricks || bricks->size() == 0)
				continue;
			displist.push_back(vd);
		}
	}
	// wx m_vd_pop_list: displayed volumes in layer order
	std::vector<VolumeData*> poplist = PopVolumeList();
	// wx: m_dpeel = !(displist.empty() && m_vol_method != VOL_METHOD_MULTI);
	// (the Qt canvas additionally needs a displayed volume in the Depth view
	// method, as before)
	m_dpeel = !displist.empty() || (m_vol_method == VolMethodMulti && !poplist.empty());
	if (m_md_pop_list.size() == 0)
		m_dpeel = false;
	displist = poplist;

	// wx PopVolumeList -> ToggleQualityDropDownBox(isbrxml)
	{
		bool isbrxml = false;
		for (VolumeData* vd : displist)
			if (vd->isBrxml()) isbrxml = true;
		if (isbrxml != m_has_brxml)
		{
			m_has_brxml = isbrxml;
			Q_EMIT pyramidVolumeShown(isbrxml);
		}
	}

	for (i = 0; i < displist.size(); i++)
	{
		VolumeData* vd = displist[i];
		if (!vd || !vd->GetDisp())
			continue;
#ifdef _DARWIN
		// wx: slice mode while the clipping pane (one volume shown, or "Sync
		// All Chan.") has a slab of at most 3 voxels (GetXdist/GetYdist/GetZdist)
		{
			bool slice_mode = false;
			const ClipSlabInfo cs = m_clip_slab ? m_clip_slab() : ClipSlabInfo();
			if (cs.valid && (displist.size() == 1 || cs.chann_link) &&
				(cs.dist[0] <= 3 || cs.dist[1] <= 3 || cs.dist[2] <= 3))
				slice_mode = true;
			if (slice_mode && vd->GetVR())
				vd->GetVR()->set_slice_mode(true);
			else if (vd->GetVR())
				vd->GetVR()->set_slice_mode(false);
		}
#endif

		switchLevel(vd);
		vd->SetInterpolate(m_intp);   // wx: the view's "Intrp." (SetIntp)
		if (!TextureRenderer::get_mem_swap()) continue;

		vd->SetMatrices(m_mv_mat, m_proj_mat, m_tex_mat);

		num_chan = 0;
		Texture* tex = vd->GetTexture();
		if (tex && vd->GetVR())
		{
			std::shared_ptr<VL_Nrrd> msk_nrrd;
			if (m_draw_mask && tex->isBrxml() && (vd->GetTexture()->nmask() >= 0 || !vd->GetSharedMaskName().IsEmpty()))
			{
				int curlv = vd->GetLevel();
				vd->SetLevel(vd->GetMaskLv());
				msk_nrrd = tex->get_nrrd(tex->nmask());
				Transform* tform = tex->transform();
				double mvmat2[16];
				tform->get_trans(mvmat2);
				vd->GetVR()->m_mv_mat2 = glm::mat4(
					mvmat2[0], mvmat2[4], mvmat2[8], mvmat2[3],
					mvmat2[1], mvmat2[5], mvmat2[9], mvmat2[7],
					mvmat2[2], mvmat2[6], mvmat2[10], mvmat2[11],
					mvmat2[12], mvmat2[13], mvmat2[14], mvmat2[15]);
				vd->GetVR()->m_mv_mat2 = vd->GetVR()->m_mv_mat * vd->GetVR()->m_mv_mat2;

				Ray view_ray = vd->GetVR()->compute_view();

				std::vector<TextureBrick*>* bricks2 = tex->get_sorted_bricks(view_ray, !m_persp);
				if (!bricks2 || bricks2->size() == 0)
				{
					vd->SetLevel(curlv);
					continue;
				}
				//"Skip empty bricks": the transfer-function test once per volume
				const int skip_max = vd->GetVR()->invisible_brick_max((*bricks2)[0]->nb(0));
				for (j = 0; j < bricks2->size(); j++)
				{
					(*bricks2)[j]->set_drawn(false);
					if (vd->GetVR()->skip_empty_brick((*bricks2)[j], skip_max) ||
						!vd->GetVR()->test_against_view_clip((*bricks2)[j]->bbox(), (*bricks2)[j]->tbox(), (*bricks2)[j]->dbox(), m_persp) ||
						!tex->GetFileName((*bricks2)[j]->getID()) ||
						(!tex->GetFileName((*bricks2)[j]->getID())->isvalid && !tex->GetFileName((*bricks2)[j]->getID())->isurl))
					{
						(*bricks2)[j]->set_disp(false);
						continue;
					}
					else
					{
						(*bricks2)[j]->set_disp(true);
						if (tex->GetFileName((*bricks2)[j]->getID()) && !tex->GetFileName((*bricks2)[j]->getID())->isvalid && tex->GetFileName((*bricks2)[j]->getID())->isurl)
							tex->GetFileName((*bricks2)[j]->getID())->isvalid = true;
					}
					if (m_draw_mask && tex->nmask() != -1 && vd->GetMaskHideMode() == VOL_MASK_HIDE_NONE)
					{
						total_num++;
						num_chan++;
						if ((*bricks2)[j]->modified(tex->nmask()))
						{
							EmptyBlockDetectorQueue eq((*bricks2)[j], tex->nmask(), tex->get_nrrd(tex->nmask()));
							if (curlv != vd->GetMaskLv())
								eq.msknrrd = (*bricks2)[j]->get_nrrd(tex->nmask());
							ebd_queues.push_back(eq);
						}
					}
					if (m_draw_label && tex->nlabel() != -1)
					{
						total_num++;
						num_chan++;
					}
				}
				vd->SetLevel(curlv);
			}

			Transform* tform = tex->transform();
			double mvmat[16];
			tform->get_trans(mvmat);
			vd->GetVR()->m_mv_mat2 = glm::mat4(
				mvmat[0], mvmat[4], mvmat[8], mvmat[3],
				mvmat[1], mvmat[5], mvmat[9], mvmat[7],
				mvmat[2], mvmat[6], mvmat[10], mvmat[11],
				mvmat[12], mvmat[13], mvmat[14], mvmat[15]);
			vd->GetVR()->m_mv_mat2 = vd->GetVR()->m_mv_mat * vd->GetVR()->m_mv_mat2;

			Ray view_ray = vd->GetVR()->compute_view();

			std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks(view_ray, !m_persp);
			if (!bricks || bricks->size() == 0)
				continue;
			//"Skip empty bricks": the transfer-function test once per volume
			const int skip_max = vd->GetVR()->invisible_brick_max((*bricks)[0]->nb(0));
			for (j = 0; j < bricks->size(); j++)
			{
				(*bricks)[j]->set_drawn(false);
				if (vd->GetVR()->skip_empty_brick((*bricks)[j], skip_max) ||
					!vd->GetVR()->test_against_view_clip((*bricks)[j]->bbox(), (*bricks)[j]->tbox(), (*bricks)[j]->dbox(), m_persp) ||
					(tex->isBrxml() && (!tex->GetFileName((*bricks)[j]->getID()) || (!tex->GetFileName((*bricks)[j]->getID())->isvalid && !tex->GetFileName((*bricks)[j]->getID())->isurl))))
				{
					(*bricks)[j]->set_disp(false);
					continue;
				}
				else
				{
					(*bricks)[j]->set_disp(true);
					if (tex->isBrxml() && tex->GetFileName((*bricks)[j]->getID()) && !tex->GetFileName((*bricks)[j]->getID())->isvalid && tex->GetFileName((*bricks)[j]->getID())->isurl)
						tex->GetFileName((*bricks)[j]->getID())->isvalid = true;
				}
				total_num++;
				num_chan++;
				if (vd->GetShadow())
				{
					total_num++;
					num_chan++;
				}
				if (m_draw_mask && (tex->nmask() != -1 || !vd->GetSharedMaskName().IsEmpty()))
				{
					if (!tex->isBrxml() && vd->GetMaskHideMode() == VOL_MASK_HIDE_NONE)
					{
						total_num++;
						num_chan++;
					}
					if ((*bricks)[j]->modified(tex->nmask()))
					{
						EmptyBlockDetectorQueue eq((*bricks)[j], tex->nmask(), tex->get_nrrd(0), msk_nrrd);
						ebd_queues.push_back(eq);
					}
				}
				if (m_draw_label && tex->nlabel() != -1 && !tex->isBrxml())
				{
					total_num++;
					num_chan++;
				}
			}
		}
		vd->SetBrickNum(num_chan);
		if (vd->GetVR())
			vd->GetVR()->set_done_loop(false);
	}

	// loader queues
	std::vector<VolumeLoaderData> queues;
	if (m_vol_method == VolMethodMulti)
	{
		// wx: m_vol_method == VOL_METHOD_MULTI -- bricks of all channels by
		// distance from the view plane
		std::vector<VolumeData*> list;
		for (i = 0; i < displist.size(); i++)
		{
			VolumeData* vd = displist[i];
			if (!vd || !vd->GetDisp() || !vd->isBrxml())
				continue;
			Texture* tex = vd->GetTexture();
			if (!tex || !vd->GetVR())
				continue;
			std::vector<TextureBrick*>* bricks = tex->get_bricks();
			if (!bricks || bricks->size() == 0)
				continue;
			list.push_back(vd);
		}

		std::vector<VolumeLoaderData> tmp_shadow;
		for (i = 0; i < list.size(); i++)
		{
			VolumeData* vd = list[i];
			Texture* tex = vd->GetTexture();
			Ray view_ray = vd->GetVR()->compute_view();
			tex->set_sort_bricks();
			std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks_dir(view_ray); // distance from view plane
			int mode = vd->GetMode() == 1 ? 1 : 0;
			bool shadow = vd->GetShadow();
			for (j = 0; bricks && j < bricks->size(); j++)
			{
				VolumeLoaderData d = {};
				TextureBrick* b = (*bricks)[j];
				if (b->get_disp())
				{
					d.brick = b;
					d.finfo = tex->GetFileName(b->getID());
					d.vd = vd;
					if (!b->drawn(mode))
					{
						d.mode = mode;
						queues.push_back(d);
					}
					if (shadow && !b->drawn(3))
					{
						d.mode = 3;
						tmp_shadow.push_back(d);
					}
				}
			}
		}
		if (TextureRenderer::get_update_order() == 1)
			std::sort(queues.begin(), queues.end(), VolumeLoader::sort_data_dsc);
		else if (TextureRenderer::get_update_order() == 0)
			std::sort(queues.begin(), queues.end(), VolumeLoader::sort_data_asc);

		if (!tmp_shadow.empty())
		{
			if (TextureRenderer::get_update_order() == 1)
			{
				int order = TextureRenderer::get_update_order();
				TextureRenderer::set_update_order(0);
				for (i = 0; i < list.size(); i++)
				{
					Ray view_ray = list[i]->GetVR()->compute_view();
					list[i]->GetTexture()->set_sort_bricks();
					list[i]->GetTexture()->get_sorted_bricks_dir(view_ray); // distance from view plane
					list[i]->GetTexture()->set_sort_bricks();
				}
				TextureRenderer::set_update_order(order);
				std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
			}
			else if (TextureRenderer::get_update_order() == 0)
				std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
			queues.insert(queues.end(), tmp_shadow.begin(), tmp_shadow.end());
		}

		//for mask
		if (m_draw_mask)
		{
			for (i = 0; i < list.size(); i++)
			{
				VolumeData* vd = list[i];
				if (vd->GetTexture()->nmask() < 0 && vd->GetSharedMaskName().IsEmpty()) continue;
				int curlevel = vd->GetLevel();
				vd->SetLevel(vd->GetMaskLv());
				Texture* tex = vd->GetTexture();
				if (tex->nmask() < 0) { vd->SetLevel(curlevel); continue; }
				Ray view_ray = vd->GetVR()->compute_view();
				std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks(view_ray, !m_persp);
				int mode = TEXTURE_RENDER_MODE_MASK;
				for (j = 0; bricks && j < bricks->size(); j++)
				{
					VolumeLoaderData d = {};
					TextureBrick* b = (*bricks)[j];
					if (b->get_disp())
					{
						d.brick = b;
						d.finfo = tex->GetFileName(b->getID());
						d.vd = vd;
						if (!b->drawn(mode))
						{
							d.mode = mode;
							queues.push_back(d);
						}
					}
				}
				vd->SetLevel(curlevel);
			}
		}
	}
	// the wx group branch (Layered / Composite views): Depth-blend groups
	// queue their bricks on every loop start, including the slab restarts of
	// DrawVolumesDP (StartLoopUpdate(false)); the other volumes only when the
	// peeling layers reset (wx: "if (!reset_peeling_layer) continue")
	else
	{
		// wx walks the group from its last volume to its first (draw order)
		std::vector<VolumeData*> list;
		for (auto it = displist.rbegin(); it != displist.rend(); ++it)
		{
			VolumeData* vd = *it;
			if (!vd || !vd->GetDisp() || !vd->isBrxml())
				continue;
			if (!reset_peeling_layer)
			{
				DataGroup* g = GetParentGroup(vd);
				if (!g || g->GetBlendMode() != VolMethodMulti)
					continue;
			}
			Texture* tex = vd->GetTexture();
			if (!tex || !vd->GetVR())
				continue;
			Ray view_ray = vd->GetVR()->compute_view();
			std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks(view_ray, !m_persp);
			if (!bricks || bricks->size() == 0)
				continue;
			list.push_back(vd);
		}

		std::vector<VolumeLoaderData> tmp_shadow;
		for (j = 0; j < list.size(); j++)
		{
			VolumeData* vd = list[j];
			Texture* tex = vd->GetTexture();
			Ray view_ray = vd->GetVR()->compute_view();
			tex->set_sort_bricks();
			std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks(view_ray, !m_persp);
			int mode = vd->GetMode() == 1 ? 1 : 0;
			bool shadow = vd->GetShadow();
			for (k = 0; k < bricks->size(); k++)
			{
				VolumeLoaderData d = {};
				TextureBrick* b = (*bricks)[k];
				if (b->get_disp())
				{
					d.brick = b;
					d.finfo = tex->GetFileName(b->getID());
					d.vd = vd;
					if (!b->drawn(mode))
					{
						d.mode = mode;
						queues.push_back(d);
					}
					if (shadow && !b->drawn(3))
					{
						d.mode = 3;
						tmp_shadow.push_back(d);
					}
				}
			}
			if (!tmp_shadow.empty())
			{
				if (TextureRenderer::get_update_order() == 1)
					std::sort(tmp_shadow.begin(), tmp_shadow.end(), VolumeLoader::sort_data_asc);
				queues.insert(queues.end(), tmp_shadow.begin(), tmp_shadow.end());
				tmp_shadow.clear();
			}
		}

		//for mask
		if (m_draw_mask)
		{
			for (j = 0; j < list.size(); j++)
			{
				VolumeData* vd = list[j];
				if (vd->GetTexture()->nmask() < 0 && vd->GetSharedMaskName().IsEmpty()) continue;
				int curlevel = vd->GetLevel();
				vd->SetLevel(vd->GetMaskLv());
				Texture* tex = vd->GetTexture();
				Ray view_ray = vd->GetVR()->compute_view();
				std::vector<TextureBrick*>* bricks = tex->get_sorted_bricks(view_ray, !m_persp);
				int mode = TEXTURE_RENDER_MODE_MASK;
				for (k = 0; bricks && k < bricks->size(); k++)
				{
					VolumeLoaderData d = {};
					TextureBrick* b = (*bricks)[k];
					if (b->get_disp())
					{
						d.brick = b;
						d.finfo = tex->GetFileName(b->getID());
						d.vd = vd;
						if (!b->drawn(mode))
						{
							d.mode = mode;
							queues.push_back(d);
						}
					}
				}
				vd->SetLevel(curlevel);
			}
		}
	}

	// 4D: prefetch the following time points of non-brxml series
	{
		std::vector<VolumeData*> vd_4d_list;
		for (VolumeData* vd : displist)
		{
			if (vd && vd->GetDisp() && !vd->isBrxml() && vd->GetReader() &&
				vd->GetReader()->GetCurTime() != vd->GetReader()->GetTimeNum())
				vd_4d_list.push_back(vd);
		}

		int toffset = 1;
		while (!vd_4d_list.empty())
		{
			bool remain = false;
			for (VolumeData* vd : vd_4d_list)
			{
				if (vd->GetCurTime() + toffset < vd->GetReader()->GetTimeNum())
				{
					VolumeLoaderData d = {};
					d.chid = vd->GetCurChannel();
					d.frameid = vd->GetCurTime() + toffset;
					d.vd = vd;
					d.datasize = vd->GetDataSize();
					d.brick = NULL;
					queues.push_back(d);
					remain = true;
				}
			}
			if (!remain) break;
			toffset++;
		}
	}

	if (!m_ebd_run)
		ebd_queues.clear();

	bool runvl = false;
	m_deferred_load.reset();
	if (queues.size() > 0 && m_interactive)
	{
		// interactive: wx starts no loader either, but its view leaves the
		// interactive state after one frame and restarts, so pyramid bricks
		// load in the pauses between drag events. The Qt view stays interactive
		// until the input has been still for IntSettleMs: StreamingIdle starts
		// this queue once the input pauses (StartDeferredLoad)
		m_deferred_load = std::make_unique<std::vector<VolumeLoaderData>>(queues);
	}
	else if (queues.size() > 0)
	{
		for (auto& q : queues)
		{
			if ((q.brick && !q.brick->isLoaded() && !q.brick->isLoading()) ||
				(q.vd && !q.vd->isBrxml()))
			{
				runvl = true;
				break;
			}
		}
		if (runvl)
		{
			m_loader->Set(queues);
			m_loader->SetMemoryLimitByte((long long)TextureRenderer::get_mainmem_buf_size() * 1024LL * 1024LL);
			TextureRenderer::set_load_on_main_thread(false);
		}
	}
	const int cpus = CpuCount();
	if (ebd_queues.size() > 0)
	{
		m_ebd->Set(ebd_queues);
		if (runvl)
			m_ebd->SetMaxThreadNum(std::max(1, (cpus - 1) / 2));
		else
			m_ebd->SetMaxThreadNum(std::max(1, cpus - 1));
		m_ebd->Run();
	}
	if (runvl)
	{
		if (ebd_queues.size() > 0)
			m_loader->SetMaxThreadNum(std::max(1, (cpus - 1) - m_ebd->GetMaxThreadNum()));
		else
			m_loader->SetMaxThreadNum(std::max(1, cpus - 1));
		m_loader->Run();
	}

	if (total_num > 0)
	{
		if (!s_loop_timing)
		{
			s_loop_timing = true;
			s_loop_t0 = GET_TICK_COUNT();
			s_loop_frames = 0;
			s_loop_bricks = 0;
		}
		TextureRenderer::set_update_loop();
		TextureRenderer::set_total_brick_num(total_num);
		TextureRenderer::reset_done_current_chan();
	}
	else if (m_md_pop_list.size() > 0)
	{
		TextureRenderer::set_update_loop();
		TextureRenderer::set_total_brick_num(0);
	}
	else
		TextureRenderer::set_total_brick_num(0);

	TextureRenderer::reset_save_final_buffer();
	TextureRenderer::invalidate_temp_final_buffer();

	m_mv_mat = mv_temp;
}
