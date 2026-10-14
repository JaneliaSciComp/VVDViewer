// RenderCanvasCamera.cpp -- camera modes, keyboard navigation, clipping
// planes and the view-level 2D adjustment of the Qt render canvas.
//
// Ports of VRenderVulkanView (VRenderView.cpp):
//   HandleProjection / HandleCamera free-fly branches -> FreeScaleFactor,
//     CalcCameraDistance (the branches themselves are in RenderCanvas.cpp)
//   SetPersp, SetFree, SetCenter, SetScale121
//   OnMouse (dragging, m_int_mode 1; wheel)      -> OnCameraDrag, OnCameraWheel
//   OnIdle key-state polling (application-wide)  -> ViewKeyFilter, OnViewKey
//   SetRotations link_update / m_linked_rot      -> PropagateLinkedRotation,
//     LinkAllRotations (VRenderView::OnRotLink)
//   SetSyncClippingPlanes, SetClipMode, SetClippingPlaneRotations,
//   GetCameraSettings, A2QSync / Q2ASync, A2Q / Q2A plane transforms,
//   RestorePlanes, CalcAndSetCombinedClippingPlanes, DrawClippingPlanes
//   m_gamma / m_brightness / m_hdr / m_levels / m_easy_2d_adjust (the
//     params_final 2D adjustment of DrawOVER / DrawMIP / DrawVolumesMulti)
//
// The Qt canvas has one flat volume list (no DataGroup / MeshGroup): where
// the wx view walks group layers in the non-synchronised clipping branches,
// this walks every volume / mesh of the canvas (the single wx group).

#include "RenderCanvas.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "VVulkan.h"
#include "Vulkan/Vulkan2dRender.h"
#include "Vulkan/VulkanDevice.hpp"
#include "FLIVR/TextureRenderer.h"
#include "FLIVR/VolumeRenderer.h"
#include "FLIVR/MeshRenderer.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/Transform.h"
#include "FLIVR/Plane.h"
#include "DataManager.h"

using namespace FLIVR;

namespace
{
	inline double DegToRad(double d) { return d * 3.14159265358979323846 / 180.0; }

	constexpr int kFrontFace = 1;   // VRenderView.h FRONT_FACE
	constexpr int kBackFace = 2;    // VRenderView.h BACK_FACE
	// ClippingView.h plane display modes
	constexpr int PM_NORMAL = 0, PM_FRAME = 1, PM_LOWTRANS = 2, PM_LOWTRANSBACK = 3, PM_NORMALBACK = 4;

	std::vector<RenderCanvas*>& Canvases()
	{
		static std::vector<RenderCanvas*> list;
		return list;
	}

	// VRenderView.cpp: UpdateV2dObject (per-frame host-visible rings)
	template <typename VTX>
	void UpdateV2dObject(vks::VulkanDevice* dev, Vulkan2dRender::V2dObject& obj,
		const std::vector<VTX>& vertex, const std::vector<uint32_t>& index)
	{
		VkDeviceSize vsize = vertex.size() * sizeof(VTX);
		VkDeviceSize isize = index.size() * sizeof(uint32_t);
		dev->GetNextHostVertexBuffer(vsize, obj.vertBuf, obj.vertOffset);
		memcpy((char*)obj.vertBuf.mapped + obj.vertOffset, vertex.data(), vsize);
		dev->GetNextHostIndexBuffer(isize, obj.idxBuf, obj.idxOffset);
		memcpy((char*)obj.idxBuf.mapped + obj.idxOffset, index.data(), isize);
		obj.vertCount = vertex.size();
		obj.idxCount = index.size();
	}

	// VRenderVulkanView::SetBackgroundColor: m_bg_color_inv (the default
	// SettingDlg text colour, "background inverted")
	Color InvertedBackground(const Color& bg)
	{
		HSVColor bg_color(bg);
		if (bg_color.val() > 0.7 && bg_color.sat() > 0.7)
			return Color(HSVColor(bg_color.hue() + 180.0, 1.0, 1.0));
		else if (bg_color.val() > 0.7)
			return Color(0.0, 0.0, 0.0);
		return Color(1.0, 1.0, 1.0);
	}

	Transform SwappedTransform(Transform* tform)
	{
		Transform tform_copy;
		double mvmat[16];
		tform->get_trans(mvmat);
		std::swap(mvmat[3], mvmat[12]);
		std::swap(mvmat[7], mvmat[13]);
		std::swap(mvmat[11], mvmat[14]);
		tform_copy.set(mvmat);
		return tform_copy;
	}

	void SetUnitPlanes(std::vector<Plane*>* planes)
	{
		(*planes)[0]->ChangePlane(Point(0.0, 0.0, 0.0), Vector(1.0, 0.0, 0.0));
		(*planes)[1]->ChangePlane(Point(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0));
		(*planes)[2]->ChangePlane(Point(0.0, 0.0, 0.0), Vector(0.0, 1.0, 0.0));
		(*planes)[3]->ChangePlane(Point(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0));
		(*planes)[4]->ChangePlane(Point(0.0, 0.0, 0.0), Vector(0.0, 0.0, 1.0));
		(*planes)[5]->ChangePlane(Point(0.0, 0.0, 1.0), Vector(0.0, 0.0, -1.0));
	}

	void SetPlaneRanges(std::vector<Plane*>* planes)
	{
		(*planes)[0]->SetRange((*planes)[0]->get_point(), (*planes)[0]->normal(), (*planes)[1]->get_point(), (*planes)[1]->normal());
		(*planes)[1]->SetRange((*planes)[1]->get_point(), (*planes)[1]->normal(), (*planes)[0]->get_point(), (*planes)[0]->normal());
		(*planes)[2]->SetRange((*planes)[2]->get_point(), (*planes)[2]->normal(), (*planes)[3]->get_point(), (*planes)[3]->normal());
		(*planes)[3]->SetRange((*planes)[3]->get_point(), (*planes)[3]->normal(), (*planes)[2]->get_point(), (*planes)[2]->normal());
		(*planes)[4]->SetRange((*planes)[4]->get_point(), (*planes)[4]->normal(), (*planes)[5]->get_point(), (*planes)[5]->normal());
		(*planes)[5]->SetRange((*planes)[5]->get_point(), (*planes)[5]->normal(), (*planes)[4]->get_point(), (*planes)[4]->normal());
	}

	std::vector<Plane*>* VolumePlanes(VolumeData* vd)
	{
		if (!vd || !vd->GetVR()) return nullptr;
		std::vector<Plane*>* planes = vd->GetVR()->get_planes();
		return (planes && planes->size() == 6) ? planes : nullptr;
	}

	std::vector<Plane*>* MeshPlanes(MeshData* md)
	{
		if (!md || !md->GetMR()) return nullptr;
		std::vector<Plane*>* planes = md->GetMR()->get_planes();
		return (planes && planes->size() == 6) ? planes : nullptr;
	}

	double Wrap360(double a)
	{
		if (a > 360.0) a -= 360.0;
		if (a < 0.0) a += 360.0;
		return a;
	}
}

// ---------------------------------------------------------------------------
// canvases (wx: vr_frame->GetView(i) for linked rotations)
// ---------------------------------------------------------------------------

const std::vector<RenderCanvas*>& RenderCanvas::AllCanvases()
{
	return Canvases();
}

void RenderCanvas::RegisterCanvas(RenderCanvas* c, bool add)
{
	auto& list = Canvases();
	list.erase(std::remove(list.begin(), list.end(), c), list.end());
	if (add) list.push_back(c);
}

void RenderCanvas::SetLinkedRot(bool b)
{
	m_linked_rot = b;
}

// VRenderView::OnRotLink(wxCommandEvent&)
void RenderCanvas::LinkAllRotations(bool link)
{
	if (!link)
	{
		m_linked_rot = false;
		return;
	}
	m_linked_rot = true;
	const double rx = m_rotx, ry = m_roty, rz = m_rotz;
	for (RenderCanvas* c : Canvases())
	{
		if (!c) continue;
		c->m_linked_rot = true;
		c->SetRotations(rx, ry, rz, false);
	}
}

// VRenderVulkanView::SetRotations, link_update branch
void RenderCanvas::PropagateLinkedRotation()
{
	for (RenderCanvas* c : Canvases())
		if (c && c != this && c->m_linked_rot)
			c->SetRotations(m_rotx, m_roty, m_rotz, false);
}

// ---------------------------------------------------------------------------
// 2D adjustment
// ---------------------------------------------------------------------------

int RenderCanvas::Get2DAdjust(VolumeData* vd, glm::vec4* loc) const
{
	const Color gamma = vd ? vd->GetGamma() : m_gamma;
	if (m_easy_2d_adjust)
	{
		const Color levels = vd ? vd->GetLevels() : m_levels;
		loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
		loc[1] = glm::vec4((float)levels.r(), (float)levels.g(), (float)levels.b(), 1.0f);
		loc[2] = glm::vec4(0.0f);
		return IMG_SHDR_LEVELS;
	}
	const Color brightness = vd ? vd->GetBrightness() : m_brightness;
	const Color hdr = vd ? vd->GetHdr() : m_hdr;
	loc[0] = glm::vec4((float)gamma.r(), (float)gamma.g(), (float)gamma.b(), 1.0f);
	loc[1] = glm::vec4((float)brightness.r(), (float)brightness.g(), (float)brightness.b(), 1.0f);
	loc[2] = glm::vec4((float)hdr.r(), (float)hdr.g(), (float)hdr.b(), 0.0f);
	return IMG_SHDR_BRIGHTNESS_CONTRAST_HDR;
}

// ---------------------------------------------------------------------------
// perspective / free fly
// ---------------------------------------------------------------------------

void RenderCanvas::RestoreFreeSaved()
{
	//restore camera translation
	m_transx = m_transx_saved;
	m_transy = m_transy_saved;
	m_transz = m_transz_saved;
	m_ctrx = m_ctrx_saved;
	m_ctry = m_ctry_saved;
	m_ctrz = m_ctrz_saved;
	//restore camera rotation
	m_rotx = m_rotx_saved;
	m_roty = m_roty_saved;
	m_rotz = m_rotz_saved;
	//restore object translation
	m_obj_transx = m_obj_transx_saved;
	m_obj_transy = m_obj_transy_saved;
	m_obj_transz = m_obj_transz_saved;
	//restore scale factor
	m_scale_factor = m_scale_factor_saved;
	Q_EMIT zoomChanged(m_scale_factor);

	SetRotations(m_rotx, m_roty, m_rotz);
}

// VRenderVulkanView::SetPersp
void RenderCanvas::SetPersp(bool persp)
{
	m_persp = persp;
	if (m_free && !m_persp)
	{
		m_free = false;
		RestoreFreeSaved();
		Q_EMIT freeModeChanged(false);
	}
	RequestRender();
}

// VRenderVulkanView::SetFree
void RenderCanvas::SetFree(bool free)
{
	const bool was = m_free;
	m_free = free;
	if (free)
	{
		m_persp = true;
		Vector pos(m_transx, m_transy, m_transz);
		Vector d = pos;
		d.normalize();
		Vector ctr;
		ctr = pos - m_radius * 0.01 * d;
		m_ctrx = ctr.x();
		m_ctry = ctr.y();
		m_ctrz = ctr.z();

		//save camera translation
		m_transx_saved = m_transx;
		m_transy_saved = m_transy;
		m_transz_saved = m_transz;
		m_ctrx_saved = m_ctrx;
		m_ctry_saved = m_ctry;
		m_ctrz_saved = m_ctrz;
		//save camera rotation
		m_rotx_saved = m_rotx;
		m_roty_saved = m_roty;
		m_rotz_saved = m_rotz;
		//save object translateion
		m_obj_transx_saved = m_obj_transx;
		m_obj_transy_saved = m_obj_transy;
		m_obj_transz_saved = m_obj_transz;
		//save scale factor
		m_scale_factor_saved = m_scale_factor;
	}
	else if (was)
	{
		m_ctrx = m_ctry = m_ctrz = 0.0;
		RestoreFreeSaved();
	}
	if (was != free)
		Q_EMIT freeModeChanged(free);
	RequestRender();
}

// VRenderVulkanView::HandleProjection, m_free branch
double RenderCanvas::FreeScaleFactor()
{
	const double r = m_radius / std::tan(DegToRad(m_aov / 2.0));
	return r / (CalcCameraDistance() + r / 12.0);
}

// VRenderVulkanView::CalcCameraDistance: eye distance to the scene box
double RenderCanvas::CalcCameraDistance()
{
	glm::mat4 mv_temp = m_mv_mat;
	//translate object
	mv_temp = glm::translate(mv_temp, glm::vec3(m_obj_transx, m_obj_transy, m_obj_transz));
	//rotate object
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotx), glm::vec3(1.0, 0.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_roty), glm::vec3(0.0, 1.0, 0.0));
	mv_temp = glm::rotate(mv_temp, float(m_obj_rotz), glm::vec3(0.0, 0.0, 1.0));
	//center object
	mv_temp = glm::translate(mv_temp, glm::vec3(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz));

	double mvmat[16] =
		{ mv_temp[0][0], mv_temp[0][1], mv_temp[0][2], mv_temp[0][3],
		  mv_temp[1][0], mv_temp[1][1], mv_temp[1][2], mv_temp[1][3],
		  mv_temp[2][0], mv_temp[2][1], mv_temp[2][2], mv_temp[2][3],
		  mv_temp[3][0], mv_temp[3][1], mv_temp[3][2], mv_temp[3][3] };

	Transform mv;
	mv.set_trans(mvmat);

	std::vector<Vector> points;
	points.reserve(8);
	points.push_back(Vector(m_bounds.min().x(), m_bounds.min().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.min().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.min().x(), m_bounds.max().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.max().y(), m_bounds.min().z()));
	points.push_back(Vector(m_bounds.min().x(), m_bounds.min().y(), m_bounds.max().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.min().y(), m_bounds.max().z()));
	points.push_back(Vector(m_bounds.min().x(), m_bounds.max().y(), m_bounds.max().z()));
	points.push_back(Vector(m_bounds.max().x(), m_bounds.max().y(), m_bounds.max().z()));

	static const int edges[12 * 2] = { 0, 1,  1, 3,  3, 2,  2, 0,
									   0, 4,  1, 5,  3, 7,  2, 6,
									   4, 5,  5, 7,  7, 6,  6, 4 };
	static const int planes[3 * 6] = { 0, 1, 2,  0, 1, 4,  1, 3, 5,  3, 2, 7,  2, 0, 6,  4, 5, 6 };

	for (int i = 0; i < 8; i++)
		points[i] = mv.project(points[i].asPoint()).asVector();

	double dmin = DBL_MAX;

	//point
	for (int i = 0; i < 8; i++)
	{
		double d = points[i].length2();
		if (d < dmin) dmin = d;
	}

	//edge
	for (int i = 0; i < 12; i++)
	{
		Vector p = points[edges[i * 2]];
		Vector q = points[edges[i * 2 + 1]];
		if (Dot(p, p - q) <= 0 || Dot(q, q - p) <= 0) continue;
		Vector e = q - p;
		e.safe_normalize();
		double d = Dot(p, e);
		d = p.length2() - d * d;
		if (d < dmin) dmin = d;
	}

	//planes
	for (int i = 0; i < 6; i++)
	{
		Vector o = points[planes[i * 3]];
		Vector p = points[planes[i * 3 + 1]] - points[planes[i * 3]];
		Vector q = points[planes[i * 3 + 2]] - points[planes[i * 3]];
		double plen = p.length();
		double qlen = q.length();
		p.safe_normalize();
		q.safe_normalize();
		Vector c = Cross(p, q);
		c.safe_normalize();

		double d = Dot(points[planes[i * 3 + 1]], c);
		Vector v = c * d - o;

		double s = Dot(v, p) / plen;
		double t = Dot(v, q) / qlen;

		d = d * d;
		if (s > 0 && s < 1 && t > 0 && t < 1 && d < dmin)
			dmin = d;
	}

	//inside?
	bool inside = false;
	{
		Vector ex = points[1] - points[0];
		Vector ey = points[2] - points[0];
		Vector ez = points[4] - points[0];
		double xlen = ex.length();
		double ylen = ey.length();
		double zlen = ez.length();
		ex.safe_normalize();
		ey.safe_normalize();
		ez.safe_normalize();

		Vector v = Vector(0) - points[0];

		double x = Dot(v, ex) / xlen;
		double y = Dot(v, ey) / ylen;
		double z = Dot(v, ez) / zlen;
		if (x > 0 && x < 1 && y > 0 && y < 1 && z > 0 && z < 1)
			inside = true;
	}

	dmin = inside ? 0 : std::sqrt(dmin);
	return dmin;
}

// ---------------------------------------------------------------------------
// Reset Transl. / 1:1
// ---------------------------------------------------------------------------

// VRenderVulkanView::SetCenter
void RenderCanvas::SetCenter()
{
	// InitView(INIT_BOUNDS | INIT_CENTER | INIT_OBJ_TRANSL)
	ComputeBounds();
	m_obj_transx = m_obj_transy = m_obj_transz = 0.0;

	std::vector<VolumeData*> vols;
	if (m_cur_vol)
		vols.push_back(m_cur_vol);
	else
		for (VolumeData* vd : PopVolumeList())
			if (vd && vd->GetDisp()) vols.push_back(vd);

	BBox bounds;
	for (VolumeData* vd : vols)
	{
		if (!vd->GetTexture() || !vd->GetVR())
			return;
		Transform* tform = vd->GetTexture()->transform();
		if (!tform)
			return;
		std::vector<Plane*>* planes = vd->GetVR()->get_planes();
		if (!planes || planes->size() != 6)
			return;

		double params[6];
		for (int j = 0; j < 6; j++)
			params[j] = (*planes)[j]->GetParam();

		Transform tform_copy = SwappedTransform(tform);

		Point p[8] = {
			Point(params[0],       params[2],       params[4]),
			Point(1.0 - params[1], params[2],       params[4]),
			Point(1.0 - params[1], 1.0 - params[3], params[4]),
			Point(params[0],       1.0 - params[3], params[4]),
			Point(params[0],       params[2],       1.0 - params[5]),
			Point(1.0 - params[1], params[2],       1.0 - params[5]),
			Point(1.0 - params[1], 1.0 - params[3], 1.0 - params[5]),
			Point(params[0],       1.0 - params[3], 1.0 - params[5])
		};

		double rotx, roty, rotz;
		Quaternion q_cl;
		Quaternion q_view_cl = m_q_cl;
		q_view_cl.ToEuler(rotx, roty, rotz);
		q_cl.FromEuler(-rotx, -roty, rotz);

		for (int j = 0; j < 8; j++)
		{
			p[j] = tform_copy.project(p[j]);
			Quaternion p2(p[j].x() - m_obj_ctrx, p[j].y() - m_obj_ctry, p[j].z() - m_obj_ctrz, 0.0);
			Quaternion p3 = q_cl * p2 * (-q_cl);
			p[j] = Point(p3.x + m_obj_ctrx, p3.y + m_obj_ctry, p3.z + m_obj_ctrz);
			bounds.extend(p[j]);
		}
	}
	if (!m_cur_vol)
	{
		PopMeshList();
		for (MeshData* md : m_md_pop_list)
			bounds.extend(md->GetBounds());
	}
	if (bounds.valid())
	{
		m_obj_transx += (bounds.min().x() + bounds.max().x()) / 2.0 - m_obj_ctrx;
		m_obj_transy += (bounds.min().y() + bounds.max().y()) / 2.0 - m_obj_ctry;
		m_obj_transz -= (bounds.min().z() + bounds.max().z()) / 2.0 - m_obj_ctrz;
	}
	RequestRender();
}

// VRenderVulkanView::SetScale121
void RenderCanvas::SetScale121()
{
	QSize s = DeviceSize();
	int nx = std::max(1, s.width());
	int ny = std::max(1, s.height());
	double aspect = (double)nx / (double)ny;

	double spc_x = 1.0, spc_y = 1.0, spc_z = 1.0;
	VolumeData* vd = m_cur_vol;
	if (!vd)
		for (VolumeData* v : PopVolumeList())
			if (v && v->GetDisp()) { vd = v; break; }
	if (vd)
		vd->GetSpacings(spc_x, spc_y, spc_z);
	spc_x = spc_x < 1e-6 ? 1.0 : spc_x;
	spc_y = spc_y < 1e-6 ? 1.0 : spc_y;

	if (aspect > 1.0)
		m_scale_factor = 2.0 * m_radius / spc_y / double(ny);
	else
		m_scale_factor = 2.0 * m_radius / spc_x / double(nx);
	Q_EMIT zoomChanged(m_scale_factor);
	RequestRender();
}

// ---------------------------------------------------------------------------
// mouse / keyboard (wx OnMouse dragging & wheel, OnIdle key state)
// ---------------------------------------------------------------------------

void RenderCanvas::OnCameraDrag(QMouseEvent* e)
{
	const QPoint p = e->pos();
	const QPoint old = m_last_pos;
	const QSize sz = size();
	const int w = std::max(1, sz.width()), h = std::max(1, sz.height());
	const bool ctrl = e->modifiers() & Qt::ControlModifier;
	const bool left = e->buttons() & Qt::LeftButton;
	bool hold_old = false;

	TextureRenderer::set_cor_up_time(int(std::sqrt(double(old.x() - p.x()) * double(old.x() - p.x()) +
		double(old.y() - p.y()) * double(old.y() - p.y()))));

	if (std::abs(old.x() - p.x()) + std::abs(old.y() - p.y()) < 200)
	{
		if (left && !ctrl)
		{
			FLIVR::Quaternion q_delta = Trackball(old.x(), p.y(), p.x(), old.y());
			if (m_rot_lock && q_delta.IsIdentity())
				hold_old = true;
			m_q *= q_delta;
			m_q.Normalize();
			UpdateCameraFromQ();

			Q2ASync();
			Q_EMIT rotationChanged(m_rotx, m_roty, m_rotz);

			m_interactive = m_adaptive;
			m_int_res = m_adaptive_res;

			if (m_linked_rot)
				PropagateLinkedRotation();
			if (!hold_old)
				RequestRender();
		}
		if ((e->buttons() & Qt::MiddleButton) || (ctrl && left))
		{
			long dx = p.x() - old.x();
			long dy = p.y() - old.y();

			Vector head(-m_transx, -m_transy, -m_transz);
			head.normalize();
			Vector side = Cross(m_up, head);
			Vector trans = -(
				side * (double(dx) * (m_ortho_right - m_ortho_left) / double(w)) +
				m_up * (double(-dy) * (m_ortho_top - m_ortho_bottom) / double(h)));
			m_obj_transx += trans.x();
			m_obj_transy += trans.y();
			m_obj_transz += trans.z();

			m_interactive = m_adaptive;
			m_int_res = m_adaptive_res;

			Q2ASync();
			RequestRender();
		}
		if (e->buttons() & Qt::RightButton)
		{
			long dx = p.x() - old.x();
			long dy = p.y() - old.y();
			double delta = std::abs(dx) > std::abs(dy) ?
				(double)dx / (double)w :
				(double)-dy / (double)h;

			if (m_free)
			{
				Vector pos(m_transx, m_transy, m_transz);
				pos.normalize();
				Vector ctr(m_ctrx, m_ctry, m_ctrz);
				ctr -= delta * pos * m_radius * 10;
				m_ctrx = ctr.x();
				m_ctry = ctr.y();
				m_ctrz = ctr.z();
				m_scale_factor = FreeScaleFactor();
			}
			else
				m_scale_factor += m_scale_factor * delta;
			Q_EMIT zoomChanged(m_scale_factor);

			m_interactive = m_adaptive;
			m_int_res = m_adaptive_res;
			RequestRender();
		}
	}

	//update mouse position
	if (!hold_old)
		m_last_pos = p;
}

void RenderCanvas::OnCameraWheel(QWheelEvent* e)
{
	const int wheel = e->angleDelta().y();
	if (!wheel) return;
	double delta = wheel / 1000.0;
	if (m_free)
	{
		Vector pos(m_transx, m_transy, m_transz);
		pos.normalize();
		Vector ctr(m_ctrx, m_ctry, m_ctrz);
		ctr -= delta * pos * m_radius * 10;
		m_ctrx = ctr.x();
		m_ctry = ctr.y();
		m_ctrz = ctr.z();
		m_scale_factor = FreeScaleFactor();
	}
	else
		m_scale_factor += delta;

	if (m_scale_factor < 0.01)
		m_scale_factor = 0.01;
	Q_EMIT zoomChanged(m_scale_factor);
	RequestRender();
}

// ---------------------------------------------------------------------------
// keyboard (wx VRenderVulkanView::OnIdle key-state polling)
//
// The wx view polls wxGetKeyState in OnIdle whenever FindFocus() is any
// window of the application that is not a text control ("editting") and the
// key lock is off -- the canvas does not need the focus. Each key acts once
// on its press edge (m_move_left, m_tseq_forward, m_clip_up, ... latches), so
// holding a key does not repeat. ViewKeyFilter is an application-wide event
// filter doing the same: it sees every key press / release of the
// application, ignores auto-repeats and text editors, and does not consume
// the event (the focused widget still gets it, as in wx).
// ---------------------------------------------------------------------------

namespace
{
	// wx OnIdle "editting": FindFocus() is a wxTextCtrl / wxDirPickerCtrl /
	// wxFilePickerCtrl (the Qt line edits, spin boxes and editable combos)
	bool FocusIsTextEditor()
	{
		QWidget* f = QApplication::focusWidget();
		if (!f) return false;
		if (qobject_cast<QLineEdit*>(f) || qobject_cast<QAbstractSpinBox*>(f) ||
			qobject_cast<QTextEdit*>(f) || qobject_cast<QPlainTextEdit*>(f))
			return true;
		if (QComboBox* cb = qobject_cast<QComboBox*>(f))
			return cb->isEditable();
		return false;
	}
}

class RenderCanvas::ViewKeyFilter : public QObject
{
public:
	explicit ViewKeyFilter(RenderCanvas* c) : QObject(c), m_canvas(c) { qApp->installEventFilter(this); }
	~ViewKeyFilter() override { qApp->removeEventFilter(this); }

protected:
	bool eventFilter(QObject* obj, QEvent* e) override
	{
		if (e->type() == QEvent::ApplicationStateChange)
		{
			// keys released while another application is active never arrive
			if (QGuiApplication::applicationState() != Qt::ApplicationActive)
			{
				m_canvas->SetKeyV(false);
				m_canvas->m_alt_v_down = m_canvas->m_ctrl_b_down = false;
			}
			return false;
		}
		if (e->type() != QEvent::KeyPress && e->type() != QEvent::KeyRelease)
			return false;
		auto* ke = static_cast<QKeyEvent*>(e);
		// an application filter sees the event once for every widget it is
		// propagated to: handle it once (same object, time stamp, type, key;
		// a stack-allocated event can reuse the address of the previous one)
		if (e == m_last && ke->timestamp() == m_last_ts && e->type() == m_last_type && ke->key() == m_last_key)
			return false;
		m_last = e;
		m_last_ts = ke->timestamp();
		m_last_type = e->type();
		m_last_key = ke->key();
		// popup menus own their keys (native menus in wx never reach OnIdle)
		if (QApplication::activePopupWidget())
			return false;
		// a modal dialog of the Qt port (wx dialogs are mostly modeless)
		if (QApplication::activeModalWidget())
			return false;
		const bool press = e->type() == QEvent::KeyPress;
		// a release always clears the latches (wx: !GetKeyState)
		if (press && FocusIsTextEditor())
			return false;
		Q_UNUSED(obj);
		m_canvas->OnViewKey(ke, press);
		return false;
	}

private:
	RenderCanvas* m_canvas;
	QEvent* m_last = nullptr;
	quint64 m_last_ts = 0;
	QEvent::Type m_last_type = QEvent::None;
	int m_last_key = 0;
};

void RenderCanvas::CreateViewKeyFilter()
{
	if (!m_key_filter)
		m_key_filter = new ViewKeyFilter(this);
}

bool RenderCanvas::OnViewKey(QKeyEvent* e, bool press)
{
	const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	const bool ctrl = mods & Qt::ControlModifier;
	const bool alt = mods & Qt::AltModifier;
	const int key = e->key();

	if (!press)
	{
		if (e->isAutoRepeat())
			return false;
		// wx: m_draw_mask = !GetKeyState('V'); latches of Alt+V / Ctrl+B
		if (key == Qt::Key_V) { SetKeyV(false); m_alt_v_down = false; return true; }
		if (key == Qt::Key_B) { m_ctrl_b_down = false; return true; }
		return false;
	}
	// held keys act on the press edge only (wx m_move_*, m_tseq_*, m_clip_* latches)
	if (e->isAutoRepeat())
	{
		switch (key)
		{
		case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
			return ctrl;
		case Qt::Key_D: case Qt::Key_Space: case Qt::Key_A: case Qt::Key_S: case Qt::Key_W: case Qt::Key_V:
		case Qt::Key_F: case Qt::Key_L:
			return !ctrl && !alt;
		default:
			return false;
		}
	}

	if (m_key_lock)
		return false;

	// wx OnIdle (this == cur_glview, m_tile_rendering): Esc ends tile rendering
	if (key == Qt::Key_Escape && m_tile_rendering && KeyActionsHere())
	{
		AbortCapture();
		return true;
	}

	// move view (Ctrl + arrows): wx int(0.8 * ortho extent) steps
	if (ctrl && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down))
	{
		Vector head(-m_transx, -m_transy, -m_transz);
		head.normalize();
		Vector side = Cross(m_up, head);
		Vector trans;
		switch (key)
		{
		case Qt::Key_Left:  trans = -(side * (int(0.8 * (m_ortho_right - m_ortho_left)))); break;
		case Qt::Key_Right: trans = side * (int(0.8 * (m_ortho_right - m_ortho_left))); break;
		case Qt::Key_Up:    trans = -m_up * (int(0.8 * (m_ortho_top - m_ortho_bottom))); break;
		default:            trans = m_up * (int(0.8 * (m_ortho_top - m_ortho_bottom))); break;
		}
		m_obj_transx += trans.x();
		m_obj_transy += trans.y();
		m_obj_transz += trans.z();
		RequestRender();
		return true;
	}

	// brush undo / redo (Ctrl+Z / Ctrl+Y): BrushTool's own application-wide
	// key filter runs them (wx OnIdle, once per press)
	if (mods == Qt::ControlModifier && (key == Qt::Key_Z || key == Qt::Key_Y))
		return true;

	// wx: Ctrl+B (no Shift / Alt) -> WarpCurrentVolume. wx calls it on every
	// OnIdle tick while the keys are held; here once per press.
	if (key == Qt::Key_B && mods == Qt::ControlModifier)
	{
		if (!KeyActionsHere()) return true;   // wx: this == cur_glview
		if (!m_ctrl_b_down)
		{
			m_ctrl_b_down = true;
			Q_EMIT warpRequested();
		}
		return true;
	}

	// wx: Alt+V -> CopyLevel(m_cur_vol) of a brxml volume + m_selector.SetVolume
	// (wx repeats it on every OnIdle tick while held; here once per press)
	if (key == Qt::Key_V && alt)
	{
		if (!KeyActionsHere()) return true;   // wx: this == cur_glview
		if (!m_alt_v_down)
		{
			m_alt_v_down = true;
			Q_EMIT copyLevelRequested();
		}
		return true;
	}

	if (ctrl || alt || (mods & Qt::MetaModifier))
		return false;

	switch (key)
	{
	case Qt::Key_V:
		// wx "draw_mask": while V is held the masks are not drawn
		SetKeyV(true);
		return true;
	// Several views: wx polls these in every view's OnIdle, so with two
	// views one press stepped the movie / moved the planes twice; the port
	// runs the pane action once, from the current view (KeyActionsHere)
	case Qt::Key_D:
	case Qt::Key_Space:
		if (KeyActionsHere()) Q_EMIT frameStepRequested(1);
		RequestRender();
		return true;
	case Qt::Key_A:
		if (KeyActionsHere()) Q_EMIT frameStepRequested(-1);
		RequestRender();
		return true;
	case Qt::Key_S:
		// wx m_clip_up: MoveLinkedClippingPlanes(1)
		if (KeyActionsHere()) Q_EMIT clipMoveRequested(1);
		RequestRender();
		return true;
	case Qt::Key_W:
		// wx m_clip_down: MoveLinkedClippingPlanes(0)
		if (KeyActionsHere()) Q_EMIT clipMoveRequested(0);
		RequestRender();
		return true;
	// wx OnIdle (this == cur_glview): f -> TraceDlg::CellFull, l ->
	// TraceDlg::CellLink(false) (Components & Tracking), once per press
	case Qt::Key_F:
		if (KeyActionsHere()) Q_EMIT cellFullRequested();
		RequestRender();
		return true;
	case Qt::Key_L:
		if (KeyActionsHere()) Q_EMIT cellLinkRequested();
		RequestRender();
		return true;
	default:
		break;
	}
	return false;
}

// ---------------------------------------------------------------------------
// clipping planes: view-level state
// ---------------------------------------------------------------------------

void RenderCanvas::SetSyncClippingPlanes(bool val, VolumeData* copy)
{
	m_sync_clipping_planes = val;
	if (!copy) return;
	copy->GetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
	copy->GetClippingFixParams(m_clip_mode, m_q_cl_zero, m_q_cl_fix, m_q_fix, m_rotx_cl_fix, m_roty_cl_fix, m_rotz_cl_fix,
		m_rotx_fix, m_roty_fix, m_rotz_fix, m_trans_fix);
	// wx copies the Euler angles only; m_q_cl follows on the next
	// SetClippingPlaneRotations / SetClipMode
	m_link_x_chk = copy->GetClippingLinkX();
	m_link_y_chk = copy->GetClippingLinkY();
	m_link_z_chk = copy->GetClippingLinkZ();
	std::vector<Plane*>* planes = VolumePlanes(copy);
	if (!planes) return;
	for (int i = 0; i < 6; i++)
		m_linked_plane_params[i] = (*planes)[i]->GetParam();
}

void RenderCanvas::SetSyncClippingPlanes(bool val, MeshData* copy)
{
	m_sync_clipping_planes = val;
	if (!copy) return;
	copy->GetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
	copy->GetClippingFixParams(m_clip_mode, m_q_cl_zero, m_q_cl_fix, m_q_fix, m_rotx_cl_fix, m_roty_cl_fix, m_rotz_cl_fix,
		m_rotx_fix, m_roty_fix, m_rotz_fix, m_trans_fix);
	m_link_x_chk = copy->GetClippingLinkX();
	m_link_y_chk = copy->GetClippingLinkY();
	m_link_z_chk = copy->GetClippingLinkZ();
	std::vector<Plane*>* planes = MeshPlanes(copy);
	if (!planes) return;
	for (int i = 0; i < 6; i++)
		m_linked_plane_params[i] = (*planes)[i]->GetParam();
}

// VRenderVulkanView::SyncClippingPlaneRotations
void RenderCanvas::SyncClippingPlaneRotations(bool force)
{
	if (!m_sync_clipping_planes && !force)
		return;
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 5:
		{
			DataGroup* group = (DataGroup*)layer;
			group->SetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
			for (int j = 0; j < group->GetVolumeNum(); j++)
				if (VolumeData* vd = group->GetVolumeData(j))
					vd->SetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
		}
		break;
		case 6:
		{
			MeshGroup* group = (MeshGroup*)layer;
			group->SetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
			for (int j = 0; j < group->GetMeshNum(); j++)
				if (MeshData* md = group->GetMeshData(j))
					md->SetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
		}
		break;
		}
	}
}

void RenderCanvas::GetCameraSettings(double& rx, double& ry, double& rz, Vector& obj_ctr, Vector& obj_trans) const
{
	rx = m_rotx;
	ry = m_roty;
	rz = m_rotz;
	obj_ctr = Vector(m_obj_ctrx, m_obj_ctry, m_obj_ctrz);
	obj_trans = Vector(m_obj_transx, m_obj_transy, m_obj_transz);
}

// VRenderVulkanView::SetClipMode
void RenderCanvas::SetClipMode(int mode)
{
	switch (mode)
	{
	case 0:
		m_clip_mode = 0;
		// RestorePlanes
		for (VolumeData* vd : PopVolumeList())
		{
			if (!vd || !vd->GetDisp()) continue;
			if (std::vector<Plane*>* planes = VolumePlanes(vd))
				for (Plane* p : *planes) p->Restore();
		}
		m_rotx_cl = 0;
		m_roty_cl = 0;
		m_rotz_cl = 0;
		break;
	case 1:
		m_clip_mode = 1;
		SetRotations(m_rotx, m_roty, m_rotz);
		break;
	case 2:
		m_clip_mode = 2;
		m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
		m_q_cl = m_q_cl_zero;
		m_q_cl.ToEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
		if (m_rotx_cl > 180.0) m_rotx_cl -= 360.0;
		if (m_roty_cl > 180.0) m_roty_cl -= 360.0;
		if (m_rotz_cl > 180.0) m_rotz_cl -= 360.0;
		SetRotations(m_rotx, m_roty, m_rotz);
		break;
	case 3:
		m_clip_mode = 3;
		m_rotx_cl_fix = m_rotx_cl;
		m_roty_cl_fix = m_roty_cl;
		m_rotz_cl_fix = m_rotz_cl;
		m_rotx_fix = m_rotx;
		m_roty_fix = m_roty;
		m_rotz_fix = m_rotz;
		m_q_cl_fix.FromEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
		m_q_fix.FromEuler(m_rotx, m_roty, m_rotz);
		m_q_fix.z *= -1;
		m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
		m_trans_fix = Vector(m_obj_transx, m_obj_transy, -m_obj_transz);
		break;
	case 4:
		m_clip_mode = 2;
		m_rotx_cl = m_rotx_cl_fix;
		m_roty_cl = m_roty_cl_fix;
		m_rotz_cl = m_rotz_cl_fix;
		m_rotx = m_rotx_fix;
		m_roty = m_roty_fix;
		m_rotz = m_rotz_fix;
		m_q_cl.FromEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
		m_q_cl.Normalize();
		SetRotations(m_rotx, m_roty, m_rotz);
		break;
	}
	RequestRender();
}

// VRenderVulkanView::SetClippingPlaneRotations
void RenderCanvas::SetClippingPlaneRotations(double rx, double ry, double rz)
{
	m_rotx_cl = rx;
	m_roty_cl = ry;
	m_rotz_cl = -rz;

	m_q_cl.FromEuler(m_rotx_cl, m_roty_cl, m_rotz_cl);
	m_q_cl.Normalize();

	SetRotations(m_rotx, m_roty, m_rotz);
}

// ---------------------------------------------------------------------------
// clipping planes: rotation <-> plane transforms
// ---------------------------------------------------------------------------

// the plane part shared by VRenderVulkanView::A2Q and ::Q2A (sync mode)
void RenderCanvas::ApplyClipPlaneTransforms()
{
	if (!m_clip_mode)
		return;
	if (m_clip_mode == 1)
		m_q_cl.FromEuler(-m_rotx, -m_roty, m_rotz);
	else if (m_clip_mode == 3)
	{
		m_q_cl_zero.FromEuler(-m_rotx, -m_roty, m_rotz);
		m_q_cl = m_q_fix * m_q_cl_zero;
	}

	for (VolumeData* vd : PopVolumeList())
	{
		if (!vd || !vd->GetDisp())
			continue;
		if (!vd->GetTexture())
			continue;
		Transform* tform = vd->GetTexture()->transform();
		if (!tform)
			continue;
		Transform tform_copy = SwappedTransform(tform);

		double rotx, roty, rotz;
		Quaternion q_cl, q_cl_fix;
		m_q_cl.ToEuler(rotx, roty, rotz);
		q_cl.FromEuler(-rotx, -roty, rotz);
		m_q_cl_fix.ToEuler(rotx, roty, rotz);
		q_cl_fix.FromEuler(-rotx, -roty, rotz);

		std::vector<Plane*>* planes = VolumePlanes(vd);
		if (!planes)
			continue;
		vd->GetVR()->set_clip_quaternion(m_q_cl);

		Vector trans1(-m_obj_ctrx, -m_obj_ctry, -m_obj_ctrz);
		Vector trans2(m_obj_ctrx, m_obj_ctry, m_obj_ctrz);
		Vector n;
		Point p;

		if (m_clip_mode == 3)
		{
			Vector obj_trans1 = m_trans_fix;
			Vector obj_trans2 = -obj_trans1;
			Vector obj_trans3 = obj_trans1 - Vector(m_obj_transx, m_obj_transy, m_obj_transz);

			for (int i = 0; i < 6; i++)
			{
				(*planes)[i]->Restore();

				p = (*planes)[i]->get_point();
				n = (*planes)[i]->normal();
				p = tform_copy.project(p);
				n = tform->unproject(n);
				n.safe_normalize();
				(*planes)[i]->ChangePlaneTemp(p, n);

				(*planes)[i]->Translate(trans1);
				(*planes)[i]->Rotate(q_cl_fix);
				(*planes)[i]->Translate(obj_trans1);
				(*planes)[i]->Rotate(q_cl);
				(*planes)[i]->Translate(obj_trans2);
				(*planes)[i]->Translate(obj_trans3);
				(*planes)[i]->Translate(trans2);

				p = (*planes)[i]->get_point();
				n = (*planes)[i]->normal();
				p = tform_copy.unproject(p);
				n = tform->project(n);
				n.safe_normalize();
				(*planes)[i]->ChangePlaneTemp(p, n);
			}
		}
		else
		{
			for (int i = 0; i < 6; i++)
			{
				(*planes)[i]->Restore();

				p = (*planes)[i]->get_point();
				n = (*planes)[i]->normal();
				p = tform_copy.project(p);
				n = tform->unproject(n);
				n.safe_normalize();
				(*planes)[i]->ChangePlaneTemp(p, n);

				(*planes)[i]->Translate(trans1);
				(*planes)[i]->Rotate(q_cl);
				(*planes)[i]->Translate(trans2);

				p = (*planes)[i]->get_point();
				n = (*planes)[i]->normal();
				p = tform_copy.unproject(p);
				n = tform->project(n);
				n.safe_normalize();
				(*planes)[i]->ChangePlaneTemp(p, n);
			}
		}
	}

	PopMeshList();
	for (MeshData* md : m_md_pop_list)
	{
		std::vector<Plane*>* planes = MeshPlanes(md);
		if (!planes)
			continue;

		Vector sz = md->GetBounds().diagonal();
		Vector scale2(sz.x(), sz.y(), sz.z());
		Vector scale2inv(1.0 / sz.x(), 1.0 / sz.y(), 1.0 / sz.z());
		Vector trans1(-0.5, -0.5, -0.5);
		Vector trans2(0.5, 0.5, 0.5);

		if (m_clip_mode == 3)
		{
			Vector obj_trans1 = m_trans_fix;
			Vector obj_trans2 = -obj_trans1;
			Vector obj_trans3 = obj_trans1 - Vector(m_obj_transx, m_obj_transy, -m_obj_transz);

			for (int i = 0; i < 6; i++)
			{
				(*planes)[i]->Restore();
				(*planes)[i]->Translate(trans1);
				(*planes)[i]->Scale2(scale2);
				(*planes)[i]->Rotate(m_q_cl_fix);
				(*planes)[i]->Translate(obj_trans1);
				(*planes)[i]->Rotate(m_q_cl);
				(*planes)[i]->Translate(obj_trans2);
				(*planes)[i]->Translate(obj_trans3);
				(*planes)[i]->Scale2(scale2inv);
				(*planes)[i]->Translate(trans2);
			}
		}
		else
		{
			for (int i = 0; i < 6; i++)
			{
				(*planes)[i]->Restore();
				(*planes)[i]->Translate(trans1);
				(*planes)[i]->Scale2(scale2);
				(*planes)[i]->Rotate(m_q_cl);
				(*planes)[i]->Scale2(scale2inv);
				(*planes)[i]->Translate(trans2);
			}
		}
	}
}

// VRenderVulkanView::A2QSync (A2Q when the view synchronises the planes)
void RenderCanvas::A2QSync()
{
	m_q.FromEuler(m_rotx, m_roty, m_rotz);
	if (m_sync_clipping_planes)
	{
		ApplyClipPlaneTransforms();
		return;
	}
	Vector obj_ctr(m_obj_ctrx, m_obj_ctry, m_obj_ctrz);
	Vector obj_trans(m_obj_transx, m_obj_transy, m_obj_transz);
	// wx: per layer; a group with synchronised clipping planes as one
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 5:
		{
			DataGroup* group = (DataGroup*)layer;
			if (group->GetSyncClippingPlanes())
				group->A2Q(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			else
				for (int j = 0; j < group->GetVolumeNum(); j++)
					if (VolumeData* vd = group->GetVolumeData(j)) vd->A2Q(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
		}
		break;
		case 6:
		{
			MeshGroup* group = (MeshGroup*)layer;
			if (group->GetSyncClippingPlanes())
				group->A2Q(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			else
				for (int j = 0; j < group->GetMeshNum(); j++)
					if (MeshData* md = group->GetMeshData(j)) md->A2Q(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
		}
		break;
		case 2:
			((VolumeData*)layer)->A2Q(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			break;
		case 3:
			((MeshData*)layer)->A2Q(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			break;
		}
	}
}

// VRenderVulkanView::Q2ASync
void RenderCanvas::Q2ASync()
{
	m_q.ToEuler(m_rotx, m_roty, m_rotz);
	m_rotx = Wrap360(m_rotx);
	m_roty = Wrap360(m_roty);
	m_rotz = Wrap360(m_rotz);
	if (m_sync_clipping_planes)
	{
		ApplyClipPlaneTransforms();
		return;
	}
	Vector obj_ctr(m_obj_ctrx, m_obj_ctry, m_obj_ctrz);
	Vector obj_trans(m_obj_transx, m_obj_transy, m_obj_transz);
	// wx: per layer; a group with synchronised clipping planes as one
	for (TreeLayer* layer : m_layer_list)
	{
		if (!layer) continue;
		switch (layer->IsA())
		{
		case 5:
		{
			DataGroup* group = (DataGroup*)layer;
			if (group->GetSyncClippingPlanes())
				group->Q2A(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			else
				for (int j = 0; j < group->GetVolumeNum(); j++)
					if (VolumeData* vd = group->GetVolumeData(j)) vd->Q2A(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
		}
		break;
		case 6:
		{
			MeshGroup* group = (MeshGroup*)layer;
			if (group->GetSyncClippingPlanes())
				group->Q2A(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			else
				for (int j = 0; j < group->GetMeshNum(); j++)
					if (MeshData* md = group->GetMeshData(j)) md->Q2A(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
		}
		break;
		case 2:
			((VolumeData*)layer)->Q2A(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			break;
		case 3:
			((MeshData*)layer)->Q2A(m_rotx, m_roty, m_rotz, obj_ctr, obj_trans);
			break;
		}
	}
}

// VRenderVulkanView::CalcAndSetCombinedClippingPlanes
void RenderCanvas::CalcAndSetCombinedClippingPlanes()
{
	std::vector<VolumeData*> vols = PopVolumeList();   // wx m_vd_pop_list
	PopMeshList();

	if (m_sync_clipping_planes)
	{
		BBox bounds;
		for (VolumeData* vd : vols)
		{
			if (!vd->GetTexture())
				continue;
			Transform* tform = vd->GetTexture()->transform();
			if (!tform)
				continue;
			Transform tform_copy = SwappedTransform(tform);
			Point p[8] = {
				Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0), Point(1.0, 1.0, 0.0), Point(0.0, 1.0, 0.0),
				Point(0.0, 0.0, 1.0), Point(1.0, 0.0, 1.0), Point(1.0, 1.0, 1.0), Point(0.0, 1.0, 1.0) };
			for (int j = 0; j < 8; j++)
			{
				p[j] = tform_copy.project(p[j]);
				bounds.extend(p[j]);
			}
		}
		for (MeshData* md : m_md_pop_list)
		{
			md->RecalcBounds();
			bounds.extend(md->GetBounds());
		}

		for (VolumeData* vd : vols)
		{
			Point pp[6] = {
				Point(bounds.min().x(), 0.0, 0.0),
				Point(bounds.max().x(), 0.0, 0.0),
				Point(0.0, bounds.min().y(), 0.0),
				Point(0.0, bounds.max().y(), 0.0),
				Point(0.0, 0.0, bounds.max().z()),
				Point(0.0, 0.0, bounds.min().z())
			};
			Vector pn[6] = {
				Vector(1.0, 0.0, 0.0),
				Vector(-1.0, 0.0, 0.0),
				Vector(0.0, 1.0, 0.0),
				Vector(0.0, -1.0, 0.0),
				Vector(0.0, 0.0, -1.0),
				Vector(0.0, 0.0, 1.0)
			};
			std::vector<Plane*>* planes = VolumePlanes(vd);
			if (!planes || !vd->GetTexture())
				continue;
			Transform* tform = vd->GetTexture()->transform();
			if (!tform)
				continue;
			Transform tform_copy = SwappedTransform(tform);
			for (int j = 0; j < 6; j++)
			{
				pp[j] = tform_copy.unproject(pp[j]);
				pn[j] = tform->project(pn[j]);
				pn[j].safe_normalize();
				(*planes)[j]->RememberParam();
				(*planes)[j]->ChangePlane(pp[j], pn[j]);
			}
			SetPlaneRanges(planes);
			for (int j = 0; j < 6; j++)
				(*planes)[j]->RestoreParam();
		}

		for (MeshData* md : m_md_pop_list)
			md->SetBounds(bounds);

		for (VolumeData* vd : vols)
		{
			std::vector<Plane*>* planes = VolumePlanes(vd);
			if (!planes)
				continue;
			for (int j = 0; j < 6; j++)
				(*planes)[j]->SetParam(m_linked_plane_params[j]);
			vd->SetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
			vd->SetClippingFixParams(m_clip_mode, m_q_cl_zero, m_q_cl_fix, m_q_fix, m_rotx_cl_fix, m_roty_cl_fix, m_rotz_cl_fix,
				m_rotx_fix, m_roty_fix, m_rotz_fix, m_trans_fix);
		}
		for (MeshData* md : m_md_pop_list)
		{
			std::vector<Plane*>* planes = MeshPlanes(md);
			if (!planes)
				continue;
			for (int j = 0; j < 6; j++)
				(*planes)[j]->SetParam(m_linked_plane_params[j]);
			md->SetClippingPlaneRotationsRaw(m_rotx_cl, m_roty_cl, m_rotz_cl);
			md->SetClippingFixParams(m_clip_mode, m_q_cl_zero, m_q_cl_fix, m_q_fix, m_rotx_cl_fix, m_roty_cl_fix, m_rotz_cl_fix,
				m_rotx_fix, m_roty_fix, m_rotz_fix, m_trans_fix);
		}
	}
	else
	{
		// wx: per layer -- a group with unsynchronised planes resets its
		// volumes' planes to the unit box, a synchronised group fits them to
		// the group's bounds and copies the group's clipping parameters;
		// top-level meshes and mesh groups likewise
		auto reset_volume_planes = [](const std::vector<VolumeData*>& gvols) {
			bool spim = false;
			for (VolumeData* vd : gvols)
			{
				if (!vd || !vd->GetTexture()) continue;
				Transform* tform = vd->GetTexture()->additional_transform();
				if (tform && !tform->is_identity()) { spim = true; break; }
			}
			for (VolumeData* vd : gvols)
			{
				std::vector<Plane*>* planes = VolumePlanes(vd);
				if (!planes)
					continue;
				SetUnitPlanes(planes);
				SetPlaneRanges(planes);
				for (Plane* p : *planes)
				{
					if (!spim)
						p->SetParam(p->GetParam());
					else
						p->RestoreParam();
				}
			}
		};
		auto reset_mesh_planes = [](MeshData* md) {
			std::vector<Plane*>* planes = MeshPlanes(md);
			if (!planes)
				return false;
			SetUnitPlanes(planes);
			SetPlaneRanges(planes);
			for (Plane* p : *planes)
				p->SetParam(p->GetParam());
			return true;
		};
		for (TreeLayer* layer : m_layer_list)
		{
			if (!layer) continue;
			switch (layer->IsA())
			{
			case 2:
				reset_volume_planes({ (VolumeData*)layer });
				break;
			case 5:
			{
				DataGroup* group = (DataGroup*)layer;
				std::vector<VolumeData*> gvols;
				for (int j = 0; j < group->GetVolumeNum(); j++)
					gvols.push_back(group->GetVolumeData(j));
				if (!group->GetSyncClippingPlanes())
				{
					reset_volume_planes(gvols);
					continue;
				}
				BBox bounds;
				for (VolumeData* vd : gvols)
				{
					if (!vd || !vd->GetTexture()) continue;
					Transform* tform = vd->GetTexture()->transform();
					if (!tform) continue;
					Transform tform_copy = SwappedTransform(tform);
					Point p[8] = {
						Point(0.0, 0.0, 0.0), Point(1.0, 0.0, 0.0), Point(1.0, 1.0, 0.0), Point(0.0, 1.0, 0.0),
						Point(0.0, 0.0, 1.0), Point(1.0, 0.0, 1.0), Point(1.0, 1.0, 1.0), Point(0.0, 1.0, 1.0) };
					for (int k = 0; k < 8; k++)
						bounds.extend(tform_copy.project(p[k]));
				}
				for (VolumeData* vd : gvols)
				{
					if (!vd) continue;
					Point pp[6] = {
						Point(bounds.min().x(), 0.0, 0.0),
						Point(bounds.max().x(), 0.0, 0.0),
						Point(0.0, bounds.min().y(), 0.0),
						Point(0.0, bounds.max().y(), 0.0),
						Point(0.0, 0.0, bounds.max().z()),
						Point(0.0, 0.0, bounds.min().z())
					};
					Vector pn[6] = {
						Vector(1.0, 0.0, 0.0), Vector(-1.0, 0.0, 0.0),
						Vector(0.0, 1.0, 0.0), Vector(0.0, -1.0, 0.0),
						Vector(0.0, 0.0, -1.0), Vector(0.0, 0.0, 1.0)
					};
					if (!vd->GetVR() || !vd->GetTexture()) continue;
					Transform* tform = vd->GetTexture()->transform();
					if (!tform) continue;
					Transform tform_copy = SwappedTransform(tform);
					std::vector<Plane*>* planes = vd->GetVR()->get_planes();
					for (int k = 0; k < 6; k++)
					{
						pp[k] = tform_copy.unproject(pp[k]);
						pn[k] = tform->project(pn[k]);
						pn[k].safe_normalize();
						(*planes)[k]->RememberParam();
						(*planes)[k]->ChangePlane(pp[k], pn[k]);
					}
					SetPlaneRanges(planes);
					for (int k = 0; k < 6; k++)
						(*planes)[k]->RestoreParam();
				}
				for (VolumeData* vd : gvols)
				{
					std::vector<Plane*>* planes = vd ? VolumePlanes(vd) : nullptr;
					if (!planes) continue;
					for (int j = 0; j < 6; j++)
						(*planes)[j]->SetParam(group->GetLinkedParam(j));
					vd->CopyClippingParams(*(ClippingLayer*)group);
				}
			}
			break;
			case 3:
			{
				MeshData* md = (MeshData*)layer;
				if (reset_mesh_planes(md))
					md->RecalcBounds();
			}
			break;
			case 6:
			{
				MeshGroup* group = (MeshGroup*)layer;
				if (!group->GetSyncClippingPlanes())
				{
					for (int j = 0; j < group->GetMeshNum(); j++)
						if (MeshData* md = group->GetMeshData(j)) reset_mesh_planes(md);
					continue;
				}
				BBox bounds;
				for (int j = 0; j < group->GetMeshNum(); j++)
					if (MeshData* md = group->GetMeshData(j)) { md->RecalcBounds(); bounds.extend(md->GetBounds()); }
				for (int j = 0; j < group->GetMeshNum(); j++)
					if (MeshData* md = group->GetMeshData(j)) md->SetBounds(bounds);
				for (int j = 0; j < group->GetMeshNum(); j++)
				{
					MeshData* md = group->GetMeshData(j);
					std::vector<Plane*>* planes = md ? MeshPlanes(md) : nullptr;
					if (!planes) continue;
					for (int k = 0; k < 6; k++)
						(*planes)[k]->SetParam(group->GetLinkedParam(k));
					md->CopyClippingParams(*(ClippingLayer*)group);
				}
			}
			break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawClippingPlanes
// ---------------------------------------------------------------------------

void RenderCanvas::DrawClippingPlanes(bool border, int face_winding)
{
	const bool link = m_clip_chann_link;
	const int plane_mode = m_clip_plane_mode;
	const int draw_type = m_clip_sel_type;
	VolumeData* cur_vd = nullptr;
	MeshData* cur_md = nullptr;
	if (draw_type == 2)
	{
		cur_vd = m_clip_vd;
		if (!cur_vd || std::find(m_volumes.begin(), m_volumes.end(), cur_vd) == m_volumes.end() || !cur_vd->GetDisp())
			return;
	}
	else if (draw_type == 3)
	{
		cur_md = m_clip_md;
		if (!cur_md || std::find(m_meshes.begin(), m_meshes.end(), cur_md) == m_meshes.end() || !cur_md->GetDisp())
			return;
	}
	else
		return;

	VkCullModeFlags cullmode = VK_CULL_MODE_NONE;
	bool draw_plane = plane_mode != PM_FRAME;
	if ((plane_mode == PM_LOWTRANSBACK ||
		plane_mode == PM_NORMALBACK) &&
		m_clip_mask == -1)
	{
		cullmode = VK_CULL_MODE_FRONT_BIT;
		if (face_winding == kBackFace)
			face_winding = kFrontFace;
		else
			draw_plane = false;
	}
	else
		cullmode = VK_CULL_MODE_BACK_BIT;

	if (!border && plane_mode == PM_FRAME)
		return;

	bool cull = false;
	VkFrontFace frontface = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	if (face_winding == kFrontFace)
	{
		cull = true;
		frontface = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	}
	else if (face_winding == kBackFace)
	{
		cull = true;
		frontface = VK_FRONT_FACE_CLOCKWISE;
	}

	std::vector<Plane*>* planes = draw_type == 2 ? VolumePlanes(cur_vd) : MeshPlanes(cur_md);
	if (!planes)
		return;

	//get six planes
	Plane* px1 = (*planes)[0];
	Plane* px2 = (*planes)[1];
	Plane* py1 = (*planes)[2];
	Plane* py2 = (*planes)[3];
	Plane* pz1 = (*planes)[4];
	Plane* pz2 = (*planes)[5];

	//calculate 4 lines
	Vector lv_x1z1, lv_x1z2, lv_x2z1, lv_x2z2;
	Point lp_x1z1, lp_x1z2, lp_x2z1, lp_x2z2;
	if (!px1->Intersect(*pz1, lp_x1z1, lv_x1z1)) return;
	if (!px1->Intersect(*pz2, lp_x1z2, lv_x1z2)) return;
	if (!px2->Intersect(*pz1, lp_x2z1, lv_x2z1)) return;
	if (!px2->Intersect(*pz2, lp_x2z2, lv_x2z2)) return;

	//calculate 8 points
	Point pp[8];
	if (!py1->Intersect(lp_x1z1, lv_x1z1, pp[0])) return;
	if (!py1->Intersect(lp_x1z2, lv_x1z2, pp[1])) return;
	if (!py1->Intersect(lp_x2z1, lv_x2z1, pp[2])) return;
	if (!py1->Intersect(lp_x2z2, lv_x2z2, pp[3])) return;
	if (!py2->Intersect(lp_x1z1, lv_x1z1, pp[4])) return;
	if (!py2->Intersect(lp_x1z2, lv_x1z2, pp[5])) return;
	if (!py2->Intersect(lp_x2z1, lv_x2z1, pp[6])) return;
	if (!py2->Intersect(lp_x2z2, lv_x2z2, pp[7])) return;

	//get color
	Color color(1.0, 1.0, 1.0);
	double plane_trans = 0.0;
	if (face_winding == kBackFace &&
		(m_clip_mask == 3 || m_clip_mask == 12 || m_clip_mask == 48 ||
		 m_clip_mask == 1 || m_clip_mask == 2 || m_clip_mask == 4 ||
		 m_clip_mask == 8 || m_clip_mask == 16 || m_clip_mask == 32 || m_clip_mask == 64))
		plane_trans = plane_mode == PM_LOWTRANS || plane_mode == PM_LOWTRANSBACK ? 0.1 : 0.3;

	if (face_winding == kFrontFace)
		plane_trans = plane_mode == PM_LOWTRANS || plane_mode == PM_LOWTRANSBACK ? 0.1 : 0.3;

	if (plane_mode == PM_NORMAL || plane_mode == PM_NORMALBACK)
	{
		if (!link)
		{
			if (draw_type == 2)
				color = cur_vd->GetColor();
			else
			{
				Color amb, diff, spec;
				double shine, alpha;
				cur_md->GetMaterial(amb, diff, spec, shine, alpha);
				color = diff;
			}
		}
	}
	else
		color = InvertedBackground(m_bg_color);   // wx GetTextColor() default

	//transform
	glm::mat4 matrix;
	if (draw_type == 2)
	{
		if (!cur_vd->GetTexture())
			return;
		Transform* tform = cur_vd->GetTexture()->transform();
		if (!tform)
			return;
		double mvmat[16];
		tform->get_trans(mvmat);
		double sclx, scly, sclz;
		cur_vd->GetScalings(sclx, scly, sclz);
		glm::mat4 mv_mat = glm::scale(m_mv_mat,
			glm::vec3(float(sclx), float(scly), float(sclz)));
		glm::mat4 mv_mat2 = glm::mat4(
			mvmat[0], mvmat[4], mvmat[8], mvmat[3],
			mvmat[1], mvmat[5], mvmat[9], mvmat[7],
			mvmat[2], mvmat[6], mvmat[10], mvmat[11],
			mvmat[12], mvmat[13], mvmat[14], mvmat[15]);
		mv_mat = mv_mat * mv_mat2;
		matrix = m_proj_mat * mv_mat;
	}
	else
	{
		BBox dbox = cur_md->GetBounds();
		glm::mat4 mvmat = glm::mat4(float(dbox.max().x() - dbox.min().x()), 0.0f, 0.0f, 0.0f,
			0.0f, float(dbox.max().y() - dbox.min().y()), 0.0f, 0.0f,
			0.0f, 0.0f, -float(dbox.max().z() - dbox.min().z()), 0.0f,
			float(dbox.min().x()), float(dbox.min().y()), float(dbox.max().z()), 1.0f);
		matrix = m_proj_mat * m_mv_mat * mvmat;
	}

	std::vector<Vulkan2dRender::Vertex> vertex;
	std::vector<uint32_t> index;
	vertex.reserve(8);
	index.reserve(6 * 4 + 6 * 5);
	for (size_t pi = 0; pi < 8; ++pi)
	{
		vertex.push_back(Vulkan2dRender::Vertex{
			{ (float)pp[pi].x(), (float)pp[pi].y(), (float)pp[pi].z() },
			{ 0.0f, 0.0f, 0.0f }
		});
	}
	//indices (fixed topology)
	static const uint32_t kIdx[6 * 4 + 6 * 5] = {
		4, 0, 5, 1,
		7, 3, 6, 2,
		1, 0, 3, 2,
		4, 5, 6, 7,
		0, 4, 2, 6,
		5, 1, 7, 3,
		4, 0, 1, 5, 4,
		7, 3, 2, 6, 7,
		1, 0, 2, 3, 1,
		4, 5, 7, 6, 4,
		0, 4, 6, 2, 0,
		5, 1, 3, 7, 5 };
	index.assign(kIdx, kIdx + sizeof(kIdx) / sizeof(kIdx[0]));

	Vulkan2dRender::V2dObject clip_vobj;   // wx m_clip_vobj (transient)
	UpdateV2dObject(m_vulkan->vulkanDevice, clip_vobj, vertex, index);

	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	Vulkan2dRender::V2dPipeline pipeline_line =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
			VK_POLYGON_MODE_FILL,
			cull ? cullmode : VK_CULL_MODE_NONE,
			frontface);
	Vulkan2dRender::V2dPipeline pipeline_tri =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
			VK_POLYGON_MODE_FILL,
			cull ? cullmode : VK_CULL_MODE_NONE,
			frontface);
	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(pipeline_line.pass);

	std::vector<Vulkan2dRender::V2DRenderParams> params_list;
	Vulkan2dRender::V2DRenderParams params;
	params.matrix[0] = matrix;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	params.obj = &clip_vobj;

	// per plane: mask bit, NORMAL-mode fill colour
	static const float kFill[6][3] = {
		{ 1.0f, 0.5f, 0.5f },   // x1 = (p4, p0, p1, p5)
		{ 1.0f, 0.5f, 1.0f },   // x2 = (p7, p3, p2, p6)
		{ 0.5f, 1.0f, 0.5f },   // y1 = (p1, p0, p2, p3)
		{ 1.0f, 1.0f, 0.5f },   // y2 = (p4, p5, p7, p6)
		{ 0.5f, 0.5f, 1.0f },   // z1 = (p0, p4, p6, p2)
		{ 0.5f, 1.0f, 1.0f } }; // z2 = (p5, p1, p3, p7)
	for (int i = 0; i < 6; ++i)
	{
		if (!(m_clip_mask & (1 << i)))
			continue;
		if (draw_plane)
		{
			if (plane_mode == PM_NORMAL || plane_mode == PM_NORMALBACK)
				params.loc[0] = glm::vec4(kFill[i][0], kFill[i][1], kFill[i][2], (float)plane_trans);
			else
				params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
			params.pipeline = pipeline_tri;
			params.render_idxCount = 4;
			params.render_idxBase = 4 * i;
			params_list.push_back(params);
		}
		if (border)
		{
			params.loc[0] = glm::vec4((float)color.r(), (float)color.g(), (float)color.b(), (float)plane_trans);
			params.pipeline = pipeline_line;
			params.render_idxCount = 5;
			params.render_idxBase = 4 * 6 + 5 * i;
			params_list.push_back(params);
		}
	}

	if (!params_list.empty())
	{
		m_frame_clear = false;
		m_v2drender->GetNextV2dRenderSemaphoreSettings(params_list[0]);
		m_v2drender->seq_render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params_list.data(), (int)params_list.size());
	}
}

// ---------------------------------------------------------------------------
// VRenderView::OnRotLink(bool) (SettingDlg "Link all rendering views'
// rotations"): verbatim -- this view's flag is set first, then every view's
// to b, and the rotation is copied to all of them
// ---------------------------------------------------------------------------

void RenderCanvas::LinkAllRotationsFromSettings(bool b)
{
	m_linked_rot = true;
	const double rx = m_rotx, ry = m_roty, rz = m_rotz;
	for (RenderCanvas* c : Canvases())
	{
		if (!c) continue;
		c->m_linked_rot = b;
		c->SetRotations(rx, ry, rz);
		c->RequestRender();
	}
}

// ---------------------------------------------------------------------------
// VRenderVulkanView::DrawGradBg: the swapchain image is filled with a
// vertical gradient derived from the background colour before the scene
// (which is then blended over it: m_frame_clear is used up)
// ---------------------------------------------------------------------------

void RenderCanvas::DrawGradBg()
{
	glm::mat4 proj_mat = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f);

	Color color1, color2;
	HSVColor hsv_color1(m_bg_color);
	if (hsv_color1.val() > 0.5)
	{
		if (hsv_color1.sat() > 0.3)
		{
			color1 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.1,
				std::min(hsv_color1.val() + 0.3, 1.0)));
			color2 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.3,
				std::min(hsv_color1.val() + 0.1, 1.0)));
		}
		else
		{
			color1 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.1,
				std::max(hsv_color1.val() - 0.5, 0.0)));
			color2 = Color(HSVColor(hsv_color1.hue(),
				hsv_color1.sat() * 0.3,
				std::max(hsv_color1.val() - 0.3, 0.0)));
		}
	}
	else
	{
		color1 = Color(HSVColor(hsv_color1.hue(),
			hsv_color1.sat() * 0.1,
			std::min(hsv_color1.val() + 0.7, 1.0)));
		color2 = Color(HSVColor(hsv_color1.hue(),
			hsv_color1.sat() * 0.3,
			std::min(hsv_color1.val() + 0.5, 1.0)));
	}

	std::vector<Vulkan2dRender::Vertex> vertex;
	vertex.reserve(8);
	const float br = (float)m_bg_color.r(), bgc = (float)m_bg_color.g(), bb = (float)m_bg_color.b();
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 1.0f, 0.0f}, {br, bgc, bb} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 1.0f, 0.0f}, {br, bgc, bb} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.7f, 0.0f}, {(float)color1.r(), (float)color1.g(), (float)color1.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.7f, 0.0f}, {(float)color1.r(), (float)color1.g(), (float)color1.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.5f, 0.0f}, {(float)color2.r(), (float)color2.g(), (float)color2.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.5f, 0.0f}, {(float)color2.r(), (float)color2.g(), (float)color2.b()} });
	vertex.push_back(Vulkan2dRender::Vertex{ {0.0f, 0.0f, 0.0f}, {br, bgc, bb} });
	vertex.push_back(Vulkan2dRender::Vertex{ {1.0f, 0.0f, 0.0f}, {br, bgc, bb} });

	std::vector<uint32_t> index = { 0, 1, 2, 3, 4, 5, 6, 7 };
	Vulkan2dRender::V2dObject grad_vobj;   // wx m_grad_vobj (transient)
	UpdateV2dObject(m_vulkan->vulkanDevice, grad_vobj, vertex, index);

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY_COLOR3,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
		);
	params.matrix[0] = proj_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	m_frame_clear = false;

	params.obj = &grad_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}

// wx VRenderVulkanView::DrawGrid (Settings wireframe test mode, m_draw_grid):
// an 11 x 11 line grid in the y = 0 plane of object space, +-m_distance,
// in the text colour
void RenderCanvas::DrawGrid()
{
	size_t grid_num = 5;
	size_t line_num = grid_num * 2 + 1;
	double gap = m_distance / grid_num;
	size_t i;
	std::vector<Vulkan2dRender::Vertex> vertex;
	std::vector<uint32_t> index;
	vertex.reserve(line_num * 2 * 2);
	index.reserve(line_num * 2 * 2);

	for (i = 0; i < line_num; ++i)
	{
		vertex.push_back(Vulkan2dRender::Vertex{ {float(-m_distance + gap * i), 0.0f, float(-m_distance)}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {float(-m_distance + gap * i), 0.0f, float(m_distance)}, {0.0f, 0.0f, 0.0f} });
		index.push_back(uint32_t(2 * i));
		index.push_back(uint32_t(2 * i + 1));
	}
	for (i = 0; i < line_num; ++i)
	{
		vertex.push_back(Vulkan2dRender::Vertex{ {float(-m_distance), 0.0f, float(-m_distance + gap * i)}, {0.0f, 0.0f, 0.0f} });
		vertex.push_back(Vulkan2dRender::Vertex{ {float(m_distance), 0.0f, float(-m_distance + gap * i)}, {0.0f, 0.0f, 0.0f} });
		index.push_back(uint32_t(2 * line_num + 2 * i));
		index.push_back(uint32_t(2 * line_num + 2 * i + 1));
	}

	Vulkan2dRender::V2dObject grid_vobj;   // wx m_grid_vobj (per-frame host buffers)
	UpdateV2dObject(m_vulkan->vulkanDevice, grid_vobj, vertex, index);

	Vulkan2dRender::V2DRenderParams params = m_v2drender->GetNextV2dRenderSemaphoreSettings();
	vks::VFrameBuffer* current_fbo = m_vulkan->frameBuffers[m_vulkan->currentBuffer].get();
	params.pipeline =
		m_v2drender->preparePipeline(
			IMG_SHDR_DRAW_GEOMETRY,
			V2DRENDER_BLEND_OVER_UI,
			current_fbo->attachments[0]->format,
			(int)current_fbo->attachments.size(),
			0,
			current_fbo->attachments[0]->is_swapchain_images,
			VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
	Color text_color = m_text_color ? m_text_color() : InvertedBackground(m_bg_color);
	params.loc[0] = glm::vec4((float)text_color.r(), (float)text_color.g(), (float)text_color.b(), 1.0f);
	params.matrix[0] = m_proj_mat * m_mv_mat;
	params.clear = m_frame_clear;
	params.clearColor = { (float)m_bg_color.r(), (float)m_bg_color.g(), (float)m_bg_color.b(), 1.0f };
	m_frame_clear = false;

	params.obj = &grid_vobj;

	if (!current_fbo->renderPass)
		current_fbo->replaceRenderPass(params.pipeline.pass);

	m_v2drender->render(m_vulkan->frameBuffers[m_vulkan->currentBuffer], params);
}
