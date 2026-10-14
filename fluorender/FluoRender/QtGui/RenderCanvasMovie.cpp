// Movie support of the Qt render canvas: the VRenderVulkanView parts that
// the Record/Export pane (wx VMovieView / RecorderDlg) drives.
//
//   wx (VRenderView.cpp / .h)                 Qt
//   VRenderVulkanView::SetParams           -> RenderCanvas::SetParams
//   VRenderVulkanView::AddKeyFrame         -> RenderCanvas::AddKeyFrame
//   VRenderVulkanView::Get4DSeqFrames      -> Get4DSeqFrames
//   VRenderVulkanView::Set4DSeqFrame       -> Set4DSeqFrame (the 4D script runs
//                                             from the Record/Export playback,
//                                             MainWindowScript.cpp)
//   VRenderVulkanView::Get3DBatFrames      -> Get3DBatFrames
//   VRenderVulkanView::Set3DBatFrame       -> Set3DBatFrame
//   VRenderVulkanView::CalcFrame           -> CalcFrame
//   m_recording / IsRecordedFrameReady /
//     DownloadRecordedFrame (OnDraw copy)  -> SetRecording / RequestRecordedFrame /
//                                             IsRecordedFrameReady / GetRecordedFrame
//
// Key codes are the wx ones (l0 = 1 view name, l1 = 1 view / 2 volume or
// mesh / 5 volume group / 6 mesh group, l2_name = property), so keys saved
// by either application play back in the other. Kept from wx on purpose:
// AddKeyFrame writes mesh-group keys with l1 = 5 while SetParams reads them
// with l1 = 6 (mesh-group keys never play back in wx either).

#include "RenderCanvas.h"

#include <chrono>
#include <thread>
#include <QFileInfo>

#include "VVulkan.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MeshRenderer.h"
#include "FLIVR/Texture.h"
#include "Animator/Interpolator.h"
#include "Formats/brkxml_reader.h"
#include "DataManager.h"

using namespace FLIVR;

// ---------------------------------------------------------------------------
// camera helpers
// ---------------------------------------------------------------------------

// wx VRenderView::SetTranslations (glview inline)
void RenderCanvas::SetTranslations(double x, double y, double z)
{
	m_transx = x; m_transy = y; m_transz = z;
	m_distance = std::sqrt(m_transx * m_transx + m_transy * m_transy + m_transz * m_transz);
	RequestRender();
}

// wx VRenderVulkanView::CalcFrame (the active branch)
void RenderCanvas::CalcFrame(int& fx, int& fy, int& fw, int& fh) const
{
	const QSize s = DeviceSize();
	int w = s.width(), h = s.height();
	if (m_tile_rendering)
	{
		w = m_capture_resx;
		h = m_capture_resy;
	}
	int size;
	if (w > h)
	{
		size = h;
		fx = int((w - h) / 2.0);
		fy = 0;
	}
	else
	{
		size = w;
		fx = 0;
		fy = int((h - w) / 2.0);
	}
	fw = fh = size;
}

// ---------------------------------------------------------------------------
// recording (wx m_recording, OnDraw copy, DownloadRecordedFrame)
// ---------------------------------------------------------------------------

void RenderCanvas::SetRecording(bool b)
{
	m_recording = b;
	m_rec_requested = false;
	m_rec_ready = false;
	if (!b)
	{
		m_rec_buf.clear();
		m_rec_buf.shrink_to_fit();
	}
}

void RenderCanvas::RequestRecordedFrame()
{
	if (!m_recording) return;
	m_rec_ready = false;
	m_rec_requested = true;
	RequestRender();
}

// after submitFrame of a frame copied by RecordFrame
void RenderCanvas::ReadRecordedFrame()
{
	m_rec_requested = false;
	if (!m_tex_record || !m_fbo_record) return;
	vkQueueWaitIdle(m_vulkan->vulkanDevice->queue);
	m_rec_w = m_fbo_record->w;
	m_rec_h = m_fbo_record->h;
	const VkFormat fmt = m_tex_record->format;
	m_rec_bgr = fmt == VK_FORMAT_B8G8R8A8_SRGB || fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SNORM;
	m_rec_buf.resize((size_t)m_rec_w * m_rec_h * 4);
	if (m_vulkan->vulkanDevice->DownloadTexture(m_tex_record, m_rec_buf.data()))
	{
		m_rec_ready = true;
		Q_EMIT recordedFrameReady();
	}
	else
		qWarning("RenderCanvas: recording: frame read-back failed");
}

// ---------------------------------------------------------------------------
// 4D sequence / 3D batch
// ---------------------------------------------------------------------------

void RenderCanvas::Get4DSeqFrames(int& start_frame, int& end_frame, int& cur_frame) const
{
	const std::vector<VolumeData*> list = PopVolumeList();
	for (int i = 0; i < (int)list.size(); i++)
	{
		VolumeData* vd = list[i];
		if (vd && vd->GetReader())
		{
			BaseReader* reader = vd->GetReader();

			int vd_start_frame = 0;
			int vd_end_frame = reader->GetTimeNum() - 1;
			// wx: reader->GetCurTime(). A time point the brick loader prefetched
			// (Convert_ThreadSafe, streaming) leaves the reader's current time
			// behind, so going back to that stale frame (e.g. the start frame
			// of a playback) was skipped and the last time point stayed on
			// screen; the volume's own time is the frame it shows.
			int vd_cur_frame = vd->GetCurTime();

			if (i == 0)
			{
				//first dataset
				start_frame = vd_start_frame;
				end_frame = vd_end_frame;
				cur_frame = vd_cur_frame;
			}
			else
			{
				//datasets after the first one
				if (vd_end_frame > end_frame)
					end_frame = vd_end_frame;
			}
		}
	}
}

void RenderCanvas::Set4DSeqFrame(int frame)
{
	int start_frame = 0, end_frame = 0, cur_frame = 0;
	Get4DSeqFrames(start_frame, end_frame, cur_frame);
	if (frame > end_frame || frame < start_frame)
		return;

	m_tseq_prv_num = m_tseq_cur_num;
	m_tseq_cur_num = frame;
	// wx: m_run_script / m_script_file from the SettingDlg (the run_script
	// argument of the wx function is false at every call site)
	if (m_script_settings)
		m_script_settings(m_run_script, m_script_file);

	for (VolumeData* vd : PopVolumeList())
	{
		if (!vd || !vd->GetReader())
			continue;
		BaseReader* reader = vd->GetReader();
		bool clear_pool = false;

		if (cur_frame != frame)
		{
			Texture* tex = vd->GetTexture();
			if (tex && tex->isBrxml())
			{
				BRKXMLReader* br = (BRKXMLReader*)reader;
				br->SetCurTime(frame);
				tex->set_FrameAndChannel(frame, vd->GetCurChannel());
				vd->SetCurTime(reader->GetCurTime());
				if (m_loader) m_loader->RemoveAllLoadedBrick();
				clear_pool = true;
			}
			else
			{
				double spcx, spcy, spcz;
				vd->GetSpacings(spcx, spcy, spcz);

				std::shared_ptr<VL_Nrrd> data;
				if (m_loader)
				{
					data = m_loader->GetLoadedNrrd(vd, vd->GetCurChannel(), frame);
					if (!data && m_loader->IsNrrdLoading(vd, vd->GetCurChannel(), frame))
					{
						// wx: poll the loader for up to 5 s
						const auto st = std::chrono::steady_clock::now();
						do {
							std::this_thread::sleep_for(std::chrono::milliseconds(10));
							data = m_loader->GetLoadedNrrd(vd, vd->GetCurChannel(), frame);
						} while (!data && std::chrono::steady_clock::now() - st <= std::chrono::milliseconds(5000));
					}
				}
				if (!data)
					data = reader->Convert(frame, vd->GetCurChannel(), false);

				if (!vd->Replace(data, false))
					continue;

				const QString data_path = QString::fromStdWString(reader->GetCurName(frame, vd->GetCurChannel()));
				QString data_name = QFileInfo(data_path).fileName();
				if (reader->GetChanNum() > 1)
					data_name += QString("_Ch%1").arg(vd->GetCurChannel() + 1);
				vd->SetName(VVDString(data_name.toStdWString()));
				vd->SetPath(VVDString(data_path.toStdWString()));

				vd->SetCurTime(frame);
				vd->SetSpacings(spcx, spcy, spcz);
				clear_pool = true;
			}
		}

		if (clear_pool && vd->GetVR())
			vd->GetVR()->clear_tex_current();
	}
	RequestRender();
	// wx: MeasureDlg::UpdateList, VRenderFrame::UpdateTreeFrames
	Q_EMIT dataFramesChanged(m_tseq_cur_num);
}

void RenderCanvas::Get3DBatFrames(int& start_frame, int& end_frame, int& cur_frame)
{
	m_bat_folder.clear();

	const std::vector<VolumeData*> list = PopVolumeList();
	for (int i = 0; i < (int)list.size(); i++)
	{
		VolumeData* vd = list[i];
		if (vd && vd->GetReader())
		{
			BaseReader* reader = vd->GetReader();
			reader->SetBatch(true);

			int vd_cur_frame = reader->GetCurBatch();
			int vd_start_frame = -vd_cur_frame;
			int vd_end_frame = reader->GetBatchNum() - 1 - vd_cur_frame;

			if (i > 0)
				m_bat_folder += "_";
			m_bat_folder += QString::fromStdWString(reader->GetDataName());

			if (i == 0)
			{
				//first dataset
				start_frame = vd_start_frame;
				end_frame = vd_end_frame;
				cur_frame = 0;
			}
			else
			{
				//datasets after the first one
				if (vd_start_frame < start_frame)
					start_frame = vd_start_frame;
				if (vd_end_frame > end_frame)
					end_frame = vd_end_frame;
			}
		}
	}
	cur_frame -= start_frame;
	end_frame -= start_frame;
	start_frame = 0;
}

void RenderCanvas::Set3DBatFrame(int offset)
{
	std::vector<BaseReader*> reader_list;
	m_bat_folder.clear();

	m_tseq_prv_num = m_tseq_cur_num;
	m_tseq_cur_num = offset;
	const std::vector<VolumeData*> list = PopVolumeList();
	for (int i = 0; i < (int)list.size(); i++)
	{
		VolumeData* vd = list[i];
		if (!(vd && vd->GetReader() && vd->GetReader()->GetBatch()))
			continue;
		Texture* tex = vd->GetTexture();
		BaseReader* reader = vd->GetReader();
		auto chan_disp = [&](const QString& data_name) {
			int chan_num = 0;
			if (data_name.contains("_1ch"))
				chan_num = 1;
			else if (data_name.contains("_2ch"))
				chan_num = 2;
			if (chan_num > 0 && vd->GetCurChannel() >= chan_num)
				vd->SetDisp(false);
			else
				vd->SetDisp(true);
		};
		if (tex && tex->isBrxml())
		{
			tex->set_FrameAndChannel(0, vd->GetCurChannel());
			vd->SetCurTime(reader->GetCurTime());
			QString data_name = QString::fromStdWString(reader->GetDataName());
			if (i > 0)
				m_bat_folder += "_";
			m_bat_folder += data_name;
			chan_disp(data_name);
			if (reader->GetChanNum() > 1)
				data_name += QString("_%1").arg(vd->GetCurChannel() + 1);
			vd->SetName(VVDString(data_name.toStdWString()));
		}
		else
		{
			if (std::find(reader_list.begin(), reader_list.end(), reader) == reader_list.end())
			{
				reader->LoadOffset(offset);
				reader_list.push_back(reader);
			}

			double spcx, spcy, spcz;
			vd->GetSpacings(spcx, spcy, spcz);

			auto data = reader->Convert(0, vd->GetCurChannel(), true);
			if (vd->Replace(data, true))
				vd->SetDisp(true);
			else
			{
				vd->SetDisp(false);
				continue;
			}

			QString data_name = QString::fromStdWString(reader->GetDataName());
			if (i > 0)
				m_bat_folder += "_";
			m_bat_folder += data_name;
			chan_disp(data_name);
			if (reader->GetChanNum() > 1)
				data_name += QString("_%1").arg(vd->GetCurChannel() + 1);
			vd->SetName(VVDString(data_name.toStdWString()));
			vd->SetPath(VVDString(reader->GetPathName()));
			vd->SetCurTime(reader->GetCurTime());
			vd->SetSpacings(spcx, spcy, spcz);
			if (vd->GetVR())
				vd->GetVR()->clear_tex_current();
		}
	}

	// wx InitView(INIT_BOUNDS|INIT_CENTER)
	RecomputeBounds();
	Q_EMIT dataFramesChanged(m_tseq_cur_num);
}

// ---------------------------------------------------------------------------
// keyframes (wx SetParams / AddKeyFrame)
// ---------------------------------------------------------------------------

namespace
{
	struct Keys
	{
		Interpolator* ip;
		std::string view;
		FLKeyCode Code(int l1, const std::string& l1_name, const char* l2_name) const
		{
			FLKeyCode k;
			k.l0 = 1;
			k.l0_name = view;
			k.l1 = l1;
			k.l1_name = l1_name;
			k.l2 = 0;
			k.l2_name = l2_name;
			return k;
		}
		// reading at t
		bool D(int l1, const std::string& n, const char* p, double t, double& v) const { return ip->GetDouble(Code(l1, n, p), t, v); }
		bool B(int l1, const std::string& n, const char* p, double t, bool& v) const { return ip->GetBoolean(Code(l1, n, p), t, v); }
		bool I(int l1, const std::string& n, const char* p, double t, int& v) const { return ip->GetInt(Code(l1, n, p), t, v); }
		bool Q(int l1, const std::string& n, const char* p, double t, Quaternion& v) const { return ip->GetQuaternion(Code(l1, n, p), t, v); }
		// adding (between Begin / End)
		void AD(int l1, const std::string& n, const char* p, double v) const { ip->AddKey(new FlKeyDouble(Code(l1, n, p), v)); }
		void AB(int l1, const std::string& n, const char* p, bool v) const { ip->AddKey(new FlKeyBoolean(Code(l1, n, p), v)); }
		void AI(int l1, const std::string& n, const char* p, int v) const { ip->AddKey(new FlKeyInt(Code(l1, n, p), v)); }
		void AQ(int l1, const std::string& n, const char* p, Quaternion v) const { ip->AddKey(new FlKeyQuaternion(Code(l1, n, p), v)); }
	};

	const char* kPlaneKeys[6] = { "x1_val", "x2_val", "y1_val", "y2_val", "z1_val", "z2_val" };

	// group / view level linked clipping state, read side (wx: SetSyncClippingPlanes ..
	// SetClipDistance on the group); 'Target' has the ClippingLayer API
	template <class Target>
	void ReadClipLayer(const Keys& k, int l1, const std::string& n, double t, Target* g)
	{
		bool bval, bx, by, bz;
		double val;
		int ix, iy, iz;
		Quaternion q;
		if (k.B(l1, n, "link_clipping_planes", t, bval))
			g->SetSyncClippingPlanes(bval);
		if (k.D(l1, n, "x1_val", t, val)) g->SetLinkedX1Param(val);
		if (k.D(l1, n, "x2_val", t, val)) g->SetLinkedX2Param(val);
		if (k.D(l1, n, "y1_val", t, val)) g->SetLinkedY1Param(val);
		if (k.D(l1, n, "y2_val", t, val)) g->SetLinkedY2Param(val);
		if (k.D(l1, n, "z1_val", t, val)) g->SetLinkedZ1Param(val);
		if (k.D(l1, n, "z2_val", t, val)) g->SetLinkedZ2Param(val);
		if (k.B(l1, n, "clip_link_x", t, bx) && k.B(l1, n, "clip_link_y", t, by) && k.B(l1, n, "clip_link_z", t, bz))
		{
			g->SetClippingLinkX(bx);
			g->SetClippingLinkY(by);
			g->SetClippingLinkZ(bz);
		}
		if (k.Q(l1, n, "clip_rotation", t, q))
		{
			double rotx, roty, rotz;
			q.ToEuler(rotx, roty, rotz);
			g->SetClippingPlaneRotationsRaw(rotx, roty, rotz);
		}
		if (k.I(l1, n, "clip_dist_x", t, ix) && k.I(l1, n, "clip_dist_y", t, iy) && k.I(l1, n, "clip_dist_z", t, iz))
			g->SetClipDistance(ix, iy, iz);
	}

	template <class Target>
	void AddClipLayer(const Keys& k, int l1, const std::string& n, Target* g)
	{
		double rotx, roty, rotz;
		int ix, iy, iz;
		Quaternion q;
		k.AB(l1, n, "link_clipping_planes", g->GetSyncClippingPlanes());
		for (int p = 0; p < 6; p++)
			k.AD(l1, n, kPlaneKeys[p], g->GetLinkedParam(p));
		k.AB(l1, n, "clip_link_x", g->GetClippingLinkX());
		k.AB(l1, n, "clip_link_y", g->GetClippingLinkY());
		k.AB(l1, n, "clip_link_z", g->GetClippingLinkZ());
		g->GetClippingPlaneRotationsRaw(rotx, roty, rotz);
		q.FromEuler(rotx, roty, rotz);
		k.AQ(l1, n, "clip_rotation", q);
		g->GetClipDistance(ix, iy, iz);
		k.AI(l1, n, "clip_dist_x", ix);
		k.AI(l1, n, "clip_dist_y", iy);
		k.AI(l1, n, "clip_dist_z", iz);
	}
}

void RenderCanvas::SetParams(Interpolator* interpolator, double t)
{
	if (!interpolator)
		return;
	Keys k{ interpolator, m_view_name.toStdString() };
	const std::string& vname = k.view;

	bool bval;
	bool oldversion = false;
	//link clipping planes
	if (k.B(1, vname, "link_planes", t, bval))
	{
		SetSyncClippingPlanes(bval);
		oldversion = true;
	}
	if (k.B(1, vname, "link_clipping_planes", t, bval))
		SetSyncClippingPlanes(bval);

	double val = 0;
	int ix, iy, iz;
	bool bx, by, bz;

	//clipping planes
	for (int p = 0; p < 6; p++)
		if (k.D(1, vname, kPlaneKeys[p], t, val))
			SetLinkedParam(p, val);
	if (k.B(1, vname, "clip_link_x", t, bx) && k.B(1, vname, "clip_link_y", t, by) && k.B(1, vname, "clip_link_z", t, bz))
	{
		SetClippingLinkX(bx);
		SetClippingLinkY(by);
		SetClippingLinkZ(bz);
	}
	//clipping plane rotation
	Quaternion q;
	if (k.Q(1, vname, "clip_rotation", t, q))
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		SetClippingPlaneRotations(rotx, roty, -rotz);
	}
	if (k.I(1, vname, "clip_dist_x", t, ix) && k.I(1, vname, "clip_dist_y", t, iy) && k.I(1, vname, "clip_dist_z", t, iz))
		SetClipDistance(ix, iy, iz);

	// one volume / mesh (wx: the same block for volumes and meshes)
	auto read_planes = [&](const std::string& n, std::vector<Plane*>* planes) {
		for (int p = 0; p < 6; p++)
		{
			if (k.D(2, n, kPlaneKeys[p], t, val))
			{
				(*planes)[p]->SetParam(val);
				if (oldversion)
					SetLinkedParam(p, val);
			}
		}
	};
	auto read_item_clip = [&](const std::string& n, auto* item) {
		if (k.B(2, n, "clip_link_x", t, bx) && k.B(2, n, "clip_link_y", t, by) && k.B(2, n, "clip_link_z", t, bz))
		{
			item->SetClippingLinkX(bx);
			item->SetClippingLinkY(by);
			item->SetClippingLinkZ(bz);
			if (oldversion)
			{
				SetClippingLinkX(bx);
				SetClippingLinkY(by);
				SetClippingLinkZ(bz);
			}
		}
		Quaternion q2;
		if (k.Q(2, n, "clip_rotation", t, q2))
		{
			double rotx, roty, rotz;
			q2.ToEuler(rotx, roty, rotz);
			item->SetClippingPlaneRotationsRaw(rotx, roty, rotz);
			if (oldversion)
				SetClippingPlaneRotations(rotx, roty, rotz);
		}
		if (k.I(2, n, "clip_dist_x", t, ix) && k.I(2, n, "clip_dist_y", t, iy) && k.I(2, n, "clip_dist_z", t, iz))
		{
			item->SetClipDistance(ix, iy, iz);
			if (oldversion)
				SetClipDistance(ix, iy, iz);
		}
	};
	auto read_volume = [&](VolumeData* vd) {
		const std::string n = vd->GetName().ToStdString();
		if (k.B(2, n, "display", t, bval))
			vd->SetDisp(bval);
		std::vector<Plane*>* planes = vd->GetVR() ? vd->GetVR()->get_planes() : nullptr;
		if (!planes || planes->size() != 6) return;
		read_planes(n, planes);
		read_item_clip(n, vd);
		if (k.D(2, n, "gamma", t, val)) vd->Set3DGamma(val);
		if (k.D(2, n, "saturation", t, val)) vd->SetOffset(val);
		if (k.D(2, n, "luminance", t, val)) vd->SetLuminance(val);
		if (k.B(2, n, "alpha_enable", t, bval)) vd->SetEnableAlpha(bval);
		if (k.D(2, n, "alpha", t, val)) vd->SetAlpha(val);
		if (k.B(2, n, "shading_enable", t, bval)) vd->SetShading(bval);
		if (k.D(2, n, "shading", t, val)) vd->SetLowShading(val);
		if (k.D(2, n, "boundary", t, val)) vd->SetBoundary(val);
		if (k.D(2, n, "left_threshold", t, val)) vd->SetLeftThresh(val);
		if (k.D(2, n, "right_threshold", t, val)) vd->SetRightThresh(val);
		if (k.B(2, n, "inv", t, bval)) vd->SetInvert(bval);
	};
	auto read_mesh = [&](MeshData* md) {
		const std::string n = md->GetName().ToStdString();
		if (k.B(2, n, "display", t, bval))
			md->SetDisp(bval);
		std::vector<Plane*>* planes = md->GetMR() ? md->GetMR()->get_planes() : nullptr;
		if (!planes || planes->size() != 6) return;
		read_planes(n, planes);
		read_item_clip(n, md);
		if (k.D(2, n, "transparency", t, val))
			md->SetFloat(val, MESH_FLOAT_ALPHA);
	};

	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer)
			continue;
		switch (layer->IsA())
		{
		case 5://group
		{
			DataGroup* group = (DataGroup*)layer;
			const std::string n = group->GetName().ToStdString();
			if (k.B(5, n, "display", t, bval))
				group->SetDisp(bval);
			ReadClipLayer(k, 5, n, t, group);
			for (int j = 0; j < group->GetVolumeNum(); j++)
				if (VolumeData* vd = group->GetVolumeData(j))
					read_volume(vd);
		}
		break;
		case 3://mesh data
			read_mesh((MeshData*)layer);
			break;
		case 6://mesh group
		{
			MeshGroup* group = (MeshGroup*)layer;
			const std::string n = group->GetName().ToStdString();
			if (k.B(6, n, "display", t, bval))
				group->SetDisp(bval);
			ReadClipLayer(k, 6, n, t, group);
			for (int j = 0; j < group->GetMeshNum(); j++)
				if (MeshData* md = group->GetMeshData(j))
					read_mesh(md);
		}
		break;
		}
	}

	//for the view
	//translation
	double tx, ty, tz;
	bx = k.D(1, vname, "translation_x", t, tx);
	by = k.D(1, vname, "translation_y", t, ty);
	bz = k.D(1, vname, "translation_z", t, tz);
	if (bx && by && bz)
	{
		m_transx = tx; m_transy = ty; m_transz = tz;
		m_distance = std::sqrt(tx * tx + ty * ty + tz * tz);
	}
	//centers
	bx = k.D(1, vname, "center_x", t, tx);
	by = k.D(1, vname, "center_y", t, ty);
	bz = k.D(1, vname, "center_z", t, tz);
	if (bx && by && bz)
		SetCenters(tx, ty, tz);
	//obj translation
	bx = k.D(1, vname, "obj_trans_x", t, tx);
	by = k.D(1, vname, "obj_trans_y", t, ty);
	bz = k.D(1, vname, "obj_trans_z", t, tz);
	if (bx && by && bz)
		SetObjTrans(tx, ty, tz);
	//scale
	double scale;
	if (k.D(1, vname, "scale", t, scale))
		SetScaleFactor(scale);
	//rotation
	if (k.Q(1, vname, "rotation", t, q))
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		SetRotations(rotx, roty, rotz, true);
	}
	//intermixing mode
	int ival;
	if (k.I(1, vname, "volmethod", t, ival))
		SetVolMethod(ival);

	LayersChanged(false);
	Q_EMIT paramsApplied(t);
}

void RenderCanvas::AddKeyFrame(Interpolator* interpolator, double duration, int interpolation, bool record_volume_params)
{
	if (!interpolator)
		return;
	Keys k{ interpolator, m_view_name.toStdString() };
	const std::string& vname = k.view;

	double rotx, roty, rotz;
	int ix, iy, iz;
	Quaternion q;

	double t = interpolator->GetLastT();
	t = t < 0.0 ? 0.0 : t + duration;

	interpolator->Begin(t);

	auto add_planes = [&](const std::string& n, std::vector<Plane*>* planes) {
		if (planes && planes->size() == 6)
			for (int p = 0; p < 6; p++)
				k.AD(2, n, kPlaneKeys[p], (*planes)[p]->GetParam());
	};
	auto add_item_clip = [&](const std::string& n, auto* item) {
		k.AB(2, n, "clip_link_x", item->GetClippingLinkX());
		k.AB(2, n, "clip_link_y", item->GetClippingLinkY());
		k.AB(2, n, "clip_link_z", item->GetClippingLinkZ());
		item->GetClippingPlaneRotationsRaw(rotx, roty, rotz);
		q.FromEuler(rotx, roty, rotz);
		k.AQ(2, n, "clip_rotation", q);
		item->GetClipDistance(ix, iy, iz);
		k.AI(2, n, "clip_dist_x", ix);
		k.AI(2, n, "clip_dist_y", iy);
		k.AI(2, n, "clip_dist_z", iz);
	};
	auto add_mesh = [&](MeshData* md) {
		const std::string n = md->GetName().ToStdString();
		k.AB(2, n, "display", md->GetDisp());
		add_planes(n, md->GetMR() ? md->GetMR()->get_planes() : nullptr);
		add_item_clip(n, md);
		if (record_volume_params && md->GetMR())
			k.AD(2, n, "transparency", md->GetMR()->get_alpha());
	};

	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer)
			continue;
		switch (layer->IsA())
		{
		case 5://group
		{
			DataGroup* group = (DataGroup*)layer;
			const std::string gn = group->GetName().ToStdString();
			k.AB(5, gn, "display", group->GetDisp());
			AddClipLayer(k, 5, gn, group);
			for (int j = 0; j < group->GetVolumeNum(); j++)
			{
				VolumeData* vd = group->GetVolumeData(j);
				if (!vd) continue;
				const std::string n = vd->GetName().ToStdString();
				k.AB(2, n, "display", vd->GetDisp());
				add_planes(n, vd->GetVR() ? vd->GetVR()->get_planes() : nullptr);
				add_item_clip(n, vd);
				if (record_volume_params)
				{
					k.AD(2, n, "gamma", vd->Get3DGamma());
					k.AD(2, n, "saturation", vd->GetOffset());
					k.AD(2, n, "luminance", vd->GetLuminance());
					k.AB(2, n, "alpha_enable", vd->GetEnableAlpha());
					k.AD(2, n, "alpha", vd->GetAlpha());
					k.AB(2, n, "shading_enable", vd->GetShading());
					k.AD(2, n, "shading", vd->GetLowShading());
					k.AD(2, n, "boundary", vd->GetBoundary());
					k.AD(2, n, "left_threshold", vd->GetLeftThresh());
					k.AD(2, n, "right_threshold", vd->GetRightThresh());
					k.AB(2, n, "inv", vd->GetInvert());
				}
			}
		}
		break;
		case 3://mesh data
			add_mesh((MeshData*)layer);
			break;
		case 6://mesh group
		{
			MeshGroup* group = (MeshGroup*)layer;
			const std::string gn = group->GetName().ToStdString();
			// wx writes the mesh-group keys with l1 = 5 (see the file comment)
			k.AB(5, gn, "display", group->GetDisp());
			AddClipLayer(k, 5, gn, group);
			for (int j = 0; j < group->GetMeshNum(); j++)
				if (MeshData* md = group->GetMeshData(j))
					add_mesh(md);
		}
		break;
		}
	}

	//for the view
	k.AB(1, vname, "link_clipping_planes", GetSyncClippingPlanes());
	for (int p = 0; p < 6; p++)
		k.AD(1, vname, kPlaneKeys[p], GetLinkedParam(p));
	k.AB(1, vname, "clip_link_x", GetClippingLinkX());
	k.AB(1, vname, "clip_link_y", GetClippingLinkY());
	k.AB(1, vname, "clip_link_z", GetClippingLinkZ());
	k.AQ(1, vname, "clip_rotation", m_q_cl);
	GetClipDistance(ix, iy, iz);
	k.AI(1, vname, "clip_dist_x", ix);
	k.AI(1, vname, "clip_dist_y", iy);
	k.AI(1, vname, "clip_dist_z", iz);

	//rotation (wx glview GetRotations gives 360 - m_rotx)
	q.FromEuler(-(360.0 - m_rotx), m_roty, m_rotz);
	k.AQ(1, vname, "rotation", q);
	//translation
	k.AD(1, vname, "translation_x", m_transx);
	k.AD(1, vname, "translation_y", m_transy);
	k.AD(1, vname, "translation_z", m_transz);
	//centers
	k.AD(1, vname, "center_x", m_ctrx);
	k.AD(1, vname, "center_y", m_ctry);
	k.AD(1, vname, "center_z", m_ctrz);
	//obj traslation
	k.AD(1, vname, "obj_trans_x", m_obj_transx);
	k.AD(1, vname, "obj_trans_y", m_obj_transy);
	k.AD(1, vname, "obj_trans_z", m_obj_transz);
	//scale
	k.AD(1, vname, "scale", m_scale_factor);
	//intermixing mode
	k.AI(1, vname, "volmethod", GetVolMethod());

	interpolator->End();

	FlKeyGroup* group = interpolator->GetKeyGroup(interpolator->GetLastIndex());
	if (group)
		group->type = interpolation;
}

// ---------------------------------------------------------------------------
// animated camera move (wx VRenderVulkanView::StartManipulation /
// EndManipulation / SetManipKey / SetManipParams / PreDraw). Two key groups
// -- the current state at t = 0 and the targets at t = m_manip_time *
// m_manip_fps / 1000 frames (smooth) -- are played back one frame per drawn
// frame; each frame of the move gets m_manip_time ms of streaming time.
// ---------------------------------------------------------------------------

void RenderCanvas::StartManipulation(const Point* view_trans, const Point* view_center, const Point* view_rot,
	const Point* obj_trans, const double* scale)
{
	MakeCurrent();
	if (m_capture) return;   // wx: if (m_capture) return;

	if (!m_manip_interpolator)
		m_manip_interpolator = std::make_shared<Interpolator>();

	m_saved_uptime = TextureRenderer::get_up_time();
	TextureRenderer::set_up_time(m_manip_time);

	m_manip_interpolator->Clear();

	SetManipKey(0, 1, nullptr, nullptr, nullptr, nullptr, nullptr);

	double t = m_manip_time * m_manip_fps * 0.001;

	SetManipKey(t, 1, view_trans, view_center, view_rot, obj_trans, scale);

	m_manip = true;
	m_manip_cur_num = 0;
	m_manip_end_frame = t;
	RequestRender();   // wx: OnMouse RefreshGL() after the double click
}

void RenderCanvas::EndManipulation()
{
	MakeCurrent();
	TextureRenderer::set_up_time(m_saved_uptime);
	if (m_manip_interpolator)
		m_manip_interpolator->Clear();

	m_manip = false;
	m_manip_cur_num = 0;
	TextureRenderer::set_done_update_loop(true);
}

void RenderCanvas::SetManipKey(double t, int interpolation, const Point* view_trans, const Point* view_center,
	const Point* view_rot, const Point* obj_trans, const double* scale)
{
	Interpolator* ip = m_manip_interpolator.get();
	if (!ip)
		return;
	Keys k{ ip, m_view_name.toStdString() };
	const std::string& vname = k.view;

	ip->Begin(t);

	//for all volumes (wx: the data manager's; keys of volumes outside the
	//view are never read back)
	for (VolumeData* vd : m_volumes)
	{
		if (!vd) continue;
		const std::string n = vd->GetName().ToStdString();
		//display
		k.AB(2, n, "display", vd->GetDisp());
		//clipping planes
		std::vector<Plane*>* planes = vd->GetVR() ? vd->GetVR()->get_planes() : nullptr;
		if (!planes || planes->size() != 6)
			continue;
		for (int p = 0; p < 6; p++)
			k.AD(2, n, kPlaneKeys[p], (*planes)[p]->GetParam());
	}
	//for the view
	//link_planes (wx: ClippingView::GetChannLink, i.e. the view's sync flag)
	k.AB(1, vname, "link_planes", m_sync_clipping_planes);
	//rotation
	double rotx, roty, rotz;
	if (!view_rot) GetRotations(rotx, roty, rotz);
	else { rotx = view_rot->x(); roty = view_rot->y(); rotz = view_rot->z(); }
	Quaternion q;
	q.FromEuler(rotx, roty, rotz);
	k.AQ(1, vname, "rotation", q);
	//translation
	double tx, ty, tz;
	if (!view_trans) GetTranslations(tx, ty, tz);
	else { tx = view_trans->x(); ty = view_trans->y(); tz = view_trans->z(); }
	k.AD(1, vname, "translation_x", tx);
	k.AD(1, vname, "translation_y", ty);
	k.AD(1, vname, "translation_z", tz);
	//centers
	if (!view_center) GetCenters(tx, ty, tz);
	else { tx = view_center->x(); ty = view_center->y(); tz = view_center->z(); }
	k.AD(1, vname, "center_x", tx);
	k.AD(1, vname, "center_y", ty);
	k.AD(1, vname, "center_z", tz);
	//obj traslation
	if (!obj_trans) GetObjTrans(tx, ty, tz);
	else { tx = obj_trans->x(); ty = obj_trans->y(); tz = obj_trans->z(); }
	k.AD(1, vname, "obj_trans_x", tx);
	k.AD(1, vname, "obj_trans_y", ty);
	k.AD(1, vname, "obj_trans_z", tz);
	//scale
	double scale_factor = scale ? *scale : m_scale_factor;
	k.AD(1, vname, "scale", scale_factor);

	ip->End();

	FlKeyGroup* group = ip->GetKeyGroup(ip->GetLastIndex());
	if (group)
		group->type = interpolation;
}

void RenderCanvas::SetManipParams(double t)
{
	Interpolator* ip = m_manip_interpolator.get();
	if (!ip)
		return;
	Keys k{ ip, m_view_name.toStdString() };
	const std::string& vname = k.view;

	//for the view
	bool bval;
	if (k.B(1, vname, "link_planes", t, bval))
		SetSyncClippingPlanes(bval);   // wx clip_view->SetChannLink(bval)

	for (VolumeData* vd : m_volumes)
	{
		if (!vd) continue;
		const std::string n = vd->GetName().ToStdString();
		//display
		if (k.B(2, n, "display", t, bval))
			vd->SetDisp(bval);
		//clipping planes
		std::vector<Plane*>* planes = vd->GetVR() ? vd->GetVR()->get_planes() : nullptr;
		if (!planes || planes->size() != 6) continue;
		double val = 0;
		for (int p = 0; p < 6; p++)
		{
			if (k.D(2, n, kPlaneKeys[p], t, val))
			{
				(*planes)[p]->SetParam(val);
				if (m_sync_clipping_planes)   // wx: clip_view->GetChannLink()
					SetLinkedParam(p, val);
			}
		}
	}

	bool bx, by, bz;
	double tx, ty, tz;
	//translation
	bx = k.D(1, vname, "translation_x", t, tx);
	by = k.D(1, vname, "translation_y", t, ty);
	bz = k.D(1, vname, "translation_z", t, tz);
	if (bx && by && bz)
		SetTranslations(tx, ty, tz);
	//centers
	bx = k.D(1, vname, "center_x", t, tx);
	by = k.D(1, vname, "center_y", t, ty);
	bz = k.D(1, vname, "center_z", t, tz);
	if (bx && by && bz)
		SetCenters(tx, ty, tz);
	//obj translation
	bx = k.D(1, vname, "obj_trans_x", t, tx);
	by = k.D(1, vname, "obj_trans_y", t, ty);
	bz = k.D(1, vname, "obj_trans_z", t, tz);
	if (bx && by && bz)
		SetObjTrans(tx, ty, tz);
	//scale (wx m_vrv->SetScaleFactor: slider + text)
	double scale;
	if (k.D(1, vname, "scale", t, scale))
		SetScaleFactor(scale);
	//rotation
	Quaternion q;
	if (k.Q(1, vname, "rotation", t, q))
	{
		double rotx, roty, rotz;
		q.ToEuler(rotx, roty, rotz);
		SetRotations(rotx, roty, rotz, true);
	}

	// wx: clip_view->SetVolumeData(GetCurSelVol()); UpdateTree; (and
	// RecorderDlg::SetSelection(index of t in the manipulation keys), which
	// selected a row of the Record/Export key list -- not done here)
	Q_EMIT manipParamsApplied();
}

// wx VRenderVulkanView::PreDraw: one step of the manipulation per drawn
// frame (with memory swap only once the frame before has finished: wx
// m_pre_draw, set at the end of DrawScene while m_manip)
void RenderCanvas::PreDraw()
{
	//skip if not done with loop
	if (TextureRenderer::get_mem_swap())
	{
		if (m_pre_draw)
			m_pre_draw = false;
		else
			return;
	}

	if (m_manip)
	{
		if (m_manip_cur_num <= m_manip_end_frame)
		{
			SetManipParams(m_manip_cur_num);

			m_manip_cur_num++;
		}
		else
		{
			SetManipParams(m_manip_end_frame);
			EndManipulation();
		}
	}
}
