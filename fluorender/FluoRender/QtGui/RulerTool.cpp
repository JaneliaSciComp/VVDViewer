// Port of the ruler code of VRenderVulkanView (VRenderView.cpp). See
// RulerTool.h for the mapping of wx members / functions.

#include "RulerTool.h"
#include "VolumeSelector.h"
#include "RenderCanvas.h"
#include "SettingsDialog.h"

#include <QMouseEvent>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <random>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "VVulkan.h"
#include "Vulkan/VulkanDevice.hpp"
#include "TextRenderer.h"
#include "DataManager.h"
#include "FLIVR/Transform.h"
#include "FLIVR/Ray.h"
#include "FLIVR/Plane.h"
#include "FLIVR/ImgShader.h"
#include "FLIVR/ThinPlateSpline.h"
#include "RigidCellsWarp.h"

using namespace FLIVR;

namespace
{
	inline double deg2rad(double d) { return d * 3.14159265358979323846 / 180.0; }

	// VRenderView.cpp: UpdateV2dObject. Overlay geometry is transient
	// (rewritten every draw), so it goes into the device's per-frame
	// host-visible rings.
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

	// VRenderView.h: UniformDensityPointGenerator (ScatterRulers)
	class UniformDensityPointGenerator
	{
	private:
		std::mt19937 gen;
		double nx, ny, nz;
		int numPoints;

		struct CellDimensions
		{
			int nx_cells, ny_cells, nz_cells;
			double cx, cy, cz;  // per-axis cell sizes (all cells equal within each axis)
		};

		CellDimensions calculateCellDimensions()
		{
			double volume = nx * ny * nz;
			double approx_cell_size = std::cbrt(volume / numPoints);

			CellDimensions dims;
			dims.nx_cells = std::max(1, static_cast<int>(std::round(nx / approx_cell_size)));
			dims.ny_cells = std::max(1, static_cast<int>(std::round(ny / approx_cell_size)));
			dims.nz_cells = std::max(1, static_cast<int>(std::round(nz / approx_cell_size)));
			dims.cx = nx / dims.nx_cells;
			dims.cy = ny / dims.ny_cells;
			dims.cz = nz / dims.nz_cells;
			return dims;
		}

	public:
		UniformDensityPointGenerator(int num_points, double max_x, double max_y, double max_z)
			: gen(static_cast<unsigned int>(time(nullptr))),
			nx(max_x), ny(max_y), nz(max_z), numPoints(num_points) {}

		std::vector<Point> generatePoints()
		{
			std::vector<Point> points;
			auto dims = calculateCellDimensions();
			std::uniform_real_distribution<double> dist_unit(0.0, 1.0);
			int total_cells = dims.nx_cells * dims.ny_cells * dims.nz_cells;
			std::vector<int> cell_indices(total_cells);
			for (int i = 0; i < total_cells; ++i)
				cell_indices[i] = i;
			std::shuffle(cell_indices.begin(), cell_indices.end(), gen);
			for (int i = 0; i < numPoints && i < total_cells; ++i)
			{
				int cell_idx = cell_indices[i];
				int z_idx = cell_idx / (dims.nx_cells * dims.ny_cells);
				int remainder = cell_idx % (dims.nx_cells * dims.ny_cells);
				int y_idx = remainder / dims.nx_cells;
				int x_idx = remainder % dims.nx_cells;
				double x = (x_idx + dist_unit(gen)) * dims.cx;
				double y = (y_idx + dist_unit(gen)) * dims.cy;
				double z = (z_idx + dist_unit(gen)) * dims.cz;
				points.emplace_back(x, y, z);
			}
			return points;
		}
	};

	inline Vulkan2dRender::Vertex V(float x, float y, const Color& c)
	{
		return Vulkan2dRender::Vertex{ {x, y, 0.0f}, {(float)c.r(), (float)c.g(), (float)c.b()} };
	}
}

// ---------------------------------------------------------------------------

RulerTool::RulerTool(RenderCanvas* canvas, QObject* parent)
	: QObject(parent), m_canvas(canvas)
{
	if (m_canvas)
	{
		m_canvas->AddTool(this);
		// wx creates m_text_renderer right after InitVulkan
		connect(m_canvas, &RenderCanvas::vulkanReady, this, [this]() { EnsureTextRenderer(); });
		if (m_canvas->IsVulkanReady())
			EnsureTextRenderer();
	}
}

RulerTool::~RulerTool()
{
	if (m_canvas) m_canvas->RemoveTool(this);
	ClearRulers();
	for (Ruler* r : m_landmarks) delete r;
	m_landmarks.clear();
}

RenderCanvas* RulerTool::Canvas() const
{
	return m_canvas.data();
}

void RulerTool::Refresh()
{
	if (m_canvas) m_canvas->RefreshView();
}

VolumeData* RulerTool::CurVol() const
{
	return m_canvas ? m_canvas->GetCurrentVolume() : nullptr;
}

void RulerTool::SetIntMode(int mode)
{
	bool changed = mode != m_int_mode;
	m_int_mode = mode;
	if (m_int_mode != 6)
		m_editing_ruler_point = nullptr;
	m_camera_drag = m_point_drag = false;
	if (changed) Q_EMIT intModeChanged(m_int_mode);
}

void RulerTool::SetCurTime(int t)
{
	if (t == m_tseq_cur_num) return;
	m_tseq_cur_num = t;
	Refresh();
	Q_EMIT rulersChanged();
}

void RulerTool::SetDrawRulers(bool b) { m_draw_rulers = b; Refresh(); }
void RulerTool::SetDrawLandmarks(bool b) { m_draw_landmarks = b; Refresh(); }

void RulerTool::ClearRulers()
{
	for (Ruler* r : m_ruler_list) delete r;
	m_ruler_list.clear();
	m_editing_ruler_point = nullptr;
	m_point_drag = false;
}

void RulerTool::RulersModified()
{
	m_editing_ruler_point = nullptr;
	m_point_drag = false;
	Refresh();
	Q_EMIT rulersChanged();
}

// ---------------------------------------------------------------------------
// text renderer (wx: created in the VRenderVulkanView constructor after
// InitVulkan; font = SettingDlg font file in <resources>/Fonts, FreeSans.ttf
// by default; size = SettingDlg text size)
// ---------------------------------------------------------------------------

void RulerTool::SetFont(const QString& path, int size)
{
	m_font_path = path;
	if (size > 0) m_font_size = size;
	m_font_dirty = true;
	Refresh();
}

void RulerTool::SetTextSize(int size)
{
	if (size <= 0) return;
	m_font_size = size;
	if (m_text_renderer) m_text_renderer->SetSize(size);
	Refresh();
}

void RulerTool::EnsureTextRenderer()
{
	if (!m_canvas) return;
	std::shared_ptr<Vulkan2dRender> v2d = m_canvas->GetV2DRender();
	if (!v2d) return;
	QString path = m_font_path;
	if (path.isEmpty())
		path = QDir(SettingsDialog::FontsDir()).filePath(QStringLiteral("FreeSans.ttf"));
	if (!m_text_renderer)
	{
		m_text_renderer = std::make_unique<TextRenderer>(path.toStdString(), v2d);
		if (m_font_size > 0)
			m_text_renderer->SetSize(m_font_size);
		m_font_dirty = false;
	}
	else if (m_font_dirty)
	{
		m_text_renderer->LoadNewFace(path.toStdString(), m_font_size);
		m_font_dirty = false;
	}
}

// VRenderVulkanView::GetTextColor (+ m_bg_color_inv from SetBackgroundColor)
Color RulerTool::GetTextColor()
{
	Color bg = m_canvas ? m_canvas->GetBackgroundColor() : Color(0.0, 0.0, 0.0);
	Color bg_inv;
	HSVColor hsv(bg);
	if (hsv.val() > 0.7 && hsv.sat() > 0.7)
		bg_inv = Color(HSVColor(hsv.hue() + 180.0, 1.0, 1.0));
	else if (hsv.val() > 0.7)
		bg_inv = Color(0.0, 0.0, 0.0);
	else
		bg_inv = Color(1.0, 1.0, 1.0);

	switch (m_text_color_mode)
	{
	case 0: return bg_inv;
	case 1: return bg;
	case 2:
		if (VolumeData* vd = CurVol())
			return vd->GetMaskColor();
		return bg_inv;
	}
	return bg_inv;
}

// ---------------------------------------------------------------------------
// ruler creation (VRenderVulkanView::FinishRuler / GetRulerFinished / AddRulerPoint)
// ---------------------------------------------------------------------------

void RulerTool::FinishRuler()
{
	size_t size = m_ruler_list.size();
	if (!size) return;
	if (m_ruler_list[size - 1] &&
		m_ruler_list[size - 1]->GetRulerType() == 1)
		m_ruler_list[size - 1]->SetFinished();
}

bool RulerTool::GetRulerFinished()
{
	size_t size = m_ruler_list.size();
	if (!size) return true;
	if (m_ruler_list[size - 1])
		return m_ruler_list[size - 1]->GetFinished();
	else
		return true;
}

VolumeData* RulerTool::AutoSelectVolume(int mx, int my)
{
	if (!m_ruler_auto_select || !m_canvas)
		return nullptr;
	VolumeData* cur_vol = CurVol();
	VolumeData* best = nullptr;
	double best_val = 0.0;
	for (VolumeData* vd : m_canvas->PopVolumeList())
	{
		if (!vd || !vd->GetDisp()) continue;
		Point p;
		double val = 0.0;
		//Maximum Intensity, value after the transfer function (as drawn)
		if (GetPointVolume(p, mx, my, vd, 1, true, 0.5, &val) <= 0.0)
			continue;
		if (!best || val > best_val || (val == best_val && vd == cur_vol))
		{
			best = vd;
			best_val = val;
		}
	}
	if (best && best != cur_vol)
	{
		Q_EMIT volumeAutoSelected(best);
		if (m_canvas && m_canvas->GetCurrentVolume() != best)
			m_canvas->SetCurrentVolume(best);
	}
	return best;
}

bool RulerTool::AutoSelectForPaintRuler(int mx, int my)
{
	VolumeData* before = CurVol();
	m_paint_auto_vd = AutoSelectVolume(mx, my);
	return m_paint_auto_vd && m_paint_auto_vd != before;
}

void RulerTool::AddRulerPoint(int mx, int my)
{
	//Auto-select: the volume under the click becomes the current volume
	VolumeData* auto_vd = AutoSelectVolume(mx, my);
	VolumeData* cur_vol = auto_vd ? auto_vd : CurVol();
	if (m_ruler_type == 3)
	{
		Point p1, p2;
		Ruler* ruler = new Ruler();
		ruler->SetRulerType(m_ruler_type);
		double t = GetPointVolumeBox2(p1, p2, mx, my, cur_vol);
		ruler->AddPoint(p1);
		ruler->AddPoint(p2);
		if (t > 0.0 && cur_vol)
		{
			ruler->SetPointVolName(0, cur_vol->GetName());
			ruler->SetPointVolName(1, cur_vol->GetName());
		}
		ruler->SetTimeDep(m_ruler_time_dep);
		ruler->SetTime(m_tseq_cur_num);
		m_ruler_list.push_back(ruler);
	}
	else
	{
		Point p;
		//volume the point is picked on (empty: view plane)
		VVDString pvol;
		if (m_point_volume_mode)
		{
			double t = GetPointVolume(p, mx, my, cur_vol,
				m_point_volume_mode, m_ruler_use_transf);
			//with narrow clipping the accumulated opacity may never reach
			//the threshold; fall back to the maximum intensity point,
			//which stays inside the clipped region, before the view plane
			if (t <= 0.0 && m_point_volume_mode == 2)
				t = GetPointVolume(p, mx, my, cur_vol,
					1, m_ruler_use_transf);
			if (t > 0.0 && cur_vol)
				pvol = cur_vol->GetName();
			if (t <= 0.0)
			{
				t = GetPointPlane(p, mx, my);
				if (t <= 0.0)
					return;
			}
		}
		else
		{
			double t = GetPointPlane(p, mx, my);
			if (t <= 0.0)
				return;
			//Auto-select with the View Plane method: the volume picked
			if (auto_vd)
				pvol = auto_vd->GetName();
		}

		bool new_ruler = true;
		if (m_ruler_list.size())
		{
			Ruler* ruler = m_ruler_list[m_ruler_list.size() - 1];
			if (ruler && !ruler->GetFinished())
			{
				if (ruler->AddPoint(p))
					ruler->SetPointVolName(ruler->GetNumPoint() - 1, pvol);
				new_ruler = false;
			}
		}
		if (new_ruler)
		{
			Ruler* ruler = new Ruler();
			ruler->SetRulerType(m_ruler_type);
			ruler->AddPoint(p);
			ruler->SetPointVolName(0, pvol);
			ruler->SetTimeDep(m_ruler_time_dep);
			ruler->SetTime(m_tseq_cur_num);
			m_ruler_list.push_back(ruler);
		}
	}

	// wx: vr_frame->GetMeasureDlg()->GetSettings(m_vrv)
	Q_EMIT rulersChanged();
}

void RulerTool::AddPaintRulerPoint(VolumeSelector* sel)
{
	if (!sel || !sel->ProcessSel(0.01))
		return;
	Point center;
	double size;
	sel->GetCenter(center);
	sel->GetSize(size);
	//Auto-select: the volume picked at the stroke's press
	VVDString pvol;
	if (m_paint_auto_vd && m_canvas)
	{
		const std::vector<VolumeData*>& vols = m_canvas->Volumes();
		if (std::find(vols.begin(), vols.end(), m_paint_auto_vd) != vols.end())
			pvol = m_paint_auto_vd->GetName();
	}
	m_paint_auto_vd = nullptr;

	bool new_ruler = true;
	if (m_ruler_list.size())
	{
		Ruler* ruler = m_ruler_list[m_ruler_list.size() - 1];
		if (ruler && !ruler->GetFinished())
		{
			if (ruler->AddPoint(center) && !pvol.empty())
				ruler->SetPointVolName(ruler->GetNumPoint() - 1, pvol);
			ruler->AddInfoNames(VVDString(QString::asprintf("\tv%d", ruler->GetNumPoint() - 1).toStdWString()));
			ruler->AddInfoValues(VVDString(QString::asprintf("\t%.0f", size).toStdWString()));
			new_ruler = false;
		}
	}
	if (new_ruler)
	{
		Ruler* ruler = new Ruler();
		ruler->SetRulerType(m_ruler_type);
		ruler->AddPoint(center);
		if (!pvol.empty())
			ruler->SetPointVolName(0, pvol);
		ruler->SetTimeDep(m_ruler_time_dep);
		ruler->SetTime(m_tseq_cur_num);
		ruler->AddInfoNames(VVDString(L"v0"));
		ruler->AddInfoValues(VVDString(QString::asprintf("%.0f", size).toStdWString()));
		m_ruler_list.push_back(ruler);
	}
	// wx: vr_frame->GetMeasureDlg()->GetSettings(m_vrv)
	Q_EMIT rulersChanged();
	Refresh();
}

// ---------------------------------------------------------------------------
// picking. The wx functions save m_mv_mat/m_proj_mat, call HandleProjection +
// HandleCamera + the object transform and restore the matrices afterwards;
// here the matrices of the last frame are used (see RulerTool.h).
// ---------------------------------------------------------------------------

bool RulerTool::PickState(int& nx, int& ny) const
{
	nx = m_nx;
	ny = m_ny;
	return m_have_frame && nx > 0 && ny > 0;
}

double RulerTool::GetPointVolume(Point& mp, int mx, int my,
	VolumeData* vd, int mode, bool use_transf, double thresh, double* max_value)
{
	if (max_value) *max_value = 0.0;
	if (!vd)
		return -1.0;
	Texture* tex = vd->GetTexture();
	if (!tex) return -1.0;
	Nrrd* nrrd = tex->get_nrrd_raw(0);
	if (!nrrd) return -1.0;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return -1.0;

	int nx, ny;
	if (!PickState(nx, ny))
		return -1.0;

	glm::mat4 mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int tmp_xx, tmp_yy, tmp_zz;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	//volume bounding box
	BBox bbox = vd->GetBounds();
	Vector vv = mp2 - mp1;
	vv.normalize();
	Point hit;
	double max_int = 0.0;
	double alpha = 0.0;
	double value = 0.0;
	std::vector<Plane*>* planes = 0;
	double mspc = 1.0;
	double return_val = -1.0;
	if (vd->GetSampleRate() > 0.0)
		mspc = sqrt(1.0 / (resx * resx) + 1.0 / (resy * resy) + 1.0 / (resz * resz)) / 2.0;
	if (vd->GetVR())
		planes = vd->GetVR()->get_planes();
	if (bbox.intersect(mp1, vv, hit))
	{
		Transform* textrans = tex->transform();
		Vector vv2 = textrans->unproject(vv);
		vv2.normalize();
		Point hp1 = textrans->unproject(hit);
		while (true)
		{
			tmp_xx = int(hp1.x() * resx);
			tmp_yy = int(hp1.y() * resy);
			tmp_zz = int(hp1.z() * resz);
			if (mode == 1 &&
				tmp_xx == xx && tmp_yy == yy && tmp_zz == zz)
			{
				//same, skip
				hp1 += vv2 * mspc;
				continue;
			}
			else
			{
				xx = tmp_xx;
				yy = tmp_yy;
				zz = tmp_zz;
			}
			//out of bound, stop
			if (xx<0 || xx>resx ||
				yy<0 || yy>resy ||
				zz<0 || zz>resz)
				break;

			bool inside = true;
			if (planes)
			{
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(hp1) < 0.0)
					{
						inside = false;
						break;
					}
				}
			}
			if (inside)
			{
				xx = xx == resx ? resx - 1 : xx;
				yy = yy == resy ? resy - 1 : yy;
				zz = zz == resz ? resz - 1 : zz;

				if (use_transf)
					value = vd->GetTransferedValue(xx, yy, zz);
				else
					value = vd->GetOriginalValue(xx, yy, zz);

				if (mode == 1)
				{
					if (value > max_int)
					{
						mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
						mp = textrans->project(mp);
						max_int = value;
					}
				}
				else if (mode == 2)
				{
					//accumulate
					if (value > 0.0)
					{
						alpha = 1.0 - pow(Clamp(1.0 - value, 0.0, 1.0), vd->GetSampleRate());
						max_int += alpha * (1.0 - max_int);
						//use the sample position instead of the voxel center:
						//the voxel center can fall outside a clipping plane
						//cutting through the voxel
						mp = textrans->project(hp1);
					}
					if (max_int >= thresh)
						break;
				}
			}
			hp1 += vv2 * mspc;
		}
	}

	if (mode == 1)
	{
		if (max_value) *max_value = max_int;
		if (max_int > 0.0)
		{
			if (planes)
			{
				//the returned point is a voxel center, which can fall outside
				//a clipping plane cutting through the voxel; snap it back
				//inside the clipped region
				Point tp = tex->transform()->unproject(mp);
				bool clamped = false;
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(tp) < 0.0)
					{
						tp = (*planes)[i]->project(tp);
						clamped = true;
					}
				}
				if (clamped)
					mp = tex->transform()->project(tp);
			}
			return_val = (mp - mp1).length();
		}
	}
	else if (mode == 2)
	{
		if (max_int >= thresh)
			return_val = (mp - mp1).length();
	}

	return return_val;
}

// wx VRenderVulkanView::GetPointAndLabelMax. wx recomputes the object
// model-view with the object rotations applied in z, y, x order; the frame's
// m_mv_mat (x, y, z) is the same matrix since the object rotations are 0.
double RulerTool::GetPointAndLabelMax(Point& mp, int& lblval, int mx, int my, VolumeData* vd, const std::vector<VolumeData*>& ref)
{
	lblval = 0;
	if (!vd)
		return -1.0;
	Texture* tex = vd->GetTexture();
	if (!tex) return -1.0;
	Nrrd* nrrd = tex->get_nrrd_raw(tex->nlabel());
	if (!nrrd) return -1.0;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return -1.0;

	int nx, ny;
	if (!PickState(nx, ny))
		return -1.0;

	glm::mat4 mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int tmp_xx, tmp_yy, tmp_zz;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	//volume bounding box
	BBox bbox = vd->GetBounds();
	Vector vv = mp2 - mp1;
	vv.normalize();
	Point hit;
	int p_int = 0;
	double max_int = 0.0;
	int value = 0;
	std::vector<Plane*>* planes = 0;
	double mspc = 1.0;
	double return_val = -1.0;
	if (vd->GetSampleRate() > 0.0)
		mspc = sqrt(1.0 / (resx * resx) + 1.0 / (resy * resy) + 1.0 / (resz * resz)) / 2.0;
	if (vd->GetVR())
		planes = vd->GetVR()->get_planes();
	if (bbox.intersect(mp1, vv, hit))
	{
		Transform* textrans = tex->transform();
		Vector vv2 = textrans->unproject(vv);
		vv2.normalize();
		Point hp1 = textrans->unproject(hit);
		while (true)
		{
			tmp_xx = int(hp1.x() * resx);
			tmp_yy = int(hp1.y() * resy);
			tmp_zz = int(hp1.z() * resz);
			if (tmp_xx == xx && tmp_yy == yy && tmp_zz == zz)
			{
				//same, skip
				hp1 += vv2 * mspc;
				continue;
			}
			else
			{
				xx = tmp_xx;
				yy = tmp_yy;
				zz = tmp_zz;
			}
			//out of bound, stop
			if (xx<0 || xx>resx ||
				yy<0 || yy>resy ||
				zz<0 || zz>resz)
				break;

			bool inside = true;
			if (planes)
			{
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(hp1) < 0.0)
					{
						inside = false;
						break;
					}
				}
			}
			if (inside)
			{
				xx = xx == resx ? resx - 1 : xx;
				yy = yy == resy ? resy - 1 : yy;
				zz = zz == resz ? resz - 1 : zz;

				value = vd->GetLabellValue(xx, yy, zz);

				if (value > 0 && vd->GetSegmentMask(value) > 0)
				{
					bool ismax = false;
					for (VolumeData* v : ref)
					{
						double dval = v->GetOriginalValue(xx, yy, zz);
						if (dval > max_int)
						{
							max_int = dval;
							ismax = true;
						}
					}
					if (ismax)
					{
						mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
						mp = textrans->project(mp);
						p_int = value;
					}
				}
			}
			hp1 += vv2 * mspc;
		}
	}

	if (p_int > 0)
		return_val = (mp - mp1).length();

	lblval = p_int;

	return return_val;
}

double RulerTool::GetPointAndIntVolume(Point& mp, double& intensity, bool normalize, int mx, int my, VolumeData* vd, double thresh)
{
	if (!vd)
		return -1.0;
	Texture* tex = vd->GetTexture();
	if (!tex) return -1.0;
	Nrrd* nrrd = tex->get_nrrd_raw(0);
	if (!nrrd) return -1.0;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return -1.0;

	int nx, ny;
	if (!PickState(nx, ny))
		return -1.0;

	glm::mat4 mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int tmp_xx, tmp_yy, tmp_zz;
	double spcx, spcy, spcz;
	vd->GetSpacings(spcx, spcy, spcz);
	int resx, resy, resz;
	vd->GetResolution(resx, resy, resz);
	//volume bounding box
	BBox bbox = vd->GetBounds();
	Vector vv = mp2 - mp1;
	vv.normalize();
	Point hit;
	double p_int = 0.0;
	double value = 0.0;
	std::vector<Plane*>* planes = 0;
	double mspc = 1.0;
	double return_val = -1.0;

	double scx, scy, scz;
	scx = scy = scz = 1.0;
	if (vd->isBrxml())
	{
		int64_t vnx = (int64_t)(nrrd->axis[0].size);
		int64_t vny = (int64_t)(nrrd->axis[1].size);
		int64_t vnz = (int64_t)(nrrd->axis[2].size);
		scx = (double)vnx / resx; scy = (double)vny / resy; scz = (double)vnz / resz;
	}

	if (vd->GetSampleRate() > 0.0)
		mspc = sqrt(1.0 / (resx * scx * resx * scx) + 1.0 / (resy * scy * resy * scy) + 1.0 / (resz * scz * resz * scz)) / 2.0;
	if (vd->GetVR())
		planes = vd->GetVR()->get_planes();
	if (bbox.intersect(mp1, vv, hit))
	{
		Transform* textrans = tex->transform();
		Vector vv2 = textrans->unproject(vv);
		vv2.normalize();
		Point hp1 = textrans->unproject(hit);
		while (true)
		{
			tmp_xx = int(hp1.x() * resx);
			tmp_yy = int(hp1.y() * resy);
			tmp_zz = int(hp1.z() * resz);
			if ((!vd->isBrxml() && tmp_xx == xx && tmp_yy == yy && tmp_zz == zz) ||
				(vd->isBrxml() && (int)(tmp_xx * scx) == (int)(xx * scx) && (int)(tmp_yy * scy) == (int)(yy * scy) && (int)(tmp_zz * scz) == (int)(zz * scz)))
			{
				//same, skip
				hp1 += vv2 * mspc;
				continue;
			}
			else
			{
				xx = tmp_xx;
				yy = tmp_yy;
				zz = tmp_zz;
			}
			//out of bound, stop
			if (xx<0 || xx>resx ||
				yy<0 || yy>resy ||
				zz<0 || zz>resz)
				break;

			bool inside = true;
			if (planes)
			{
				for (int i = 0; i < 6; i++)
				{
					if ((*planes)[i] &&
						(*planes)[i]->eval_point(hp1) < 0.0)
					{
						inside = false;
						break;
					}
				}
			}
			if (inside)
			{
				xx = xx == resx ? resx - 1 : xx;
				yy = yy == resy ? resy - 1 : yy;
				zz = zz == resz ? resz - 1 : zz;

				value = vd->GetOriginalValue(xx, yy, zz, normalize);

				if (vd->GetColormapMode() == 3)
				{
					unsigned char r = 0, g = 0, b = 0;
					vd->GetRenderedIDColor(r, g, b, (int)value);
					if (r == 0 && g == 0 && b == 0)
					{
						hp1 += vv2 * mspc;
						continue;
					}
				}

				if (value >= thresh)
				{
					mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
					mp = textrans->project(mp);
					p_int = value;
					break;
				}
			}
			hp1 += vv2 * mspc;
		}
	}

	if (p_int >= thresh)
		return_val = (mp - mp1).length();

	intensity = p_int;

	return return_val;
}

double RulerTool::GetPointVolumeBox(Point& mp, int mx, int my, VolumeData* vd, bool /*calc_mats*/)
{
	if (!vd)
		return -1.0;
	int nx, ny;
	if (!PickState(nx, ny))
		return -1.0;
	if (!vd->GetVR() || !vd->GetTexture())
		return -1.0;
	std::vector<Plane*>* planes = vd->GetVR()->get_planes();
	if (planes->size() != 6)
		return -1.0;

	Transform mv;
	Transform p;
	glm::mat4 mv_temp = m_mv_mat;
	Transform* tform = vd->GetTexture()->transform();
	double mvmat[16];
	tform->get_trans(mvmat);

	glm::mat4 mv_mat2 = glm::mat4(
		mvmat[0], mvmat[4], mvmat[8], mvmat[12],
		mvmat[1], mvmat[5], mvmat[9], mvmat[13],
		mvmat[2], mvmat[6], mvmat[10], mvmat[14],
		mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
	mv_temp = mv_temp * mv_mat2;

	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);
	Vector ray_d = mp1 - mp2;
	ray_d.normalize();
	Ray ray(mp1, ray_d);
	double mint = -1.0;
	double t;
	//for each plane, calculate the intersection point
	Plane* plane = 0;
	Point pp;//a point on plane
	int i, j;
	bool pp_out;
	for (i = 0; i < 6; i++)
	{
		plane = (*planes)[i];
		FLIVR::Vector vec = plane->normal();
		FLIVR::Point pnt = plane->get_point();
		if (ray.planeIntersectParameter(vec, pnt, t))
		{
			pp = ray.parameter(t);

			pp_out = false;
			//determine if the point is inside the box
			for (j = 0; j < 6; j++)
			{
				if (j == i)
					continue;
				if ((*planes)[j]->eval_point(pp) < 0)
				{
					pp_out = true;
					break;
				}
			}

			if (!pp_out)
			{
				if (t > mint)
				{
					mp = pp;
					mint = t;
				}
			}
		}
	}

	mp = tform->transform(mp);

	return mint;
}

double RulerTool::GetPointVolumeBox2(Point& p1, Point& p2, int mx, int my, VolumeData* vd)
{
	if (!vd)
		return -1.0;
	int nx, ny;
	if (!PickState(nx, ny))
		return -1.0;
	if (!vd->GetVR() || !vd->GetTexture())
		return -1.0;
	std::vector<Plane*>* planes = vd->GetVR()->get_planes();
	if (planes->size() != 6)
		return -1.0;

	glm::mat4 mv_temp = m_mv_mat;
	Transform* tform = vd->GetTexture()->transform();
	double mvmat[16];
	tform->get_trans(mvmat);
	glm::mat4 mv_mat2 = glm::mat4(
		mvmat[0], mvmat[4], mvmat[8], mvmat[12],
		mvmat[1], mvmat[5], mvmat[9], mvmat[13],
		mvmat[2], mvmat[6], mvmat[10], mvmat[14],
		mvmat[3], mvmat[7], mvmat[11], mvmat[15]);
	mv_temp = mv_temp * mv_mat2;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to object space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	mp1 = mv.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	mp2 = mv.transform(mp2);
	Vector ray_d = mp1 - mp2;
	ray_d.normalize();
	Ray ray(mp1, ray_d);
	double mint = -1.0;
	double maxt = std::numeric_limits<double>::max();
	double t;
	//for each plane, calculate the intersection point
	Plane* plane = 0;
	Point pp;//a point on plane
	int i, j;
	bool pp_out;
	for (i = 0; i < 6; i++)
	{
		plane = (*planes)[i];
		FLIVR::Vector vec = plane->normal();
		FLIVR::Point pnt = plane->get_point();
		if (ray.planeIntersectParameter(vec, pnt, t))
		{
			pp = ray.parameter(t);

			pp_out = false;
			//determine if the point is inside the box
			for (j = 0; j < 6; j++)
			{
				if (j == i)
					continue;
				if ((*planes)[j]->eval_point(pp) < 0)
				{
					pp_out = true;
					break;
				}
			}

			if (!pp_out)
			{
				if (t > mint)
				{
					p1 = pp;
					mint = t;
				}
				if (t < maxt)
				{
					p2 = pp;
					maxt = t;
				}
			}
		}
	}

	p1 = tform->transform(p1);
	p2 = tform->transform(p2);

	return mint;
}

double RulerTool::GetPointPlane(Point& mp, int mx, int my, Point* planep, bool /*calc_mats*/)
{
	int nx, ny;
	if (!PickState(nx, ny))
		return -1.0;

	glm::mat4 mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	Vector n(0.0, 0.0, 1.0);
	Point center(0.0, 0.0, -m_distance);
	if (planep)
	{
		center = *planep;
		center = mv.transform(center);
	}
	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	p.invert();
	mv.invert();
	//transform mp1 and mp2 to eye space
	Point mp1(x, y, 0.0);
	mp1 = p.transform(mp1);
	Point mp2(x, y, 1.0);
	mp2 = p.transform(mp2);
	FLIVR::Vector vec = mp2 - mp1;
	Ray ray(mp1, vec);
	double t = 0.0;
	if (ray.planeIntersectParameter(n, center, t))
		mp = ray.parameter(t);
	//transform mp to world space
	mp = mv.transform(mp);

	return (mp - mp1).length();
}

Point* RulerTool::GetEditingRulerPoint(int mx, int my)
{
	Point* point = 0;

	int nx, ny;
	if (!PickState(nx, ny))
		return 0;

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	double aspect = (double)nx / (double)ny;

	glm::mat4 mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	VolumeData* cur_vol = CurVol();
	int i, j;
	Point ptemp;
	bool found = false;
	for (i = 0; i < (int)m_ruler_list.size() && !found; i++)
	{
		Ruler* ruler = m_ruler_list[i];
		if (!ruler) continue;

		bool skip = true;
		if (cur_vol)
		{
			for (int k = 0; k < ruler->GetNumPoint(); ++k)
			{
				if (cur_vol->InsideClippingPlanes(*(ruler->GetPoint(k))))
				{
					skip = false;
					break;
				}
			}
		}
		else
			skip = false;

		if (skip)
			continue;

		for (j = 0; j < ruler->GetNumPoint(); j++)
		{
			point = ruler->GetPoint(j);
			if (!point) continue;
			// wx dereferences m_cur_vol unconditionally here
			if (cur_vol && !cur_vol->InsideClippingPlanes(*point)) continue;
			ptemp = *point;
			ptemp = mv.transform(ptemp);
			ptemp = p.transform(ptemp);
			if (ptemp.z() <= 0.0 || ptemp.z() >= 1.0)
				continue;
			if (x<ptemp.x() + 0.02 / aspect &&
				x>ptemp.x() - 0.02 / aspect &&
				y<ptemp.y() + 0.02 &&
				y>ptemp.y() - 0.02)
			{
				found = true;
				break;
			}
		}
	}

	if (!found && m_draw_landmarks)
	{
		for (i = 0; i < (int)m_landmarks.size() && !found; i++)
		{
			Ruler* ruler = m_landmarks[i];
			if (!ruler) continue;

			for (j = 0; j < ruler->GetNumPoint(); j++)
			{
				point = ruler->GetPoint(j);
				if (!point) continue;
				ptemp = *point;
				ptemp = mv.transform(ptemp);
				ptemp = p.transform(ptemp);
				if (ptemp.z() <= 0.0 || ptemp.z() >= 1.0)
					continue;
				if (x<ptemp.x() + 0.02 / aspect &&
					x>ptemp.x() - 0.02 / aspect &&
					y<ptemp.y() + 0.02 &&
					y>ptemp.y() - 0.02)
				{
					found = true;
					break;
				}
			}
		}
	}

	if (found)
		return point;
	else
		return 0;
}

bool RulerTool::SwitchRulerBalloonVisibility_Point(int mx, int my)
{
	Point* point = 0;

	int nx, ny;
	if (!PickState(nx, ny))
		return false;

	double x, y;
	x = double(mx) * 2.0 / double(nx) - 1.0;
	y = double(my) * 2.0 / double(ny) - 1.0;
	double aspect = (double)nx / (double)ny;

	glm::mat4 mv_temp = m_mv_mat;

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(mv_temp));
	p.set(glm::value_ptr(m_proj_mat));

	int i, j;
	Point ptemp;
	bool found = false;
	for (i = 0; i < (int)m_ruler_list.size() && !found; i++)
	{
		Ruler* ruler = m_ruler_list[i];
		if (!ruler) continue;

		for (j = 0; j < ruler->GetNumPoint(); j++)
		{
			point = ruler->GetPoint(j);
			if (!point) continue;
			ptemp = *point;
			ptemp = mv.transform(ptemp);
			ptemp = p.transform(ptemp);
			if (ptemp.z() <= 0.0 || ptemp.z() >= 1.0)
				continue;
			if (x<ptemp.x() + 0.02 / aspect &&
				x>ptemp.x() - 0.02 / aspect &&
				y<ptemp.y() + 0.02 &&
				y>ptemp.y() - 0.02)
			{
				ruler->SetBalloonVisibility(j, !ruler->GetBalloonVisibility(j));
				found = true;
				break;
			}
		}
	}

	if (!found)
	{
		for (i = 0; i < (int)m_landmarks.size() && !found; i++)
		{
			Ruler* ruler = m_landmarks[i];
			if (!ruler) continue;

			for (j = 0; j < ruler->GetNumPoint(); j++)
			{
				point = ruler->GetPoint(j);
				if (!point) continue;
				ptemp = *point;
				ptemp = mv.transform(ptemp);
				ptemp = p.transform(ptemp);
				if (ptemp.z() <= 0.0 || ptemp.z() >= 1.0)
					continue;
				if (x<ptemp.x() + 0.02 / aspect &&
					x>ptemp.x() - 0.02 / aspect &&
					y<ptemp.y() + 0.02 &&
					y>ptemp.y() - 0.02)
				{
					ruler->SetBalloonVisibility(j, !ruler->GetBalloonVisibility(j));
					found = true;
					break;
				}
			}
		}
	}

	return found;
}

// ---------------------------------------------------------------------------
// mouse (VRenderVulkanView::OnMouse, m_int_mode 5 / 6)
// ---------------------------------------------------------------------------

bool RulerTool::MousePress(QMouseEvent* e)
{
	const QPoint pos = e->position().toPoint();
	const bool alt = e->modifiers() & Qt::AltModifier;

	if (e->button() == Qt::LeftButton)
	{
		if (m_int_mode == 6)
			m_editing_ruler_point = GetEditingRulerPoint(pos.x(), pos.y());

		if ((m_int_mode == 5 && alt) ||
			(m_int_mode == 6 && m_editing_ruler_point == 0))
		{
			// camera (wx m_pick / rotation)
			m_camera_drag = true;
			return false;
		}
		m_point_drag = (m_int_mode == 6);
		return true;
	}

	// right / middle: wx only records the position; dragging is the camera
	// unless a ruler is being created
	if (m_int_mode == 5 && !alt)
		return true;
	m_camera_drag = true;
	return false;
}

bool RulerTool::MouseMove(QMouseEvent* e)
{
	if (e->buttons() == Qt::NoButton)
		return false;
	if (m_camera_drag)
		return false;

	if (m_int_mode == 6)
	{
		if ((e->buttons() & Qt::LeftButton) &&
			m_editing_ruler_point)
		{
			const QPoint pos = e->position().toPoint();
			//Auto-select: the volume under the pointer becomes current
			VolumeData* auto_vd = AutoSelectVolume(pos.x(), pos.y());
			VolumeData* cur_vol = auto_vd ? auto_vd : CurVol();
			Point point;
			bool failed = false;
			VVDString pvol;
			if (m_point_volume_mode)
			{
				double t = GetPointVolume(point,
					pos.x(), pos.y(),
					cur_vol, m_point_volume_mode,
					m_ruler_use_transf);
				//fall back to the maximum intensity point, which stays
				//inside the clipped region, before the view plane
				if (t <= 0.0 && m_point_volume_mode == 2)
					t = GetPointVolume(point,
						pos.x(), pos.y(),
						cur_vol, 1, m_ruler_use_transf);
				if (t > 0.0 && cur_vol)
					pvol = cur_vol->GetName();
				if (t <= 0.0)
					t = GetPointPlane(point,
						pos.x(), pos.y(),
						m_editing_ruler_point);
				if (t <= 0.0)
					failed = true;
			}
			else
			{
				double t = GetPointPlane(point,
					pos.x(), pos.y(),
					m_editing_ruler_point);
				if (t <= 0.0)
					failed = true;
				else if (auto_vd)
					pvol = auto_vd->GetName();
			}

			if (!failed)
			{
				m_editing_ruler_point->x(point.x());
				m_editing_ruler_point->y(point.y());
				m_editing_ruler_point->z(point.z());
				//the moved point now lies on the volume it was re-picked on
				for (Ruler* r : m_ruler_list)
				{
					if (!r) continue;
					for (int k = 0; k < r->GetNumPoint(); ++k)
						if (r->GetPoint(k) == m_editing_ruler_point)
							r->SetPointVolName(k, pvol);
				}
				Refresh();
				Q_EMIT rulersChanged();
			}
		}
		return true;
	}
	return true;   // mode 5: no camera while creating rulers (wx)
}

bool RulerTool::HandlesRightClick(QMouseEvent* e) const
{
	return m_int_mode == 5 && !(e->modifiers() & Qt::AltModifier) && !m_camera_drag;
}

bool RulerTool::MouseRelease(QMouseEvent* e)
{
	const QPoint pos = e->position().toPoint();
	const bool alt = e->modifiers() & Qt::AltModifier;
	const bool camera = m_camera_drag;
	if (e->buttons() == Qt::NoButton)
		m_camera_drag = false;

	if (e->button() == Qt::LeftButton)
	{
		if (m_int_mode == 5 && !alt && !camera)
		{
			//add one point to a ruler
			AddRulerPoint(pos.x(), pos.y());
			Refresh();
			return true;
		}
		else if (m_int_mode == 6)
		{
			bool was_point = m_point_drag;
			m_editing_ruler_point = 0;
			m_point_drag = false;
			Refresh();
			return was_point && !camera;
		}
		return !camera;
	}
	if (e->button() == Qt::RightButton)
	{
		if (m_int_mode == 5 && !alt && !camera)
		{
			if (GetRulerFinished())
			{
				SetIntMode(1);
				Q_EMIT rulersChanged();
			}
			else
			{
				AddRulerPoint(pos.x(), pos.y());
				FinishRuler();
				Q_EMIT rulersChanged();
			}
			Refresh();
			return true;
		}
		return !camera;
	}
	// middle
	return !camera;
}

// ---------------------------------------------------------------------------
// drawing (VRenderVulkanView::DrawRulers, called after the scene like
// VRenderVulkanView::Draw does when m_draw_rulers is set)
// ---------------------------------------------------------------------------

void RulerTool::DrawOverlay(RenderCanvas* canvas, int nx, int ny)
{
	if (!canvas) return;
	// the frame's object-space matrices: used for drawing and for picking
	m_mv_mat = canvas->GetMvMat();
	m_proj_mat = canvas->GetProjMat();
	m_nx = nx;
	m_ny = ny;
	// HandleProjection: m_distance = m_radius / tan(d2r(m_aov / 2.0)) / m_scale_factor
	double sf = canvas->GetScaleFactor();
	double tn = std::tan(deg2rad(canvas->GetAov() / 2.0));
	if (sf != 0.0 && tn != 0.0)
		m_distance = canvas->GetRadius() / tn / sf;
	m_have_frame = true;

	EnsureTextRenderer();
	if (m_draw_rulers)
		DrawRulers();
}

void RulerTool::DrawRulers()
{
	if (!m_text_renderer || !m_canvas)
		return;
	std::shared_ptr<VVulkan> vulkan = m_canvas->GetVulkan();
	std::shared_ptr<Vulkan2dRender> v2drender = m_canvas->GetV2DRender();
	if (!vulkan || !v2drender)
		return;
	const std::unique_ptr<vks::VFrameBuffer>& cur_framebuf = vulkan->frameBuffers[vulkan->currentBuffer];
	if (!cur_framebuf)
		return;

	VolumeData* cur_vol = CurVol();
	int nx = m_nx;
	int ny = m_ny;
	float sx, sy;
	sx = 2.0 / nx;
	sy = 2.0 / ny;
	float w = m_text_renderer->GetSize() / 4.0f;
	float px, py, p2x, p2y;

	std::vector<Vulkan2dRender::Vertex> verts;
	std::vector<uint32_t> index;
	int vert_num = 0;
	for (size_t i = 0; i < m_ruler_list.size(); ++i)
		if (m_ruler_list[i])
			vert_num += m_ruler_list[i]->GetNumPoint();
	verts.reserve(vert_num * 4);
	index.reserve(vert_num * 8);

	Transform mv;
	Transform p;
	mv.set(glm::value_ptr(m_mv_mat));
	p.set(glm::value_ptr(m_proj_mat));
	Point p1, p2;
	Color color;
	Color text_color = GetTextColor();

	double spcx = 1.0;
	double spcy = 1.0;
	double spcz = 1.0;

	if (cur_vol)
	{
		Texture* vtex = cur_vol->GetTexture();
		if (vtex && vtex->isBrxml())
		{
			BRKXMLReader* br = (BRKXMLReader*)cur_vol->GetReader();
			br->SetLevel(0);
			spcx = br->GetXSpc();
			spcy = br->GetYSpc();
			spcz = br->GetZSpc();
		}
	}

	// one ruler / landmark set: points, segments with arrow heads, name,
	// balloons, protractor arc (the wx code is the same for both lists
	// except for the clipping skip and the arrow heads)
	auto draw_balloon = [&](Ruler* ruler, int j, const Point& pp2, uint32_t& vnum)
	{
		if (!ruler->GetBalloonVisibility(j))
			return;
		vvd::StringArray ann = ruler->GetAnnotations(j, cur_vol ? cur_vol->GetAnnotation() : std::vector<AnnotationDB>(), spcx, spcy, spcz);
		int lnum = (int)ann.size();
		if (lnum <= 0)
			return;
		p2x = pp2.x() * nx / 2.0;
		p2y = pp2.y() * ny / 2.0;

		double margin_x = 5 + w;
		double margin_top = w;
		double margin_bottom = w;
		double line_spc = 2;
		double maxlw, lw, lh;

		lh = m_text_renderer->GetSize();
		margin_bottom += lh / 2;
		maxlw = 0.0;

		for (int i = 0; i < lnum; i++)
		{
			std::wstring wstr_temp = ann.Item(i);
			m_text_renderer->RenderText(cur_framebuf,
				wstr_temp, color,
				p2x * sx + margin_x * sx, p2y * sy + (lh * (i + 1) + line_spc * i + margin_top) * sy);
			lw = m_text_renderer->RenderTextDims(wstr_temp).width;
			if (lw > maxlw) maxlw = lw;
		}

		float bw = (margin_x * 2 + maxlw);
		float bh = (margin_top + margin_bottom + lh * lnum + line_spc * (lnum - 1));

		px = (pp2.x() + 1.0) * nx / 2.0;
		py = (pp2.y() + 1.0) * ny / 2.0;
		verts.push_back(V(px, py, color));
		verts.push_back(V(px + bw, py, color));
		verts.push_back(V(px + bw, py + bh, color));
		verts.push_back(V(px, py + bh, color));
		uint32_t tmp[] = { vnum + 0, vnum + 1, vnum + 1, vnum + 2, vnum + 2, vnum + 3, vnum + 3, vnum + 0 };
		index.insert(index.end(), tmp, tmp + 8);
		vnum += 4;
	};
	auto draw_protractor = [&](Ruler* ruler, uint32_t& vnum)
	{
		if (ruler->GetRulerType() == 4 &&
			ruler->GetNumPoint() >= 3)
		{
			Point center = *(ruler->GetPoint(1));
			Vector v1 = *(ruler->GetPoint(0)) - center;
			Vector v2 = *(ruler->GetPoint(2)) - center;
			double len = Min(v1.length(), v2.length());
			if (len > w)
			{
				v1.normalize();
				v2.normalize();
				p1 = center + v1 * w;
				p1 = mv.transform(p1);
				p1 = p.transform(p1);
				px = (p1.x() + 1.0) * nx / 2.0;
				py = (p1.y() + 1.0) * ny / 2.0;
				verts.push_back(V(px, py, color));
				p1 = center + v2 * w;
				p1 = mv.transform(p1);
				p1 = p.transform(p1);
				px = (p1.x() + 1.0) * nx / 2.0;
				py = (p1.y() + 1.0) * ny / 2.0;
				verts.push_back(V(px, py, color));
				index.push_back(vnum); index.push_back(vnum + 1);
				vnum += 2;
			}
		}
	};

	uint32_t vnum = 0;
	for (size_t i = 0; i < m_ruler_list.size(); i++)
	{
		Ruler* ruler = m_ruler_list[i];
		if (!ruler) continue;
		if (!ruler->GetTimeDep() ||
			(ruler->GetTimeDep() &&
				ruler->GetTime() == m_tseq_cur_num))
		{
			bool skip = true;
			if (cur_vol)
			{
				for (int j = 0; j < ruler->GetNumPoint(); ++j)
				{
					p2 = *(ruler->GetPoint(j));
					if (cur_vol->InsideClippingPlanes(p2))
					{
						skip = false;
						break;
					}
				}
			}
			else
				skip = false;

			if (skip)
				continue;

			if (ruler->GetUseColor())
				color = ruler->GetColor();
			else
				color = text_color;
			for (int j = 0; j < ruler->GetNumPoint(); ++j)
			{
				p2 = *(ruler->GetPoint(j));
				p2 = mv.transform(p2);
				p2 = p.transform(p2);
				if (p2.z() <= 0.0 || p2.z() >= 1.0)
					continue;
				px = (p2.x() + 1.0) * nx / 2.0;
				py = (p2.y() + 1.0) * ny / 2.0;
				verts.push_back(V(px - w, py - w, color));
				verts.push_back(V(px + w, py - w, color));
				verts.push_back(V(px + w, py + w, color));
				verts.push_back(V(px - w, py + w, color));
				uint32_t tmp[] = { vnum + 0, vnum + 1, vnum + 1, vnum + 2, vnum + 2, vnum + 3, vnum + 3, vnum + 0 };
				index.insert(index.end(), tmp, tmp + 8);
				vnum += 4;

				if (j + 1 == ruler->GetNumPoint())
				{
					p2x = p2.x() * nx / 2.0;
					p2y = p2.y() * ny / 2.0;
					m_text_renderer->RenderText(
						cur_framebuf,
						ruler->GetNameDisp(),
						color,
						(p2x + w) * sx, (p2y - w) * sy);
				}
				if (j > 0)
				{
					p1 = *(ruler->GetPoint(j - 1));
					p1 = mv.transform(p1);
					p1 = p.transform(p1);
					if (p1.z() <= 0.0 || p1.z() >= 1.0)
						continue;
					verts.push_back(V(px, py, color));
					p2x = (p1.x() + 1.0) * nx / 2.0;
					p2y = (p1.y() + 1.0) * ny / 2.0;
					verts.push_back(V(p2x, p2y, color));
					index.push_back(vnum); index.push_back(vnum + 1);
					int vid = vnum;
					vnum += 2;

					float vx = p2x - px;
					float vy = p2y - py;
					p2x = 0.8660254f * vx - 0.5f * vy;
					p2y = 0.5f * vx + 0.8660254f * vy;
					float d = 15.0f / sqrt(p2x * p2x + p2y * p2y);
					verts.push_back(V(p2x * d + px, p2y * d + py, color));
					index.push_back(vid); index.push_back(vnum);
					vnum += 1;

					p2x = 0.8660254f * vx + 0.5f * vy;
					p2y = -0.5f * vx + 0.8660254f * vy;
					verts.push_back(V(p2x * d + px, p2y * d + py, color));
					index.push_back(vid); index.push_back(vnum);
					vnum += 1;
				}

				draw_balloon(ruler, j, p2, vnum);
			}
			draw_protractor(ruler, vnum);
		}
	}

	//draw landmarks
	////////////////////////////////////////////////////////////////////////
	std::vector<VolumeData*> displist;
	for (VolumeData* vd : m_canvas->Volumes())
		if (vd && vd->GetDisp())
			displist.push_back(vd);
	std::sort(displist.begin(), displist.end());
	std::sort(m_lm_vdlist.begin(), m_lm_vdlist.end());

	if (m_lm_vdlist != displist)
	{
		for (size_t i = 0; i < m_landmarks.size(); i++)
		{
			delete m_landmarks[i];
			m_landmarks[i] = NULL;
		}
		std::vector<Ruler*>().swap(m_landmarks);

		m_lm_vdlist = displist;

		if (m_lm_vdlist.size() > 0)
		{
			size_t id1;
			std::wstring lmid;
			for (id1 = 1; id1 < m_lm_vdlist.size(); id1++)
			{
				if (m_lm_vdlist[id1]->GetLandmarkNum() > 0)
				{
					m_lm_vdlist[id1]->GetMetadataID(lmid);
					break;
				}
			}

			bool multi = false;
			for (size_t i = id1; i < m_lm_vdlist.size(); i++)
			{
				std::wstring lmid2;
				m_lm_vdlist[i]->GetMetadataID(lmid2);
				if (lmid != lmid2 && m_lm_vdlist[i]->GetLandmarkNum() > 0)
				{
					multi = true;
					break;
				}
			}

			std::map<std::wstring, VD_Landmark> lmmap;
			for (size_t i = 0; i < m_lm_vdlist.size(); i++)
			{
				int lmnum = m_lm_vdlist[i]->GetLandmarkNum();
				for (int j = 0; j < lmnum; j++)
				{
					VD_Landmark lm;
					m_lm_vdlist[i]->GetLandmark(j, lm);
					if (multi)
					{
						std::wstring name = lm.name;
						m_lm_vdlist[i]->GetMetadataID(lmid);
						if (!lmid.empty())
						{
							name += L" (";
							name += lmid;
							name += L")";
							lm.name = name;
						}
					}

					if (lmmap.find(lm.name) == lmmap.end())
					{
						lmmap[lm.name] = lm;
						Ruler* ruler = new Ruler();
						ruler->SetNameDisp(lm.name);
						ruler->SetRulerType(2);
						ruler->AddPoint(Point(lm.x, lm.y, lm.z));
						ruler->SetTimeDep(false);
						m_landmarks.push_back(ruler);
					}
				}
			}
		}
	}

	if (m_draw_landmarks)
	{
		for (int i = 0; i < (int)m_landmarks.size(); i++)
		{
			Ruler* ruler = m_landmarks[i];
			if (!ruler) continue;
			if (!ruler->GetTimeDep() ||
				(ruler->GetTimeDep() &&
					ruler->GetTime() == m_tseq_cur_num))
			{
				if (ruler->GetUseColor())
					color = ruler->GetColor();
				else
					color = text_color;
				for (int j = 0; j < ruler->GetNumPoint(); ++j)
				{
					p2 = *(ruler->GetPoint(j));
					p2 = mv.transform(p2);
					p2 = p.transform(p2);
					if (p2.z() <= 0.0 || p2.z() >= 1.0)
						continue;
					px = (p2.x() + 1.0) * nx / 2.0;
					py = (p2.y() + 1.0) * ny / 2.0;
					verts.push_back(V(px - w, py - w, color));
					verts.push_back(V(px + w, py - w, color));
					verts.push_back(V(px + w, py + w, color));
					verts.push_back(V(px - w, py + w, color));
					uint32_t tmp[] = { vnum + 0, vnum + 1, vnum + 1, vnum + 2, vnum + 2, vnum + 3, vnum + 3, vnum + 0 };
					index.insert(index.end(), tmp, tmp + 8);
					vnum += 4;

					if (j + 1 == ruler->GetNumPoint())
					{
						p2x = p2.x() * nx / 2.0;
						p2y = p2.y() * ny / 2.0;
						m_text_renderer->RenderText(
							cur_framebuf,
							ruler->GetNameDisp(),
							color,
							(p2x + w) * sx, (p2y - w) * sy);
					}
					if (j > 0)
					{
						p1 = *(ruler->GetPoint(j - 1));
						p1 = mv.transform(p1);
						p1 = p.transform(p1);
						if (p1.z() <= 0.0 || p1.z() >= 1.0)
							continue;
						verts.push_back(V(px, py, color));
						px = (p1.x() + 1.0) * nx / 2.0;
						py = (p1.y() + 1.0) * ny / 2.0;
						verts.push_back(V(px, py, color));
						index.push_back(vnum); index.push_back(vnum + 1);
						vnum += 2;
					}

					draw_balloon(ruler, j, p2, vnum);
				}
				draw_protractor(ruler, vnum);
			}
		}
	}

	if (!verts.empty() && !index.empty())
	{
		UpdateV2dObject(vulkan->vulkanDevice, m_ruler_vobj, verts, index);

		// one link of the frame's render-semaphore chain, after the final
		// buffer (RenderCanvas::DrawFinalBuffer) and the text above
		Vulkan2dRender::V2DRenderParams params = v2drender->GetNextV2dRenderSemaphoreSettings();
		vks::VFrameBuffer* current_fbo = cur_framebuf.get();
		params.pipeline =
			v2drender->preparePipeline(
				IMG_SHDR_DRAW_GEOMETRY_COLOR3,
				V2DRENDER_BLEND_OVER_UI,
				current_fbo->attachments[0]->format,
				(int)current_fbo->attachments.size(),
				0,
				current_fbo->attachments[0]->is_swapchain_images,
				VK_PRIMITIVE_TOPOLOGY_LINE_LIST
			);
		params.matrix[0] = glm::ortho(float(0), float(nx), float(0), float(ny));
		// wx: params.clear = m_frame_clear - always false here, the canvas
		// has already drawn (and cleared) the swapchain image this frame
		params.clear = false;
		Color bg = m_canvas->GetBackgroundColor();
		params.clearColor = { (float)bg.r(), (float)bg.g(), (float)bg.b() };

		params.obj = &m_ruler_vobj;

		if (!current_fbo->renderPass)
			current_fbo->replaceRenderPass(params.pipeline.pass);

		v2drender->render(cur_framebuf, params);
	}
}

// ---------------------------------------------------------------------------
// BigWarp landmarks, scatter, warp (VRenderVulkanView::ImportBigWarpCSV,
// ExportBigWarpCSV, ScatterRulers, WarpCurrentVolumeInternal)
// ---------------------------------------------------------------------------

bool RulerTool::ImportBigWarpCSV(const QString& inputpath)
{
	QFile csv(inputpath);
	if (!csv.exists())
		return false;
	if (!csv.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	ClearRulers();
	Ruler::ResetID();

	// wx reads the lines with wxTextFile (GetFirstLine; while (!Eof()) ...
	// GetNextLine()); with wx 3.3 that loop processes every line, including a
	// last line without a newline (checked: 10 of 10 landmarks), as here.
	QTextStream ts(&csv);
	while (!ts.atEnd())
	{
		QString line = ts.readLine();
		// wxStringTokenizer(line, ","): empty tokens are kept except a
		// trailing one
		QStringList elems = line.split(QLatin1Char(','));
		if (!elems.isEmpty() && elems.last().isEmpty())
			elems.removeLast();
		for (QString& token : elems)
			token.remove(QLatin1Char('"'));
		if (elems.size() == 8)
		{
			Point p1, p2;
			p1.x(elems[2].toDouble());
			p1.y(elems[3].toDouble());
			p1.z(-elems[4].toDouble());
			p2.x(elems[5].toDouble());
			p2.y(elems[6].toDouble());
			p2.z(-elems[7].toDouble());

			Ruler* ruler = new Ruler();
			ruler->AddPoint(p1);
			ruler->SetTimeDep(m_ruler_time_dep);
			ruler->SetTime(m_tseq_cur_num);
			if (p1 != p2) // 2 points
			{
				ruler->SetRulerType(0);
				ruler->AddPoint(p2);
			}
			else
			{
				ruler->SetRulerType(2);
			}
			m_ruler_list.push_back(ruler);
		}
	}
	csv.close();

	Q_EMIT rulersChanged();
	Refresh();
	return true;
}

bool RulerTool::ExportBigWarpCSV(const QString& outpath)
{
	QStringList lines;
	int count = 0;
	for (size_t i = 0; i < m_ruler_list.size(); ++i)
	{
		if (m_ruler_list[i])
		{
			QString str;
			if (m_ruler_list[i]->GetRulerType() == 2) //locator (single point)
			{
				Point* p = m_ruler_list[i]->GetPoint(0);
				if (p)
					str = QString::asprintf("\"Pt-%d\",\"true\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\"", count, p->x(), p->y(), p->z() * -1.0, p->x(), p->y(), p->z() * -1.0);
			}
			else if (m_ruler_list[i]->GetRulerType() == 0) //2 points
			{
				Point* p1 = m_ruler_list[i]->GetPoint(0);
				Point* p2 = m_ruler_list[i]->GetPoint(1);
				if (p1 && p2)
					str = QString::asprintf("\"Pt-%d\",\"true\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\",\"%.14f\"", count, p1->x(), p1->y(), p1->z() * -1.0, p2->x(), p2->y(), p2->z() * -1.0);
			}

			if (!str.isEmpty())
			{
				lines << str;
				count++;
			}
		}
	}

	if (lines.isEmpty())
		return false;

	QFile csv(outpath);
	if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return false;
	QTextStream ts(&csv);
	for (const QString& l : lines)
		ts << l << '\n';
	ts.flush();
	csv.close();
	return true;
}

void RulerTool::ScatterRulers(long density)
{
	VolumeData* cur_vol = CurVol();
	if (!cur_vol)
		return;
	Texture* tex = cur_vol->GetTexture();
	if (!tex) return;
	Nrrd* nrrd = tex->get_nrrd_raw(0);
	if (!nrrd) return;
	void* data = nrrd->data;
	if (!data && !tex->isBrxml()) return;
	if (!cur_vol->GetVR()) return;

	//volume res
	int xx = -1;
	int yy = -1;
	int zz = -1;
	int resx, resy, resz;
	cur_vol->GetResolution(resx, resy, resz);
	std::vector<Plane*>* planes = 0;
	if (cur_vol->GetVR())
		planes = cur_vol->GetVR()->get_planes();
	Transform* textrans = tex->transform();

	UniformDensityPointGenerator generator(density, resx, resy, resz);
	std::vector<Point> points = generator.generatePoints();

	Point mp;

	cur_vol->GetVR()->return_mask();
	//find labels in the old that are selected by the current mask
	auto mask_nrrd = cur_vol->GetMask(true);
	unsigned char* mask_data = NULL;
	if (mask_nrrd && mask_nrrd->getNrrd())
	{
		mask_data = (unsigned char*)(mask_nrrd->getNrrd()->data);
		if (!mask_data)
			return;
		// If the mask exists but is entirely zero (empty), treat it as no mask
		bool mask_empty = true;
		long long mask_size = (long long)resx * resy * resz;
		for (long long i = 0; i < mask_size; i++)
		{
			if (mask_data[i] > 0)
			{
				mask_empty = false;
				break;
			}
		}
		if (mask_empty)
			mask_data = NULL;
	}

	for (Point& pt : points)
	{
		xx = int(pt.x());
		yy = int(pt.y());
		zz = int(pt.z());

		//out of bound, skip
		if (xx<0 || xx>resx ||
			yy<0 || yy>resy ||
			zz<0 || zz>resz)
			continue;

		bool inside = true;
		if (planes)
		{
			Point tx_p = pt;
			tx_p.x(pt.x() / resx);
			tx_p.y(pt.y() / resy);
			tx_p.z(pt.z() / resz);
			for (int i = 0; i < 6; i++)
			{
				if ((*planes)[i] &&
					(*planes)[i]->eval_point(tx_p) < 0.0)
				{
					inside = false;
					break;
				}
			}
		}
		if (mask_data)
		{
			xx = xx == resx ? resx - 1 : xx;
			yy = yy == resy ? resy - 1 : yy;
			zz = zz == resz ? resz - 1 : zz;

			long long idx = (long long)resx * resy * zz + (long long)resx * yy + xx;
			if (mask_data[idx] <= 0)
				inside = false;
		}

		if (inside)
		{
			xx = xx == resx ? resx - 1 : xx;
			yy = yy == resy ? resy - 1 : yy;
			zz = zz == resz ? resz - 1 : zz;

			mp = Point((xx + 0.5) / resx, (yy + 0.5) / resy, (zz + 0.5) / resz);
			mp = textrans->project(mp);

			Ruler* ruler = new Ruler();
			ruler->SetRulerType(2);
			ruler->AddPoint(mp);
			ruler->SetPointVolName(0, cur_vol->GetName());
			ruler->SetTimeDep(m_ruler_time_dep);
			ruler->SetTime(m_tseq_cur_num);
			m_ruler_list.push_back(ruler);
		}
	}

	Q_EMIT rulersChanged();
	Refresh();
}

VolumeData* RulerTool::WarpCurrentVolume(int ttype, double lambda, int interp, QString* error, VolumeData* target)
{
	//shared with the wx GUI (DataManager.cpp WarpVolumeByRulers)
	VVDString err;
	VolumeData* vd = WarpVolumeByRulers(CurVol(), m_ruler_list, ttype, lambda, interp, &err, target);
	if (!vd && error)
		*error = QString::fromStdWString(err);
	return vd;
}

VolumeData* RulerTool::WarpCurrentVolumeCells(const rigidcells::Params& params, rigidcells::Report* report, QString* error,
	VolumeData* target)
{
	//shared with the wx GUI (RigidCellsWarp.cpp)
	VVDString err;
	VolumeData* vd = RigidCellsWarpVolumeByRulers(CurVol(), m_ruler_list, params, report, &err, target);
	if (!vd && error)
		*error = QString::fromStdWString(err);
	return vd;
}
