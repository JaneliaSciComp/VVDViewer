// Still capture of the Qt render canvas: the engine side of the wx
// "Capture" button.
//
//   wx (VRenderView.cpp)                      Qt
//   VRenderView::OnCapture (after dialog)  -> RenderCanvas::StartCapture
//                                             (dialog + project: CaptureDialog.cpp)
//   VRenderView::StartGroupCapture /
//     SetupNextGroupCapture /
//     IsolateGroupForCapture /
//     RestoreGroupCapVisibility            -> StartGroupCapture / SetupNextGroupCapture
//   VRenderVulkanView::StartTileRendering  -> StartTileRendering
//   VRenderVulkanView::EndTileRendering    -> EndTileRendering
//   VRenderVulkanView::Get1x1DispSize      -> Get1x1DispSize
//   HandleProjection (m_tile_rendering)    -> ApplyTileProjection
//   DrawVolumes / DrawVolumesDP "final
//     composition" tile branch + DrawTile  -> DrawTiledFinalBuffer / DrawTile
//   OnDraw: PostDraw() before DrawFrame /
//     DrawInfo / brush                     -> CaptureSwapchain (inside the frame)
//   PostDraw (readback, TIFF)              -> CapturePostDraw (after submitFrame)
//   OnIdle Esc                             -> AbortCapture
//
// Differences from wx, all deliberate:
//  * wx only honours the width/height of the dialog when both are > 4000
//    and differ from the view size (tile rendering); any other size is
//    silently captured at the view size. Here every size that differs from
//    the view size is rendered with the same tiling code (tiles of at most
//    ~2000 px, same tile-size formula), so 2x etc. work too.
//  * wx writes rows [y, h) of a frame-cropped non-tiled capture (only right
//    when the frame starts at y = 0); rows [y, y + h) are written here.
//  * Esc during tile rendering cancels the capture (wx then captured the
//    next normal frame into the file).
//  * A capture with nothing shown falls back to the view-size path (the
//    tile loop is driven by the volume / mesh pass).

#include "RenderCanvas.h"
#include "CaptureWriter.h"

#include <QFile>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <new>
#include <tiffio.h>
#include <glm/gtc/matrix_transform.hpp>

#include "VVulkan.h"
#include "Vulkan/Vulkan2dRender.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MultiVolumeRenderer.h"
#include "FLIVR/ImgShader.h"
#include "DataManager.h"
#include "utility.h"

using namespace FLIVR;

// ---------------------------------------------------------------------------
// TIFF writer (wx PostDraw)
// ---------------------------------------------------------------------------

bool CaptureWriter::WriteRGB8Tiff(const QString& path, const unsigned char* rgb,
	size_t w, size_t h, size_t row_pitch, bool compress, bool big_tiff)
{
	if (!rgb || w == 0 || h == 0)
		return false;
	const int dst_chann = 3;
	const QByteArray fn = QFile::encodeName(path);
	TIFF* out = TIFFOpen(fn.constData(), big_tiff ? "wb8" : "wb");
	if (!out)
		return false;
	TIFFSetField(out, TIFFTAG_IMAGEWIDTH, (uint32_t)w);
	TIFFSetField(out, TIFFTAG_IMAGELENGTH, (uint32_t)h);
	TIFFSetField(out, TIFFTAG_SAMPLESPERPIXEL, dst_chann);
	TIFFSetField(out, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField(out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	if (compress)
		TIFFSetField(out, TIFFTAG_COMPRESSION, COMPRESSION_LZW);

	tsize_t linebytes = (tsize_t)(dst_chann * w);
	unsigned char* buf = (unsigned char*)_TIFFmalloc(linebytes);
	TIFFSetField(out, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(out, 0));
	bool ok = buf != nullptr;
	for (uint32_t row = 0; ok && row < (uint32_t)h; row++)
	{
		memcpy(buf, rgb + (size_t)row * row_pitch, linebytes);
		if (TIFFWriteScanline(out, buf, row, 0) < 0)
			ok = false;
	}
	TIFFClose(out);
	if (buf)
		_TIFFfree(buf);
	return ok;
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

struct RenderCanvas::CaptureState
{
	// copy of the swapchain image at the wx PostDraw point
	std::unique_ptr<vks::VFrameBuffer> fbo_rec;
	std::shared_ptr<vks::VTexture> tex_rec;
	bool rec_pending = false;
	// wx m_fbo_tile / m_tex_tile (the finished tile, RGBA8 + background)
	std::unique_ptr<vks::VFrameBuffer> fbo_tile;
	std::shared_ptr<vks::VTexture> tex_tile;
	// wx m_fbo_tiled_tmp / m_tex_tiled_tmp (preview of the tiles so far)
	std::unique_ptr<vks::VFrameBuffer> fbo_tiled_tmp;
	std::shared_ptr<vks::VTexture> tex_tiled_tmp;
	Vulkan2dRender::V2dObject tile_vobj;
	// wx m_tiled_image (RGB, capture size)
	std::vector<unsigned char> tiled_image;
	// wx m_group_cap_queue / m_group_cap_index / m_group_cap_saved_vd_vis
	std::vector<GroupCaptureEntry> group_queue;
	int group_index = 0;
	std::vector<std::pair<VolumeData*, bool>> saved_vis;
	std::vector<std::pair<DataGroup*, bool>> saved_grp_vis;
};

RenderCanvas::CaptureState& RenderCanvas::Cap()
{
	if (!m_cap)
		m_cap = std::make_shared<CaptureState>();
	return *m_cap;
}

void RenderCanvas::ReleaseCaptureResources()
{
	if (!m_cap) return;
	if (m_vulkan && m_vulkan->vulkanDevice)
		vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
	m_cap->fbo_rec.reset();
	m_cap->tex_rec.reset();
	m_cap->fbo_tile.reset();
	m_cap->tex_tile.reset();
	m_cap->fbo_tiled_tmp.reset();
	m_cap->tex_tiled_tmp.reset();
	m_cap->tiled_image.clear();
	m_cap->tiled_image.shrink_to_fit();
}

// wx Resize(): volume renderers re-create their blend buffers at the new size
void RenderCanvas::ResizeRenderers()
{
	for (VolumeData* vd : m_volumes)
		if (vd && vd->GetVR()) vd->GetVR()->resize();
	ResizeMultiVolumeRenderer();
}

// ---------------------------------------------------------------------------
// entry points
// ---------------------------------------------------------------------------

// wx VRenderView::OnCapture, the part after the file dialog (minus the
// project save, which the caller does: CaptureDialog.cpp)
void RenderCanvas::StartCapture(const QString& file, int resx, int resy, bool compress)
{
	if (file.isEmpty()) return;
	if (m_tile_rendering) EndTileRendering();
	m_cap_file = file;
	m_cap_compress = compress;
	m_capture = true;
	m_postdraw = false;
	Cap().rec_pending = false;

	const QSize d = DeviceSize();
	bool any_visible = !PopVolumeList().empty();
	PopMeshList();
	if (!m_md_pop_list.empty()) any_visible = true;

	if (resx > 0 && resy > 0 && (resx != d.width() || resy != d.height()) && any_visible)
	{
		// wx: if (m_cap_resx > 4000 && m_cap_resy > 4000 && ...) -- see the
		// file comment; the tile size formula is the wx one
		int tilew = resx / (resx / 2000 + 1) + (resx / 2000 + 1);
		int tileh = resy / (resy / 2000 + 1) + (resy / 2000 + 1);
		StartTileRendering(resx, resy, tilew, tileh);
	}
	else
	{
		// wx: m_glview->Resize()
		ResizeRenderers();
		RequestRender();
	}
}

bool RenderCanvas::StartGroupCapture(const std::vector<GroupCaptureEntry>& groups, bool compress)
{
	if (groups.empty()) return false;
	if (m_tile_rendering) EndTileRendering();
	CaptureState& c = Cap();
	// wx StartGroupCapture: save the current visibility of the top-level
	// volumes, the groups and their volumes
	c.saved_vis.clear();
	c.saved_grp_vis.clear();
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		if (layer->IsA() == 2)
		{
			VolumeData* vd = (VolumeData*)layer;
			c.saved_vis.push_back(std::make_pair(vd, vd->GetDisp()));
		}
		else if (layer->IsA() == 5)
		{
			DataGroup* g = (DataGroup*)layer;
			c.saved_grp_vis.push_back(std::make_pair(g, g->GetDisp()));
			for (int j = 0; j < g->GetVolumeNum(); j++)
				if (VolumeData* vd = g->GetVolumeData(j))
					c.saved_vis.push_back(std::make_pair(vd, vd->GetDisp()));
		}
	}
	c.group_queue = groups;
	c.group_index = 0;
	m_cap_compress = compress;
	m_capture_groups = true;
	SetupNextGroupCapture();
	return true;
}

// wx VRenderView::SetupNextGroupCapture + IsolateGroupForCapture +
// RestoreGroupCapVisibility
void RenderCanvas::SetupNextGroupCapture()
{
	CaptureState& c = Cap();
	if (!m_capture_groups) return;
	int idx = c.group_index;
	if (idx >= (int)c.group_queue.size())
	{
		// all groups done (RestoreGroupCapVisibility)
		for (auto& p : c.saved_grp_vis)
			if (std::find(m_layer_list.begin(), m_layer_list.end(), (TreeLayer*)p.first) != m_layer_list.end())
				p.first->SetDisp(p.second);
		for (auto& p : c.saved_vis)
			if (std::find(m_volumes.begin(), m_volumes.end(), p.first) != m_volumes.end())
				p.first->SetDisp(p.second);
		m_capture_groups = false;
		c.group_queue.clear();
		c.saved_vis.clear();
		c.saved_grp_vis.clear();
		RequestRender();
		Q_EMIT groupCaptureFinished();
		return;
	}
	const GroupCaptureEntry& e = c.group_queue[idx];
	// IsolateGroupForCapture: top-level volumes hidden, every group shown;
	// the target group and groups named "background" keep their volumes'
	// saved visibility, the other groups' volumes are hidden
	auto saved = [&c](VolumeData* vd) {
		auto it = std::find_if(c.saved_vis.begin(), c.saved_vis.end(),
			[vd](const std::pair<VolumeData*, bool>& p) { return p.first == vd; });
		return it != c.saved_vis.end() ? it->second : true;
	};
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		if (layer->IsA() == 2)
			((VolumeData*)layer)->SetDisp(false);
		else if (layer->IsA() == 5)
		{
			DataGroup* g = (DataGroup*)layer;
			g->SetDisp(true);
			bool isBg = QString::fromStdWString(g->GetName()).toLower() == "background";
			bool isTarget = (g == e.group) || isBg;
			if (!e.group)   // no group given: the entry's volumes
				isTarget = false;
			for (int j = 0; j < g->GetVolumeNum(); j++)
			{
				VolumeData* vd = g->GetVolumeData(j);
				if (!vd) continue;
				bool target = isTarget || (!e.group && std::find(e.volumes.begin(), e.volumes.end(), vd) != e.volumes.end());
				vd->SetDisp(target ? saved(vd) : false);
			}
		}
	}
	m_cap_file = e.filename;
	m_capture = true;
	m_postdraw = false;
	c.rec_pending = false;
	RequestRender();
}

void RenderCanvas::AbortCapture()
{
	const bool was = m_capture;
	const QString file = m_cap_file;
	m_capture = false;
	m_postdraw = false;
	if (m_cap) m_cap->rec_pending = false;
	if (m_tile_rendering) EndTileRendering();
	if (was) Q_EMIT captureFinished(file, false);
	if (m_capture_groups)
	{
		// stop the queue, restore visibility
		Cap().group_index = (int)Cap().group_queue.size();
		SetupNextGroupCapture();
	}
}

// verbatim from VRenderVulkanView::Get1x1DispSize
void RenderCanvas::Get1x1DispSize(int& w, int& h)
{
	QSize s = DeviceSize();
	int nx = std::max(1, s.width());
	int ny = std::max(1, s.height());

	double maxvpd = 0.0;
	for (VolumeData* vd : PopVolumeList())
	{
		Texture* vtex = vd->GetTexture();
		if (vtex)
		{
			double aspect = (double)nx / (double)ny;
			double spc_x;
			double spc_y;
			double spc_z;
			vtex->get_spacings(spc_x, spc_y, spc_z, 0);
			spc_x = spc_x < EPS ? 1.0 : spc_x;
			spc_y = spc_y < EPS ? 1.0 : spc_y;

			double sf;
			if (aspect > 1.0)
				sf = 2.0 * m_radius / spc_y / double(ny);
			else
				sf = 2.0 * m_radius / spc_x / double(nx);

			double vx_per_dot = m_scale_factor / sf;

			if (maxvpd < vx_per_dot) maxvpd = vx_per_dot;
		}
	}

	if (maxvpd <= 0.0)
	{
		// no volume: wx divides by zero here; the dialog then falls back to
		// the view size (w, h <= 0)
		w = h = -1;
		return;
	}
	w = (int)(nx / maxvpd);
	h = (int)(ny / maxvpd);
}

// ---------------------------------------------------------------------------
// tile rendering
// ---------------------------------------------------------------------------

void RenderCanvas::StartTileRendering(int w_, int h_, int tilew_, int tileh_)
{
	m_tile_rendering = true;
	m_capture_resx = w_;
	m_capture_resy = h_;
	m_tile_w = tilew_;
	m_tile_h = tileh_;
	m_tile_xnum = w_ / tilew_ + (w_ % tilew_ > 0);
	m_tile_ynum = h_ / tileh_ + (h_ % tileh_ > 0);
	m_current_tileid = 0;

	// wx: m_tmp_res_mode = m_res_mode; m_res_mode = 1;
	m_tmp_res_mode = m_res_mode;
	m_res_mode = 1;

	CaptureState& c = Cap();
	if (m_capture)
	{
		c.tiled_image.clear();
		try
		{
			c.tiled_image.assign((size_t)m_capture_resx * (size_t)m_capture_resy * 3, 0);
		}
		catch (const std::bad_alloc&)
		{
			qWarning("RenderCanvas: capture: cannot allocate %d x %d image", m_capture_resx, m_capture_resy);
			c.tiled_image.clear();
			AbortCapture();
			return;
		}
	}

	// wx Resize()
	ResizeRenderers();
	RequestRender();
}

void RenderCanvas::EndTileRendering()
{
	m_tile_rendering = false;
	m_res_mode = m_tmp_res_mode;
	if (m_cap)
	{
		m_cap->tiled_image.clear();
		m_cap->tiled_image.shrink_to_fit();
	}
	// wx Resize(false); the Qt canvas also redraws at the view size right away
	ResizeRenderers();
	RequestRender();
}

// wx HandleProjection, the m_tile_rendering parts (nx, ny = capture size)
void RenderCanvas::ApplyTileProjection(int nx, int ny)
{
	int tilex = m_current_tileid % m_tile_xnum;
	int tiley = m_current_tileid / m_tile_xnum;
	if (m_persp)
	{
		double tilew = 2.0 * m_tile_w / nx;
		double tileh = 2.0 * m_tile_h / ny;
		double scalex = (double)nx / (double)(m_tile_w);
		double scaley = (double)ny / (double)(m_tile_h);
		double tx = -1.0 * (-1.0 + tilew / 2.0 + tilew * tilex);
		double ty = -1.0 * (-1.0 + tileh / 2.0 + tileh * tiley);
		auto scale = glm::scale(glm::mat4(1.0f), glm::vec3((float)scalex, (float)scaley, 1.f));
		auto translate = glm::translate(glm::mat4(1.0f), glm::vec3((float)tx, (float)ty, 0.f));
		m_proj_mat = scale * translate * m_proj_mat;
	}
	else
	{
		float shiftX = (double)m_tile_w * (m_ortho_right - m_ortho_left) / m_capture_resx;
		float shiftY = (double)m_tile_h * (m_ortho_top - m_ortho_bottom) / m_capture_resy;
		m_proj_mat = glm::ortho((float)(m_ortho_left + shiftX * tilex), (float)(m_ortho_left + shiftX * (tilex + 1)),
			(float)(m_ortho_bottom + shiftY * tiley), (float)(m_ortho_bottom + shiftY * (tiley + 1)),
			(float)(-m_far_clip / 100.0), (float)m_far_clip);
	}
}

// wx DrawVolumes / DrawVolumesDP: "final composition" with m_tile_rendering
// (the tile into the preview buffer, the preview to the swapchain), then the
// tile-advance block (DrawTile, m_current_tileid++, m_postdraw).
void RenderCanvas::DrawTiledFinalBuffer()
{
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	CaptureState& c = Cap();
	const int nx = m_fbo_final->w, ny = m_fbo_final->h;   // tile size

	if (c.fbo_tiled_tmp && (c.fbo_tiled_tmp->w != nx || c.fbo_tiled_tmp->h != ny))
	{
		c.fbo_tiled_tmp.reset();
		c.tex_tiled_tmp.reset();
	}
	if (!c.fbo_tiled_tmp)
	{
		c.fbo_tiled_tmp = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		c.fbo_tiled_tmp->w = nx;
		c.fbo_tiled_tmp->h = ny;
		c.fbo_tiled_tmp->device = prim_dev;
		c.tex_tiled_tmp = prim_dev->GenTexture2D(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FILTER_LINEAR, nx, ny);
		c.fbo_tiled_tmp->addAttachment(c.tex_tiled_tmp);
	}

	// wx StartTileRendering: the quad of this tile in view NDC
	{
		const QSize ws = DeviceSize();
		int w = std::max(1, ws.width());
		int h = std::max(1, ws.height());
		const int tileid = m_current_tileid;

		double win_aspect = (double)w / (double)h;
		double ren_aspect = (double)m_capture_resx / (double)m_capture_resy;
		double stpos_x, stpos_y, edpos_x, edpos_y, tilew, tileh, xbound, ybound;
		if (win_aspect > ren_aspect)
		{
			tilew = 2.0 * m_tile_w * h / m_capture_resy / w;
			tileh = 2.0 * m_tile_h * h / m_capture_resy / h;
			stpos_x = -1.0 + 2.0 * (w - h * ren_aspect) / 2.0 / w;
			stpos_y = -1.0;
			xbound = stpos_x + 2.0 * h * ren_aspect / w;
			ybound = 1.0;

			stpos_x = stpos_x + tilew * (tileid % m_tile_xnum);
			stpos_y = stpos_y + tileh * (tileid / m_tile_xnum);
			edpos_x = stpos_x + tilew;
			edpos_y = stpos_y + tileh;
		}
		else
		{
			tilew = 2.0 * m_tile_w * w / m_capture_resx / w;
			tileh = 2.0 * m_tile_h * w / m_capture_resx / h;
			stpos_x = -1.0;
			stpos_y = -1.0 + 2.0 * (h - w / ren_aspect) / 2.0 / h;
			xbound = 1.0;
			ybound = stpos_y + 2.0 * w / ren_aspect / h;

			stpos_x = stpos_x + tilew * (tileid % m_tile_xnum);
			stpos_y = stpos_y + tileh * (tileid / m_tile_xnum);
			edpos_x = stpos_x + tilew;
			edpos_y = stpos_y + tileh;
		}

		double tex_x = 1.0;
		double tex_y = 1.0;
		if (edpos_x > xbound)
		{
			tex_x = (xbound - stpos_x) / tilew;
			edpos_x = xbound;
		}
		if (edpos_y > ybound)
		{
			tex_y = (ybound - stpos_y) / tileh;
			edpos_y = ybound;
		}

		std::vector<Vulkan2dRender::Vertex> verts;
		verts.push_back(Vulkan2dRender::Vertex{ {(float)edpos_x, (float)edpos_y, 0.0f}, {(float)tex_x, (float)tex_y, 0.0f} });
		verts.push_back(Vulkan2dRender::Vertex{ {(float)stpos_x, (float)edpos_y, 0.0f}, {0.0f,         (float)tex_y, 0.0f} });
		verts.push_back(Vulkan2dRender::Vertex{ {(float)stpos_x, (float)stpos_y, 0.0f}, {0.0f,         0.0f,         0.0f} });
		verts.push_back(Vulkan2dRender::Vertex{ {(float)edpos_x, (float)stpos_y, 0.0f}, {(float)tex_x, 0.0f,         0.0f} });
		std::vector<uint32_t> indices = { 0,1,2, 2,3,0 };

		// per-frame host ring (ViewOverlays / RulerTool) instead of the wx
		// buffer of all tile quads: nothing to retire when a capture ends
		VkDeviceSize vsize = verts.size() * sizeof(Vulkan2dRender::Vertex);
		VkDeviceSize isize = indices.size() * sizeof(uint32_t);
		prim_dev->GetNextHostVertexBuffer(vsize, c.tile_vobj.vertBuf, c.tile_vobj.vertOffset);
		memcpy((char*)c.tile_vobj.vertBuf.mapped + c.tile_vobj.vertOffset, verts.data(), vsize);
		prim_dev->GetNextHostIndexBuffer(isize, c.tile_vobj.idxBuf, c.tile_vobj.idxOffset);
		memcpy((char*)c.tile_vobj.idxBuf.mapped + c.tile_vobj.idxOffset, indices.data(), isize);
		c.tile_vobj.vertCount = verts.size();
		c.tile_vobj.idxCount = indices.size();
	}

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHADER_TEXTURE_LOOKUP,
			V2DRENDER_BLEND_OVER,
			c.fbo_tiled_tmp->attachments[0]->format,
			(int)c.fbo_tiled_tmp->attachments.size(),
			0,
			c.fbo_tiled_tmp->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_final.get();
	if (m_current_tileid == 0)
		params.clear = true;
	params.obj = &c.tile_vobj;
	if (!c.fbo_tiled_tmp->renderPass)
		c.fbo_tiled_tmp->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(c.fbo_tiled_tmp, params);

	Vulkan2dRender::V2DRenderParams params2 = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params2.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
			V2DRENDER_BLEND_OVER,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images);
	params2.tex[0] = c.tex_tiled_tmp.get();
	params2.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params2.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params2.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	params2.clear = m_frame_clear;
	params2.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	m_frame_clear = false;
	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params2.pipeline.pass);
	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params2);

	// wx: if ((start_update_loop && done_update_loop) || !mem_swap || total_brick_num == 0)
	// The Depth-mode pass (DrawVolumesDP) resets start_update_loop before
	// the final composition, so the finished loop is detected by
	// done_update_loop alone (it is false from StartLoopUpdate until the
	// loop is complete).
	if (m_current_tileid < m_tile_xnum * m_tile_ynum)
	{
		if (TextureRenderer::get_done_update_loop() ||
			!TextureRenderer::get_mem_swap() ||
			TextureRenderer::get_total_brick_num() == 0)
		{
			DrawTile();
			m_current_tileid++;
			if (m_capture) m_postdraw = true;
		}
	}
}

// verbatim from VRenderVulkanView::DrawTile
void RenderCanvas::DrawTile()
{
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	CaptureState& c = Cap();
	const int nx = m_fbo_final->w, ny = m_fbo_final->h;

	if (c.fbo_tile && (c.fbo_tile->w != nx || c.fbo_tile->h != ny))
	{
		c.fbo_tile.reset();
		c.tex_tile.reset();
	}
	if (!c.fbo_tile)
	{
		c.fbo_tile = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		c.fbo_tile->w = nx;
		c.fbo_tile->h = ny;
		c.fbo_tile->device = prim_dev;
		c.tex_tile = prim_dev->GenTexture2D(VK_FORMAT_R8G8B8A8_UNORM, VK_FILTER_LINEAR, nx, ny);
		c.fbo_tile->addAttachment(c.tex_tile);
	}

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 0.0f };
	params.clear = true;
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_BLEND_BRIGHT_BACKGROUND_HDR,
			V2DRENDER_BLEND_OVER,
			c.fbo_tile->attachments[0]->format,
			(int)c.fbo_tile->attachments.size(),
			0,
			c.fbo_tile->attachments[0]->is_swapchain_images);
	params.tex[0] = m_tex_final.get();
	params.loc[0] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	params.loc[2] = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	if (!c.fbo_tile->renderPass)
		c.fbo_tile->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(c.fbo_tile, params);
}

// ---------------------------------------------------------------------------
// readback (wx PostDraw)
// ---------------------------------------------------------------------------

// Inside the frame, at the wx PostDraw point (scene, rulers, camera centre,
// legend, scale bar, colour map drawn; frame border / Info / brush not yet):
// copy the swapchain image (wx downloads current_fbo->attachments[0]; the Qt
// swapchain is copied first, like RecordFrame, and read after submitFrame).
void RenderCanvas::CaptureSwapchain(int nx, int ny)
{
	if (!m_capture || m_cap_file.isEmpty() || m_tile_rendering)
		return;
	// wx PostDraw: skip while the streaming loop is still running
	if (!IsUpdateLoopDone())
		return;
	CaptureState& c = Cap();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
	if (!current_fbo) return;
	if (c.fbo_rec && (c.fbo_rec->w != nx || c.fbo_rec->h != ny ||
		c.tex_rec->format != current_fbo->attachments[0]->format))
	{
		c.fbo_rec.reset();
		c.tex_rec.reset();
	}
	if (!c.fbo_rec)
	{
		c.fbo_rec = std::make_unique<vks::VFrameBuffer>(vks::VFrameBuffer());
		c.fbo_rec->w = nx;
		c.fbo_rec->h = ny;
		c.fbo_rec->device = prim_dev;
		c.tex_rec = prim_dev->GenTexture2D(current_fbo->attachments[0]->format, VK_FILTER_LINEAR, nx, ny);
		c.fbo_rec->addAttachment(c.tex_rec);
	}
	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	params.pipeline = m_v2drender->preparePipeline(IMG_SHADER_TEXTURE_LOOKUP, V2DRENDER_BLEND_DISABLE,
		c.fbo_rec->attachments[0]->format, (int)c.fbo_rec->attachments.size(), 0,
		c.fbo_rec->attachments[0]->is_swapchain_images);
	params.tex[0] = current_fbo->attachments[0].get();
	params.clear = true;
	if (!c.fbo_rec->renderPass)
		c.fbo_rec->replaceRenderPass(params.pipeline.pass);
	m_v2drender->render(c.fbo_rec, params);
	c.rec_pending = true;
}

// wx VRenderVulkanView::PostDraw (capture part) + the end of UpdateScreen
// (EndTileRendering) and OnIdle (m_refresh: next tile)
void RenderCanvas::CapturePostDraw()
{
	CaptureState* c = m_cap.get();
	const bool have_frame = c && c->rec_pending;
	bool finished = false, finished_ok = false;
	QString finished_file;
	if (m_capture && !m_cap_file.isEmpty() && (have_frame || m_postdraw))
	{
		//the readbacks below copy out of this frame's render target, so its
		//GPU work must be complete
		vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
		vks::VulkanDevice* prim_dev = m_vulkan->vulkanDevice;
		const QString outputfilename = m_cap_file;
		const int chann = 4;
		const int dst_chann = 3;
		bool done = true;
		bool ok = false;

		if (m_tile_rendering && m_postdraw && !c->tiled_image.empty() && c->tex_tile)
		{
			const size_t tw = (size_t)c->fbo_tile->w, th = (size_t)c->fbo_tile->h;
			std::vector<unsigned char> image(tw * th * chann);
			prim_dev->DownloadTexture(c->tex_tile, image.data());
			size_t stx = (size_t)((m_current_tileid - 1) % m_tile_xnum) * m_tile_w;
			size_t sty = (size_t)((m_current_tileid - 1) / m_tile_xnum) * m_tile_h;
			size_t edx = stx + m_tile_w;
			size_t edy = sty + m_tile_h;
			if (edx > (size_t)m_capture_resx) edx = m_capture_resx;
			if (edy > (size_t)m_capture_resy) edy = m_capture_resy;
			for (size_t y = sty; y < edy; y++)
				for (size_t x = stx; x < edx; x++)
					for (int ch = 0; ch < dst_chann; ch++)
						c->tiled_image[(y * m_capture_resx + x) * dst_chann + ch] =
							image[((y - sty) * tw + (x - stx)) * chann + ch];

			if (m_current_tileid < m_tile_xnum * m_tile_ynum)
				done = false;
			else
			{
				const QSize ws = DeviceSize();
				double scalefac = ws.width() > ws.height() ?
					(double)m_capture_resy / (double)ws.height() :
					(double)m_capture_resx / (double)ws.width();

				size_t cap_ox = m_draw_frame ? (size_t)(m_frame_x * scalefac) : 0;
				size_t cap_oy = m_draw_frame ? (size_t)(m_frame_y * scalefac) : 0;
				size_t cap_w = m_draw_frame ? (size_t)(m_frame_w * scalefac) : m_capture_resx;
				size_t cap_h = m_draw_frame ? (size_t)(m_frame_h * scalefac) : m_capture_resy;

				if (cap_ox <= (size_t)m_capture_resx && cap_oy <= (size_t)m_capture_resy)
				{
					if (cap_ox + cap_w > (size_t)m_capture_resx)
						cap_w = m_capture_resx - cap_ox;
					if (cap_oy + cap_h > (size_t)m_capture_resy)
						cap_h = m_capture_resy - cap_oy;
					size_t pitchX = (size_t)dst_chann * m_capture_resx;
					ok = CaptureWriter::WriteRGB8Tiff(outputfilename,
						&c->tiled_image[cap_oy * pitchX + cap_ox * dst_chann],
						cap_w, cap_h, pitchX, m_cap_compress, CaptureWriter::NeedsBigTiff(cap_w, cap_h));
				}
				c->tiled_image.clear();
				c->tiled_image.shrink_to_fit();
			}
		}
		else if (!m_tile_rendering && have_frame && c->tex_rec)
		{
			const int w = c->fbo_rec->w, h = c->fbo_rec->h;
			std::vector<unsigned char> image((size_t)w * h * chann);
			prim_dev->DownloadTexture(c->tex_rec, image.data());

			const VkFormat fmt = c->tex_rec->format;
			const bool colorSwizzleBGR = fmt == VK_FORMAT_B8G8R8A8_SRGB ||
				fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SNORM;
			std::vector<unsigned char> rgb_image((size_t)w * h * dst_chann);
			for (size_t y = 0; y < (size_t)h; y++)
				for (size_t x = 0; x < (size_t)w; x++)
				{
					const unsigned char* s = &image[(y * w + x) * chann];
					unsigned char* d = &rgb_image[(y * w + x) * dst_chann];
					if (colorSwizzleBGR) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; }
					else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
				}

			int x = 0, y = 0, cw = w, ch = h;
			if (m_draw_frame)
			{
				x = std::clamp(m_frame_x, 0, w);
				y = std::clamp(m_frame_y, 0, h);
				cw = std::clamp(m_frame_w, 0, w - x);
				ch = std::clamp(m_frame_h, 0, h - y);
			}
			if (cw > 0 && ch > 0)
				ok = CaptureWriter::WriteRGB8Tiff(outputfilename,
					&rgb_image[((size_t)y * w + x) * dst_chann], cw, ch, (size_t)w * dst_chann, m_cap_compress);
		}
		else
			done = !m_tile_rendering;

		if (c) c->rec_pending = false;
		m_postdraw = false;
		if (done)
		{
			m_capture = false;
			if (!ok) qWarning() << "RenderCanvas: capture: could not write" << outputfilename;
			finished = true;
			finished_ok = ok;
			finished_file = outputfilename;
		}
	}
	m_postdraw = false;

	// wx UpdateScreen: EndTileRendering once the last tile is drawn;
	// OnIdle m_refresh: render the next tile
	if (m_tile_rendering)
	{
		if (m_current_tileid >= m_tile_xnum * m_tile_ynum)
			EndTileRendering();
		else if (TextureRenderer::get_done_update_loop())
			RequestRender();
	}

	if (finished)
	{
		Q_EMIT captureFinished(finished_file, finished_ok);
		// wx: group capture advances after the current call stack unwinds
		if (m_capture_groups)
		{
			Cap().group_index++;
			QTimer::singleShot(0, this, [this]() { SetupNextGroupCapture(); });
		}
	}
}
