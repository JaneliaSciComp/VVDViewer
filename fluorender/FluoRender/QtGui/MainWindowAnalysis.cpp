#include <QFileInfo>
// MainWindow's AnalysisContext implementation and the "Analyze" menu.
//
// In the wx build these live on VRenderVulkanView (m_selector, m_calculator,
// CompAnalysis, CalculateSingle, CompExport, ShowAnnotations). The Qt app has
// one render view, so the main window owns them.

#include "MainWindow.h"
#include <QMimeData>
#include <QDragEnterEvent>
#include "TestRun.h"

#include <QMenu>
#include <QAction>
#include <QDialog>

#include "RenderCanvas.h"
#include "TreePanel.h"
#include "NoiseReductionDialog.h"
#include "CountingDialog.h"
#include "ConvertDialog.h"
#include "ColocalizationDialog.h"
#include "InfoDialog.h"
#include "SettingsDialog.h"
#include "RulerTool.h"
#include "ViewOverlays.h"
#include "BrushTool.h"
#include "BrushToolDialog.h"
#include "MeasureDialog.h"
#include "DataManager.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include "FLIVR/VolumeRenderer.h"

#include <algorithm>
#include <QTimer>
#include <memory>
#include <QScrollBar>
#include <QLocale>
#include "VolumePropPanel.h"
#include <cmath>
#include <QKeyEvent>
#include <QLineEdit>
#include <QElapsedTimer>
#include <QUrl>
#include <cstdio>
#include <QPushButton>
#include "ViewPanel.h"
#include <QApplication>
#include <functional>
#include <QMouseEvent>

void MainWindow::BuildAnalysisMenu(QMenu* menu)
{
	m_analysis_menu = menu;
	auto pending = [this](QAction* a) {
		a->setEnabled(false);
		a->setToolTip(tr("Not ported to Qt yet"));
	};
	// Brush tool and measurement need painting / rulers in the render canvas.
	menu->addAction(tr("Analyze..."), this, [this]() {
		if (!m_brush_dlg) m_brush_dlg = new BrushToolDialog(this, m_brush_tool, this);
		m_brush_dlg->GetSettings();
		ShowTool(m_brush_dlg);
	});
	menu->addAction(tr("Measurement..."), this, [this]() {
		EnsureMeasureDialog()->GetSettings();
		ShowTool(m_measure_dlg);
	});
	// wx has this entry commented out (VRenderFrame.cpp m_tb_menu_edit);
	// the Qt app shows it so component tracking is reachable from the menus
	menu->addAction(tr("Components && Tracking..."), this, [this]() { ShowTraceDlg(); });

	menu->addAction(tr("Noise Reduction..."), this, [this]() {
		if (!m_nr_dlg) m_nr_dlg = new NoiseReductionDialog(this, this);
		ShowTool(m_nr_dlg);
	});
	menu->addAction(tr("Counting and Volume..."), this, [this]() {
		if (!m_count_dlg) m_count_dlg = new CountingDialog(this, this);
		ShowTool(m_count_dlg);
	});
	menu->addAction(tr("Colocalization Analysis..."), this, [this]() {
		if (!m_coloc_dlg) m_coloc_dlg = new ColocalizationDialog(this, this);
		m_coloc_dlg->GetSettings();
		ShowTool(m_coloc_dlg);
	});
	menu->addAction(tr("Convert..."), this, [this]() {
		if (!m_convert_dlg)
		{
			m_convert_dlg = new ConvertDialog(this, this);
			connect(m_convert_dlg, &ConvertDialog::meshCreated, this, [this](MeshData* md) {
				if (!md) return;
				m_canvas->AddMesh(md);
				RefreshTree();
				m_canvas->RefreshView();
			});
		}
		ShowTool(m_convert_dlg);
	});
	menu->addAction(tr("Infomation..."), this, [this]() {
		if (!m_info_dlg) m_info_dlg = new InfoDialog(this, this);
		m_info_dlg->SetData(CurrentVolume());
		ShowTool(m_info_dlg);
	});
}

void MainWindow::ShowTool(QDialog* dlg)
{
	if (!dlg) return;
	m_canvas->EnsureVulkan();   // selector / calculator passes run on the GPU
	MakeFloatingTool(dlg);   // wx: floating AUI panes stay above the frame
	dlg->show();
	dlg->raise();
	dlg->activateWindow();
}

// ---------------------------------------------------------------------------
// AnalysisContext
// ---------------------------------------------------------------------------

VolumeData* MainWindow::CurrentVolume()
{
	return m_canvas->GetCurrentVolume();
}

void MainWindow::RefreshView()
{
	m_canvas->RefreshView();
}

void MainWindow::AddVolume(VolumeData* vd)
{
	if (!vd) return;
	if (!m_data_mgr->GetVolumeData(vd->GetName()))
		m_data_mgr->AddVolumeData(vd);
	const auto& vols = m_canvas->Volumes();
	if (std::find(vols.begin(), vols.end(), vd) == vols.end())
		m_canvas->AddVolume(vd);
	RefreshTree();
	if (VolumeData* cur = CurrentVolume()) m_tree->SelectVolume(cur);
	m_canvas->RefreshView();
}

std::vector<VolumeData*> MainWindow::VisibleVolumes()
{
	return m_canvas->PopVolumeList();   // wx GetDispVolumeData: group visibility honoured
}

// ---------------------------------------------------------------------------
// picking in the view (wx VRenderVulkanView::Pick / PickVolume /
// SelSegVolume / PickMesh / SelLabelSegVolumeMax)
// ---------------------------------------------------------------------------

namespace
{
	bool AnyNAMode(const std::vector<VolumeData*>& vols)
	{
		for (VolumeData* vd : vols)
			if (vd && vd->GetDisp() && vd->GetNAMode())
				return true;
		return false;
	}
}

// wx LeftUp in int mode 1: if (m_pick && !na_mode) Pick()
void MainWindow::PickAt(const QPoint& pos)
{
	if (AnyNAMode(m_canvas->PopVolumeList()))
		return;
	// wx Pick(): only while the view draws its data (m_draw_all)
	if (!m_canvas->GetDraw())
		return;
	if (!SelSegVolume(3, pos))
		PickVolume(pos);
	PickMeshAt(pos);   // wx PickMesh (MainWindowUI.cpp)
	m_canvas->RefreshView();
}

bool MainWindow::PickVolume(const QPoint& pos)
{
	double dist = 0.0;
	double min_dist = -1.0;
	FLIVR::Point p;
	VolumeData* picked_vd = nullptr;
	const bool persp = m_canvas->GetPerspective();
	for (VolumeData* vd : m_canvas->PopVolumeList())
	{
		if (!vd || !vd->GetDisp()) continue;
		double sel_id;
		if (vd->GetColormapMode() == 3)
			dist = m_ruler_tool->GetPointAndIntVolume(p, sel_id, false, pos.x(), pos.y(), vd, 0.3);
		else
			dist = m_ruler_tool->GetPointVolume(p, pos.x(), pos.y(), vd, 2, true, 0.3);
		if (dist > 0.0)
		{
			if (min_dist < 0.0 || (persp ? dist < min_dist : dist > min_dist))
			{
				min_dist = dist;
				picked_vd = vd;
			}
		}
	}
	if (picked_vd)
	{
		// wx: GetTree()->SetFocus(); Select(view, picked_vd->GetName())
		m_tree->SelectVolume(picked_vd);
		return true;
	}
	return false;
}

// mode: 0 toggle, 1 toggle exclusively, 2 deselect, 3 edit (an already
// selected segment becomes the edited one)
bool MainWindow::SelSegVolume(int mode, const QPoint& pos)
{
	double dist = 0.0;
	double min_dist = -1.0;
	FLIVR::Point p;
	VolumeData* picked_vd = nullptr;
	int picked_sel_id = 0;
	const bool persp = m_canvas->GetPerspective();
	for (VolumeData* vd : m_canvas->PopVolumeList())
	{
		if (!vd || !vd->GetDisp() || vd->GetColormapMode() != 3) continue;
		double sel_id = 0.0;
		dist = m_ruler_tool->GetPointAndIntVolume(p, sel_id, false, pos.x(), pos.y(), vd);
		if (dist > 0.0)
		{
			if (min_dist < 0.0 || (persp ? dist < min_dist : dist > min_dist))
			{
				min_dist = dist;
				picked_vd = vd;
				picked_sel_id = int(sel_id);
			}
		}
	}
	if (!picked_vd || picked_sel_id <= 0)
		return false;

	int combined_seg_id = picked_vd->IsROICombined(picked_sel_id);
	if (combined_seg_id > 0)
		picked_sel_id = combined_seg_id;

	switch (mode)
	{
	case 0:
		if (picked_vd->isSelID(picked_sel_id))
			picked_vd->DelSelID(picked_sel_id);
		else
			picked_vd->AddSelID(picked_sel_id);
		break;
	case 1:
		if (picked_vd->isSelID(picked_sel_id))
			picked_vd->DelSelID(picked_sel_id);
		else
		{
			picked_vd->ClearSelIDs();
			picked_vd->AddSelID(picked_sel_id);
		}
		break;
	case 2:
		picked_vd->DelSelID(picked_sel_id);
		break;
	case 3:
		if (picked_vd->isSelID(picked_sel_id))
			picked_vd->SetEditSelID(picked_sel_id);
		break;
	}

	if (m_prop->GetVolume() == picked_vd)
		m_prop->UpdateUIsROI();
	if (mode != 3) m_tree->UpdateTreeIcons();
	m_tree->SelectROI(picked_vd, picked_sel_id);
	return true;
}

// wx VRenderVulkanView::SelLabelSegVolumeMax: NA-mode volumes. A double
// click picks the label (GetPointAndLabelMax: the voxel of the shown
// segments where the NA-mode volumes are brightest along the ray) and, in
// every displayed NA-mode volume, mode 0 toggles that segment between
// highlighted (2) and shown (1), mode 1 makes it the only highlighted one.
// wx mode 1 never returns: its loop over the active ids does not advance
// the iterator (VRenderView.cpp, "while (it != ids->end())"); it is fixed
// here (each active id set to 1 once). Only mode 0 is used (LeftDClick).
bool MainWindow::SelLabelSegVolumeMax(int mode, const std::vector<VolumeData*>& ref, const QPoint& pos)
{
	double dist = 0.0;
	double min_dist = -1.0;
	FLIVR::Point p;
	VolumeData* picked_vd = nullptr;
	std::vector<VolumeData*> navds;
	int picked_sel_id = 0;
	bool rval = false;
	const bool persp = m_canvas->GetPerspective();
	for (VolumeData* vd : m_canvas->PopVolumeList())
	{
		if (!vd || !vd->GetDisp() || !vd->GetNAMode()) continue;
		navds.push_back(vd);
		if (!vd->GetLabel(false)) continue;

		if (picked_vd)
			continue;

		int sel_id = 0;
		dist = m_ruler_tool->GetPointAndLabelMax(p, sel_id, pos.x(), pos.y(), vd, ref);

		if (dist > 0.0)
		{
			if (min_dist < 0.0 || (persp ? dist < min_dist : dist > min_dist))
			{
				min_dist = dist;
				picked_vd = vd;
				picked_sel_id = sel_id;
			}
		}
	}

	if (picked_vd && picked_sel_id > 0)
	{
		for (VolumeData* nvd : navds)
		{
			switch (mode)
			{
			case 0:
				if (nvd->GetSegmentMask(picked_sel_id) == 2)
					nvd->SetSegmentMask(picked_sel_id, 1);
				else if (nvd->GetSegmentMask(picked_sel_id) == 1)
					nvd->SetSegmentMask(picked_sel_id, 2);
				break;
			case 1:
				if (nvd->GetSegmentMask(picked_sel_id) == 2)
					nvd->SetSegmentMask(picked_sel_id, false);
				else if (nvd->GetSegmentMask(picked_sel_id) == 1)
				{
					if (std::set<int>* ids = nvd->GetActiveSegIDs())
					{
						// copy: SetSegmentMask may edit the active set
						const std::vector<int> act(ids->begin(), ids->end());
						for (int id : act)
							nvd->SetSegmentMask(id, 1);
					}
					nvd->SetSegmentMask(picked_sel_id, 2);
				}
				break;
			}
			if (mode != 3) m_tree->UpdateTreeIcons();
			rval = true;
		}
	}
	return rval;
}

// wx VPropView::SaveROIName
void MainWindow::SaveROIName(const QString& name)
{
	VolumeData* vd = m_prop->GetVolume();
	if (!vd) return;
	std::wstring parent;
	int parent_id = -1;
	if (vd->GetROIName(vd->GetEditSelID()).empty())
	{
		// a new name under the selected segment group item
		QTreeWidgetItem* sel_item = m_tree->Tree()->currentItem();
		if (sel_item && TreePanel::ItemType(sel_item) == TreePanel::TypeSegGroup)
		{
			QTreeWidgetItem* vitem = m_tree->ParentVolItem(sel_item);
			if (vitem && TreePanel::ItemName(vitem) == vd->GetName().ToQt())
			{
				parent = TreePanel::ItemName(sel_item).toStdWString();
				parent_id = vd->GetROIid(parent);
			}
		}
	}
	vd->SetROIName(name.toStdWString(), -1, parent);
	RefreshTree();
	m_tree->SelectROI(vd, parent_id);
	m_canvas->RefreshView();
}

// ---------------------------------------------------------------------------
// brush settings in projects
// ---------------------------------------------------------------------------

namespace
{
	const char* kBrushView = "/views/%1/properties/";
	const char* kBrushDiag = "/brush_diag/";
}

void MainWindow::ApplyProjectBrushSettings(int view)
{
	ViewSlot* slot = (view >= 0 && view < (int)m_views.size()) ? m_views[view].get() : nullptr;
	if (!slot) return;
	const std::vector<std::pair<QString, QString>>& keys = view == 0 ? m_prj_passthrough : slot->prj_props;
	auto find = [&keys](const QString& key, double& v) {
		for (const auto& kv : keys)
			if (kv.first == key)
			{
				bool ok = false;
				const double d = QLocale::c().toDouble(kv.second.trimmed(), &ok);
				if (ok) { v = d; return true; }
			}
		return false;
	};
	BrushTool* brush = slot->brush;
	const QString pv = QString(kBrushView).arg(view), pd = kBrushDiag;
	double v = 0.0, size1 = 0.0, size2 = 0.0;
	if (find(pv + "brush_use_pres", v)) brush->SetBrushUsePres(v != 0.0);
	if (find(pv + "brush_size_1", size1) && find(pv + "brush_size_2", size2))
		brush->SetBrushSize(size1, size2);
	if (find(pv + "brush_spacing", v)) brush->SetBrushSpacing(v);
	if (find(pv + "brush_iteration", v)) brush->SetBrushIteration((int)v);
	if (find(pv + "brush_translate", v)) brush->SetBrushSclTranslate(v);
	if (find(pv + "w2d", v)) brush->SetW2d(v);
	if (view != 0) return;
	if (m_brush_dlg)
	{
		if (find(pd + "ca_min", v)) m_brush_dlg->SetDftCAMin(v);
		if (find(pd + "ca_max", v)) m_brush_dlg->SetDftCAMax(v);
		if (find(pd + "ca_thresh", v)) m_brush_dlg->SetDftCAThresh(v);
		if (find(pd + "nr_thresh", v)) m_brush_dlg->SetDftNRThresh(v);
		if (find(pd + "nr_size", v)) m_brush_dlg->SetDftNRSize(v);
		m_brush_dlg->GetSettings();
	}
	if (m_nr_dlg) m_nr_dlg->GetSettings();
}

void MainWindow::StoreProjectBrushSettings(int view)
{
	ViewSlot* slot = (view >= 0 && view < (int)m_views.size()) ? m_views[view].get() : nullptr;
	if (!slot) return;
	std::vector<std::pair<QString, QString>>& keys = view == 0 ? m_prj_passthrough : slot->prj_props;
	auto put = [&keys](const QString& key, double v) {
		const QString val = QString::number(v, 'g', 15);
		for (auto& kv : keys)
			if (kv.first == key) { kv.second = val; return; }
		keys.emplace_back(key, val);
	};
	BrushTool* brush = slot->brush;
	const QString pv = QString(kBrushView).arg(view), pd = kBrushDiag;
	put(pv + "brush_use_pres", brush->GetBrushUsePres() ? 1 : 0);
	put(pv + "brush_size_1", brush->GetBrushSize1());
	put(pv + "brush_size_2", brush->GetBrushSize2());
	put(pv + "brush_spacing", brush->GetBrushSpacing());
	put(pv + "brush_iteration", brush->GetBrushIteration());
	put(pv + "brush_translate", brush->GetBrushSclTranslate());
	put(pv + "w2d", brush->GetW2d());
	if (view != 0) return;
	if (m_brush_dlg)
	{
		put(pd + "ca_min", m_brush_dlg->GetDftCAMin());
		put(pd + "ca_max", m_brush_dlg->GetDftCAMax());
		put(pd + "ca_thresh", m_brush_dlg->GetDftCAThresh());
		put(pd + "nr_thresh", m_brush_dlg->GetDftNRThresh());
		put(pd + "nr_size", m_brush_dlg->GetDftNRSize());
	}
}

// VRenderVulkanView::SetEnhanceSelection (the flag lives on the view)
void MainWindow::SetEnhanceSelection(bool enhance)
{
	m_canvas->SetEnhanceSelection(enhance);
}

bool MainWindow::GetEnhanceSelection()
{
	return m_canvas->GetEnhanceSelection();
}

// VRenderVulkanView::CompAnalysis
int MainWindow::CompAnalysis(double min_voxels, double max_voxels, double thresh,
	bool select, bool gen_ann, int iter_limit)
{
	m_canvas->EnsureVulkan();
	if (!select)
	{
		m_sel->Set2DMask(m_brush_tool->PaintTex());
		m_sel->Set2DWeight(m_canvas->GetFinalTex(), m_canvas->GetVolTex());
	}
	m_sel->SetSizeMap(false);
	int ret = m_sel->CompAnalysis(min_voxels, max_voxels, thresh, 1.0, select, gen_ann, iter_limit);
	if (Annotations* ann = m_sel->GetAnnotations())
		m_data_mgr->AddAnnotations(ann);
	return ret;
}

// VRenderVulkanView::NoiseAnalysis
int MainWindow::NoiseAnalysis(double min_voxels, double max_voxels, double thresh)
{
	m_canvas->EnsureVulkan();
	VolumeData* vd = m_sel->GetVolume();
	bool copied = false;
	if (vd && vd->isBrxml())
	{
		// wx CopyLevel(vd): the copy is not added to the data manager and
		// stays the selector's volume (NoiseRemoval then works on it)
		vd = vd->CopyLevel();
		m_sel->SetVolume(vd);
		if (vd) copied = true;
	}
	m_sel->Set2DMask(m_brush_tool->PaintTex());
	m_sel->Set2DWeight(m_canvas->GetFinalTex(), m_canvas->GetVolTex());
	int ret = m_sel->CompAnalysis(min_voxels, max_voxels, thresh, 1.0, false, false);
	if (copied) m_canvas->RefreshView();
	return ret;
}

// wx TreePanel::BrushErase (the tree's / Noise Reduction's Erase)
void MainWindow::BrushErase()
{
	if (m_brush_tool)
		m_brush_tool->BrushErase();
}

// VRenderVulkanView::NoiseRemoval
void MainWindow::NoiseRemoval(int iter, double thresh)
{
	VolumeData* vd = m_sel->GetVolume();
	if (!vd || !vd->GetMask(false)) return;
	m_canvas->EnsureVulkan();

	const std::wstring name = vd->GetName();
	if (name.find(L"_NR") == std::wstring::npos)
	{
		m_sel->NoiseRemoval(iter, thresh, 1);
		std::vector<VolumeData*>* vol_list = m_sel->GetResultVols();
		if (!vol_list || vol_list->empty()) return;
		VolumeData* vd_new = (*vol_list)[0];
		if (vd_new)
		{
			m_data_mgr->AddVolumeData(vd_new);
			const VVDString group_name = m_canvas->AddGroup(VVDString());
			if (DataGroup* group = m_canvas->GetGroup(group_name))
			{
				group->SetGamma(vd->GetGamma());
				group->SetBrightness(vd->GetBrightness());
				group->SetHdr(vd->GetHdr());
				group->SetLevels(vd->GetLevels());
				group->SetSyncR(vd->GetSyncR());
				group->SetSyncG(vd->GetSyncG());
				group->SetSyncB(vd->GetSyncB());
			}
			m_canvas->AddVolumeData(vd_new, group_name);
			vd->SetDisp(false);
			// wx UpdateTree(vd_new->GetName(), 2)
			m_canvas->SetCurrentVolume(vd_new);
			RefreshTree();
			m_tree->SelectVolume(vd_new);
		}
	}
	else
		m_sel->NoiseRemoval(iter, thresh);
	m_canvas->RefreshView();
}

// VRenderVulkanView::Calculate: types 5/6/7 with the brush tool's "Select
// group" apply CalculateSingle to every displayed volume of A's group
void MainWindow::Calculate(int type, bool add)
{
	VolumeData* vd = m_calc->GetVolumeA();
	// the result goes into A's group (wx callers pass the group of the
	// selected volume as prev_group; AddVolumeData("") picks the current
	// volume's group, normally the same)
	DataGroup* agroup = vd ? m_canvas->GetParentGroup(vd) : nullptr;
	const VVDString prev_group = agroup ? agroup->GetName() : VVDString();
	if ((type == 5 || type == 6 || type == 7) && m_sel->GetSelectGroup() && agroup && agroup->GetVolumeNum() > 1)
	{
		std::vector<VolumeData*> vd_list;
		for (int i = 0; i < agroup->GetVolumeNum(); i++)
		{
			VolumeData* tmp_vd = agroup->GetVolumeData(i);
			if (tmp_vd && tmp_vd->GetDisp())
				vd_list.push_back(tmp_vd);
		}
		for (VolumeData* v : vd_list)
		{
			m_calc->SetVolumeA(v);
			CalculateSingle(type, prev_group, add);
		}
		m_calc->SetVolumeA(vd);
	}
	else
		CalculateSingle(type, prev_group, add);
}

// VRenderVulkanView::CalculateSingle
void MainWindow::CalculateSingle(int type, const VVDString& prev_group, bool add)
{
	m_canvas->EnsureVulkan();
	// wx: pyramid (.brxml) operands are calculated on a copy of their mask
	// level (VRenderVulkanView::CopyLevel = VolumeData::CopyLevel; the copy
	// is not added anywhere and is deleted afterwards)
	VolumeData* prev[3] = { m_calc->GetVolumeA(), m_calc->GetVolumeB(), m_calc->GetVolumeC() };
	VolumeData* cur[3] = { prev[0], prev[1], prev[2] };
	bool copied[3] = { false, false, false };
	for (int i = 0; i < 3; i++)
		if (cur[i] && cur[i]->isBrxml())
		{
			if (VolumeData* c = cur[i]->CopyLevel(cur[i]->GetMaskLv()))
			{
				cur[i] = c;
				copied[i] = true;
			}
		}
	m_calc->SetVolumeA(cur[0]);
	m_calc->SetVolumeB(cur[1]);
	m_calc->SetVolumeC(cur[2]);
	auto restore = [&]() {
		// wx restores A only (B/C would keep the deleted copies)
		m_calc->SetVolumeA(prev[0]);
		m_calc->SetVolumeB(prev[1]);
		m_calc->SetVolumeC(prev[2]);
		for (int i = 0; i < 3; i++)
			if (copied[i])
			{
				prev[i]->SetDisp(cur[i]->GetDisp());
				delete cur[i];
			}
		if (copied[0] || copied[1] || copied[2])
			m_canvas->RefreshView();
	};

	VolumeData* vd_a = cur[0];
	m_calc->Calculate(type);
	VolumeData* vd = m_calc->GetResult();
	static const int kTypes[] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 11 };
	if (!vd || std::find(std::begin(kTypes), std::end(kTypes), type) == std::end(kTypes))
	{
		restore();
		return;
	}

	if (vd_a)
	{
		// copy render settings of A to the result
		std::vector<FLIVR::Plane*>* planes = vd_a->GetVR() ? vd_a->GetVR()->get_planes() : nullptr;
		if (planes && vd->GetVR())
			vd->GetVR()->set_planes(planes);
		vd->Set3DGamma(vd_a->Get3DGamma());
		vd->SetBoundary(vd_a->GetBoundary());
		vd->SetOffset(vd_a->GetOffset());
		vd->SetLeftThresh(vd_a->GetLeftThresh());
		vd->SetRightThresh(vd_a->GetRightThresh());
		FLIVR::Color col = vd_a->GetColor();
		vd->SetColor(col);
		vd->SetAlpha(vd_a->GetAlpha());
		vd->SetShading(vd_a->GetShading());
		double amb, diff, spec, shine;
		vd_a->GetMaterial(amb, diff, spec, shine);
		vd->SetMaterial(amb, diff, spec, shine);
		vd->SetShadow(vd_a->GetShadow());
		double shadow;
		vd_a->GetShadowParams(shadow);
		vd->SetShadowParams(shadow);
		vd->SetSampleRate(vd_a->GetSampleRate());
		col = vd_a->GetGamma();
		vd->SetGamma(col);
		col = vd_a->GetBrightness();
		vd->SetBrightness(col);
		col = vd_a->GetHdr();
		vd->SetHdr(col);
		col = vd_a->GetLevels();
		vd->SetLevels(col);
		vd->SetSyncR(vd_a->GetSyncR());
		vd->SetSyncG(vd_a->GetSyncG());
		vd->SetSyncB(vd_a->GetSyncB());
		vd->SetMaxValue(vd_a->GetMaxValue());
		vd->SetScalarScale(vd_a->GetScalarScale());
	}

	if (!add)
	{
		// wx: the result stays with the calculator (the caller owns it)
		restore();
		m_canvas->RefreshView();
		return;
	}

	bool btmp = m_data_mgr->GetOverrideVox();
	m_data_mgr->SetOverrideVox(false);
	m_data_mgr->AddVolumeData(vd);
	m_canvas->AddVolumeData(vd, prev_group);
	m_data_mgr->SetOverrideVox(btmp);

	VolumeData* vd_b = cur[1];
	VolumeData* vd_c = cur[2];
	if (type == 5 || type == 6 || type == 9)
	{
		if (vd_a) vd_a->SetDisp(false);
	}
	else if (type >= 1 && type <= 4)
	{
		if (vd_a) vd_a->SetDisp(false);
		if (vd_b) vd_b->SetDisp(false);
	}
	else if (type == 10 || type == 11)
	{
		if (vd_a) vd_a->SetDisp(false);
		if (vd_b) vd_b->SetDisp(false);
		if (vd_c) vd_c->SetDisp(false);
	}

	// wx: UpdateTree(vd->GetName(), 2, false) selects the result, then
	// SetVolumeA(vd_prevA): the calculator keeps the original operand
	m_canvas->SetCurrentVolume(vd);
	RefreshTree();
	m_tree->SelectVolume(vd);
	restore();
	m_canvas->RefreshView();
}

// VRenderVulkanView::CompExport
void MainWindow::CompExport(int mode, bool select)
{
	VolumeData* src = m_sel->GetVolume();
	if (!src) return;
	m_canvas->EnsureVulkan();
	switch (mode)
	{
	case 0:
		m_sel->CompExportMultiChann(select);
		break;
	case 1:
	{
		VVDString sel_name = src->GetName();
		VolumeData* vd_r = nullptr;
		VolumeData* vd_g = nullptr;
		VolumeData* vd_b = nullptr;
		for (int i = 0; i < m_data_mgr->GetVolumeNum(); ++i)
		{
			VolumeData* vd = m_data_mgr->GetVolumeData(i);
			VVDString name = vd->GetName();
			if (name == sel_name + L"_COMP1") vd_r = vd;
			else if (name == sel_name + L"_COMP2") vd_g = vd;
			else if (name == sel_name + L"_COMP3") vd_b = vd;
		}
		m_sel->CompExportRandomColor(1, vd_r, vd_g, vd_b, select);
		break;
	}
	}
	// wx: every result volume into ONE new group, which then syncs the
	// channels and takes the source's 2D adjustments
	if (std::vector<VolumeData*>* vols = m_sel->GetResultVols())
	{
		VVDString group_name;
		DataGroup* group = nullptr;
		for (size_t i = 0; i < vols->size(); i++)
		{
			VolumeData* vd = (*vols)[i];
			if (!vd) continue;
			m_data_mgr->AddVolumeData(vd);
			if (!group)
			{
				group_name = m_canvas->AddGroup(VVDString());
				group = m_canvas->GetGroup(group_name);
			}
			m_canvas->AddVolumeData(vd, group_name);
		}
		if (group)
		{
			group->SetSyncRAll(true);
			group->SetSyncGAll(true);
			group->SetSyncBAll(true);
			FLIVR::Color col = src->GetGamma();
			group->SetGammaAll(col);
			col = src->GetBrightness();
			group->SetBrightnessAll(col);
			col = src->GetHdr();
			group->SetHdrAll(col);
			col = src->GetLevels();
			group->SetLevelsAll(col);
		}
	}
	RefreshTree();
	m_tree->SelectVolume(src);
	m_canvas->RefreshView();
}

// VRenderVulkanView::ShowAnnotations: annotations are kept in the data
// manager (CompAnalysis adds them) and become a layer of the view; the Qt
// canvas does not draw them yet.
void MainWindow::ShowAnnotations()
{
	if (Annotations* ann = m_sel->GetAnnotations())
	{
		if (!m_data_mgr->GetAnnotations(ann->GetName()))
			m_data_mgr->AddAnnotations(ann);
		// wx AddAnnotations: a layer of the view (tree item; not drawn yet)
		m_canvas->AddAnnotations(ann);
		VolumeData* cur = CurrentVolume();
		m_tree->UpdateTree(cur ? cur->GetName().ToQt() : QString(), cur ? TreePanel::TypeVolume : -1);
	}
	m_canvas->RefreshView();
}

// ---------------------------------------------------------------------------
// testing aid
// ---------------------------------------------------------------------------

// Steps (comma separated): openvolume, openmesh, openproject, saveproject
// (MainWindow dialogs), savevolume, savesegmented, bake, exportmeta (tree
// actions on the selected item), select:<i> (select the i-th volume of the
// view), clip:<x0>:<x1>:<y0>:<y1>:<z0>:<z1> (clipping planes of the selected
// volume as fractions of its box, like the Clipping Planes sliders),
// wait:<ms>. The dialogs pick their files and options from
// VVD_QT_FILEDLG_FILE_<NAME> / VVD_QT_FILEDLG_OPTS_<NAME> (FileDialogs.h).
void MainWindow::RunFileDialogTest(const QString& steps)
{
	for (const QString& st : steps.split(',', Qt::SkipEmptyParts))
	{
		const QStringList p = st.trimmed().split(':');
		const QString cmd = p[0];
		fprintf(stderr, "[filedlg] step %s\n", st.toLocal8Bit().constData());
		if (cmd == "openvolume") OnOpenVolume();
		else if (cmd == "openmesh") OnOpenMesh();
		else if (cmd == "openproject") OnOpenProject();
		else if (cmd == "saveproject") OnSaveProject();
		else if (cmd == "select" && p.size() > 1)
		{
			const std::vector<VolumeData*>& vols = m_canvas->Volumes();
			const int i = p[1].toInt();
			if (i >= 0 && i < (int)vols.size()) m_tree->SelectVolume(vols[i]);
		}
		else if (cmd == "clip" && p.size() == 7)
		{
			VolumeData* vd = m_tree->CurrentVolume();
			std::vector<FLIVR::Plane*>* planes = vd && vd->GetVR() ? vd->GetVR()->get_planes() : nullptr;
			if (planes && planes->size() >= 6)
				for (int a = 0; a < 3; a++)
				{
					// ClipPanel::EditPlane: low plane lo/res, high plane 1 - hi/res
					(*planes)[2 * a]->SetParam(p[1 + 2 * a].toDouble());
					(*planes)[2 * a + 1]->SetParam(1.0 - p[2 + 2 * a].toDouble());
				}
			m_canvas->RefreshView();
		}
		else if (cmd == "wait" && p.size() > 1)
		{
			QElapsedTimer t;
			t.start();
			while (t.elapsed() < p[1].toInt())
				QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		}
		else if (!m_tree->RunFileAction(cmd))
			fprintf(stderr, "[filedlg] unknown step %s\n", cmd.toLocal8Bit().constData());
		QCoreApplication::processEvents();
	}
	fprintf(stderr, "[filedlg] data manager: slice_seq %d time_seq %d time_id '%ls'; settings: realtime compress %d skip bricks %d save compress %d; embed %d\n",
		(int)m_data_mgr->GetSliceSequence(), (int)m_data_mgr->GetTimeSequence(), m_data_mgr->GetTimeId().c_str(),
		(int)m_settings->GetRealtimeCompress(), (int)m_settings->GetSkipBricks(), (int)m_settings->GetSaveCompress(), (int)m_vrp_embed);
	for (VolumeData* vd : m_canvas->Volumes())
	{
		int nx = 0, ny = 0, nz = 0;
		vd->GetResolution(nx, ny, nz);
		const int tn = vd->GetReader() ? vd->GetReader()->GetTimeNum() : 0;
		fprintf(stderr, "[filedlg] volume %ls: %dx%dx%d, %d time points, compressed %d, skip brick %d\n",
			vd->GetName().c_str(), nx, ny, nz, tn, (int)vd->GetCompression(), (int)vd->GetSkipBrick());
	}
	for (MeshData* md : m_canvas->Meshes())
		fprintf(stderr, "[filedlg] mesh %ls\n", md->GetName().c_str());
}

void MainWindow::RunTestScript(const QString& name)
{
	RenderCanvas* cv = m_canvas;
	const int w = cv->width(), h = cv->height();
	auto at = [this](int ms, std::function<void()> f) { QTimer::singleShot(ms, this, f); };
	auto click = [cv](QPoint p) {
		cv->SimulateMouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
		cv->SimulateMouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
	};
	if (name == "layout")
	{
		RunLayoutTest(name);   // MainWindowLayout.cpp; quits when done
		return;
	}
	if (name.startsWith("b4b_"))
	{
		RunPluginTest(name);   // MainWindowPluginTest.cpp; quits when done
		return;
	}
	if (name.startsWith("b4_") || name == "legend2")
	{
		RunBatch4Test(name);   // MainWindowBatch4Test.cpp; quits when done
		return;
	}
	if (name.startsWith("b3_"))
	{
		RunBatch3Test(name);   // MainWindowBatch3Test.cpp; quits when done
		return;
	}
	if (VVDQtPaintTestKind(name))
	{
		RunPaintTest(name);   // MainWindowPaintTest.cpp; quits when done
		return;
	}
	if (name == "ruler")
	{
		m_ruler_tool->SetRulerType(0);
		m_ruler_tool->SetIntMode(5);
		at(300, [=]() { click(QPoint(w * 35 / 100, h / 2)); });
		at(800, [=]() { click(QPoint(w * 65 / 100, h * 55 / 100)); });
	}
	else if (name == "dialogs")
	{
		// open every tool window and save a picture of each
		const QString dir = qEnvironmentVariable("VVD_QT_GRAB_DIR");
		int t = 200;
		QList<QAction*> acts = m_analysis_menu ? m_analysis_menu->actions() : QList<QAction*>();
		const QString only = qEnvironmentVariable("VVD_QT_TEST_ONLY");
		for (QAction* a : acts)
		{
			if (!only.isEmpty() && !a->text().contains(only)) continue;
			at(t, [a]() { a->trigger(); });
			at(t + 700, [this, a, dir]() {
				for (QWidget* tw : QApplication::topLevelWidgets())
					if (tw != this && tw->isVisible() && tw->isWindow() && !qobject_cast<QMenu*>(tw) && !tw->inherits("QDockWidget"))
					{
						QString n = a->text().remove("...").remove(' ');
						tw->grab().save(dir + "/dlg_" + n + ".png");
						tw->hide();
					}
			});
			t += 1000;
		}
		if (only.isEmpty() || only == "Settings")
		{
			at(t, [this, dir]() {
				m_settings->UpdateUI(); MakeFloatingTool(m_settings); m_settings->show();
			});
			at(t + 700, [this, dir]() { m_settings->grab().save(dir + "/dlg_Settings.png"); m_settings->hide(); });
		}
	}
	else if (name == "tree")
	{
		// Workspace tree: context menus, groups made / changed by drag & drop
		// through the same event path as the mouse (TreePanel::SimulateDrag),
		// toggle / rename / isolate. Pictures in VVD_QT_GRAB_DIR, the layer
		// tree after each step on stderr.
		const QString dir = qEnvironmentVariable("VVD_QT_GRAB_DIR");
		auto dump = [this](const char* step) {
			fprintf(stderr, "[tree] %s:\n", step);
			for (int i = 0; i < m_canvas->GetLayerNum(); i++)
			{
				TreeLayer* l = m_canvas->GetLayer(i);
				fprintf(stderr, "  %d %ls", l->IsA(), l->GetName().c_str());
				if (l->IsA() == 5)
				{
					DataGroup* g = (DataGroup*)l;
					fprintf(stderr, " disp=%d [", (int)g->GetDisp());
					for (int k = 0; k < g->GetVolumeNum(); k++)
						fprintf(stderr, "%s%ls%s", k ? ", " : "", g->GetVolumeData(k)->GetName().c_str(), g->GetVolumeData(k)->GetDisp() ? "" : "(hidden)");
					fprintf(stderr, "]");
				}
				else if (l->IsA() == 6)
				{
					MeshGroup* g = (MeshGroup*)l;
					fprintf(stderr, " [");
					for (int k = 0; k < g->GetMeshNum(); k++)
						fprintf(stderr, "%s%ls", k ? ", " : "", g->GetMeshData(k)->GetName().c_str());
					fprintf(stderr, "]");
				}
				fprintf(stderr, "\n");
			}
			fprintf(stderr, "  displayed volumes: %d of %d\n", (int)m_canvas->PopVolumeList().size(), (int)m_canvas->Volumes().size());
		};
		auto item = [this](const QString& n, int type = -1) { return m_tree->FindItem(n, type); };
		auto vol = [this](int i) { return i < (int)m_canvas->Volumes().size() ? m_canvas->Volumes()[i]->GetName().ToQt() : QString(); };
		auto grab_menu = [this, dir, item](const QString& n, int type, const QString& file) {
			QTreeWidgetItem* it = item(n, type);
			if (!it) { fprintf(stderr, "[tree] no item %s\n", n.toLocal8Bit().constData()); return; }
			m_tree->Tree()->setCurrentItem(it);
			QMenu* menu = m_tree->BuildContextMenu(it);
			if (!menu) return;
			menu->ensurePolished();
			menu->adjustSize();
			QStringList entries;
			for (QAction* a : menu->actions()) entries << (a->isSeparator() ? QString("----") : a->text());
			fprintf(stderr, "[tree] menu %s: %s\n", file.toLocal8Bit().constData(), entries.join(" | ").toLocal8Bit().constData());
			if (!dir.isEmpty()) menu->grab().save(dir + "/menu_" + file + ".png");
			delete menu;
		};
		auto trigger = [this, item](const QString& n, int type, const QString& entry) {
			QTreeWidgetItem* it = item(n, type);
			if (!it) return;
			m_tree->Tree()->setCurrentItem(it);
			QMenu* menu = m_tree->BuildContextMenu(it);
			if (!menu) return;
			for (QAction* a : menu->actions()) if (a->text() == entry) { a->trigger(); break; }
			delete menu;
		};
		at(100, [=]() {
			dump("loaded");
			if (!dir.isEmpty()) m_tree->grab().save(dir + "/tree_0_loaded.png");
			grab_menu(tr("Active Datasets"), TreePanel::TypeRoot, "root");
			grab_menu(tr("Render View:1"), TreePanel::TypeView, "view");
			grab_menu("Group 1", TreePanel::TypeGroup, "group");
			grab_menu(vol(0), TreePanel::TypeVolume, "volume");
			if (!m_canvas->Meshes().empty())
				grab_menu(m_canvas->Meshes()[0]->GetName().ToQt(), TreePanel::TypeMesh, "mesh");
			// "Add Volume Group" from the view's menu, "Add Mesh Group" from the group's
			trigger(tr("Render View:1"), TreePanel::TypeView, tr("Add Volume Group"));
			trigger(tr("Render View:1"), TreePanel::TypeView, tr("Add Mesh Group"));
			dump("Add Volume Group + Add Mesh Group");
			grab_menu("MGroup 1", TreePanel::TypeMGroup, "mgroup");
		});
		at(600, [=]() {
			// volume 0 dropped onto Group 2 (wx: MoveLayerfromtoGroup)
			bool ok = m_tree->SimulateDrag(item(vol(0), TreePanel::TypeVolume), item("Group 2", TreePanel::TypeGroup), true);
			fprintf(stderr, "[tree] drop volume on Group 2: %d\n", (int)ok);
			// the mesh dropped onto the mesh group (wx: MoveMeshtoGroup)
			if (!m_canvas->Meshes().empty() && item("MGroup 1", TreePanel::TypeMGroup))
				fprintf(stderr, "[tree] drop mesh on MGroup 1: %d\n", (int)m_tree->SimulateDrag(
					item(m_canvas->Meshes()[0]->GetName().ToQt(), TreePanel::TypeMesh), item("MGroup 1", TreePanel::TypeMGroup), true));
		});
		at(1100, [=]() {
			dump("volume onto Group 2");
			// the next volume dropped on the empty space: out of its group,
			// OrganizeLayers puts it into a new group (wx)
			const QString v = m_canvas->GetGroup(VVDString(L"Group 1"))->GetVolumeData(0)->GetName().ToQt();
			bool ok = m_tree->SimulateDrag(item(v, TreePanel::TypeVolume), nullptr, true);
			fprintf(stderr, "[tree] drop %s on empty space: %d\n", v.toLocal8Bit().constData(), (int)ok);
		});
		at(1600, [=]() {
			dump("volume onto empty space");
			// a volume of Group 1 dropped onto the volume in Group 2 (after it)
			DataGroup* g1 = m_canvas->GetGroup(VVDString(L"Group 1"));
			DataGroup* g2 = m_canvas->GetGroup(VVDString(L"Group 2"));
			if (g1 && g2 && g1->GetVolumeNum() > 0 && g2->GetVolumeNum() > 0)
			{
				bool ok = m_tree->SimulateDrag(item(g1->GetVolumeData(0)->GetName().ToQt(), TreePanel::TypeVolume),
					item(g2->GetVolumeData(0)->GetName().ToQt(), TreePanel::TypeVolume), true);
				fprintf(stderr, "[tree] drop volume of Group 1 after the volume of Group 2: %d\n", (int)ok);
			}
		});
		at(2100, [=]() {
			dump("volume between groups");
			// reorder inside Group 2: its last volume before its first
			DataGroup* g2 = m_canvas->GetGroup(VVDString(L"Group 2"));
			if (g2 && g2->GetVolumeNum() > 1)
			{
				bool ok = m_tree->SimulateDrag(item(g2->GetVolumeData(g2->GetVolumeNum() - 1)->GetName().ToQt(), TreePanel::TypeVolume),
					item(g2->GetVolumeData(0)->GetName().ToQt(), TreePanel::TypeVolume), false);
				fprintf(stderr, "[tree] reorder in Group 2: %d\n", (int)ok);
			}
			// a group dropped on another group: the layers swap (MoveLayerinView)
			bool ok2 = m_tree->SimulateDrag(item("Group 3", TreePanel::TypeGroup), item("Group 1", TreePanel::TypeGroup), false);
			fprintf(stderr, "[tree] Group 3 onto Group 1: %d\n", (int)ok2);
		});
		at(2600, [=]() {
			dump("reorder in group + group onto group");
			// toggle Group 1 (double-click path: OnAct), rename Group 2
			m_tree->Tree()->setCurrentItem(item("Group 1", TreePanel::TypeGroup));
			m_tree->OnAct(false);
			trigger("Group 2", TreePanel::TypeGroup, tr("Rename"));
			if (QTreeWidgetItem* it = m_tree->Tree()->currentItem()) it->setText(0, "Renamed group");
			m_tree->Tree()->closePersistentEditor(m_tree->Tree()->currentItem());
		});
		at(3100, [=]() {
			dump("Group 1 hidden, Group 2 renamed");
			if (!dir.isEmpty()) grab().save(dir + "/window_groups.png");
			// isolate the first volume of the renamed group, then show all
			DataGroup* g = m_canvas->GetGroup(VVDString(L"Renamed group"));
			if (g && g->GetVolumeNum() > 0)
				trigger(g->GetVolumeData(0)->GetName().ToQt(), TreePanel::TypeVolume, tr("Isolate"));
			dump("Isolate");
			if (!dir.isEmpty()) m_tree->grab().save(dir + "/tree_isolate.png");
			if (g && g->GetVolumeNum() > 0)
				trigger(g->GetVolumeData(0)->GetName().ToQt(), TreePanel::TypeVolume, tr("Show All"));
			dump("Show All");
			if (!dir.isEmpty()) m_tree->grab().save(dir + "/tree_final.png");
			// VVD_QT_GROUP_DEPTH=1: the renamed group in Depth blend mode
			// (VolumePropPanel "Depth": per-group MultiVolumeRenderer pass)
			if (g && qEnvironmentVariableIntValue("VVD_QT_GROUP_DEPTH"))
			{
				g->SetBlendMode(2);
				m_canvas->RefreshView();
			}
		});
	}
	else if (name == "colortext")
	{
		// VVD_QT_COLOR="g;y;gfp;mcherry;488nm;12, 34, 56": double-click the Color box,
		// type each entry key by key, press Enter, and print the volume colour
		QStringList entries = qEnvironmentVariable("VVD_QT_COLOR").split(';');
		int t = 300;
		for (const QString& entry : entries)
		{
			at(t, [this, entry]() {
				auto* le = m_prop->findChild<QLineEdit*>("ColorText");
				if (!le || !CurrentVolume()) { fprintf(stderr, "colortext: no box/volume\n"); return; }
				QPoint p = le->rect().center();
				QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(p), le->mapToGlobal(QPointF(p)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
				QCoreApplication::sendEvent(le, &dbl);
				le->setFocus();
				for (QChar ch : entry)
				{
					QKeyEvent kp(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
					QCoreApplication::sendEvent(le, &kp);
				}
				QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
				QCoreApplication::sendEvent(le, &enter);
				FLIVR::Color c = CurrentVolume()->GetColor();
				fprintf(stderr, "colortext '%s' -> %d %d %d  box='%s'\n", entry.toUtf8().constData(),
					int(std::lround(c.r() * 255)), int(std::lround(c.g() * 255)), int(std::lround(c.b() * 255)),
					le->text().toUtf8().constData());
			});
			t += 300;
		}
	}
	else if (name == "measuretop")
	{
		// Measurement stays above the main window while the view is clicked
		at(300, [this]() { EnsureMeasureDialog()->GetSettings(); ShowTool(m_measure_dlg); });
		at(300, [this, cv, w, h]() {
			activateWindow(); raise();
			cv->SimulateMouse(QEvent::MouseButtonPress, QPoint(w / 2, h / 2), Qt::LeftButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, QPoint(w / 2, h / 2), Qt::LeftButton, Qt::NoButton);
		});
		at(1500, [this]() {
			fprintf(stderr, "measuretop: tool=%d visible=%d mainActive=%d\n",
				int((m_measure_dlg->windowFlags() & Qt::WindowType_Mask) == Qt::Tool),
				int(m_measure_dlg->isVisible()), int(isActiveWindow()));
		});
	}
	else if (name == "sphererot")
	{
		// Settings "Virtual sphere": a centre drag turns like the wx
		// trackball; a drag around the centre near the edge mostly rolls; a drag
		// near the left/right side still turns the data.
		// "[sphere]" lines; view captures sph_<n>.png (VVD_QT_TEST_DIR).
		RenderCanvas* cv = m_canvas;
		const QString dir = QFileInfo(qEnvironmentVariable("VVD_QT_GRAB")).absolutePath();
		auto drag = [cv](const std::vector<QPoint>& pts) {
			cv->SimulateMouse(QEvent::MouseMove, pts[0], Qt::NoButton, Qt::NoButton);
			cv->SimulateMouse(QEvent::MouseButtonPress, pts[0], Qt::LeftButton, Qt::LeftButton);
			for (size_t i = 1; i < pts.size(); i++)
				cv->SimulateMouse(QEvent::MouseMove, pts[i], Qt::NoButton, Qt::LeftButton);
			cv->SimulateMouse(QEvent::MouseButtonRelease, pts.back(), Qt::LeftButton, Qt::NoButton);
		};
		auto rot = [cv](const char* tag) {
			double x, y, z; cv->GetRotations(x, y, z);
			fprintf(stderr, "[sphere] %s: rot %.3f %.3f %.3f\n", tag, x, y, z);
		};
		const int w = cv->width(), h = cv->height();
		const QPoint c(w / 2, h / 2);
		std::vector<QPoint> line, vline, arc;
		for (int i = 0; i <= 10; i++) line.push_back(c + QPoint(3 * i, 0));
		for (int i = 0; i <= 10; i++) vline.push_back(c + QPoint(0, 3 * i));
		const double r = 0.48 * std::min(w, h);
		for (int i = 0; i <= 45; i++)   // 0 -> 45 degrees, clockwise on screen
			arc.push_back(c + QPoint(int(std::lround(r * std::cos(i * M_PI / 180.0))), int(std::lround(r * std::sin(i * M_PI / 180.0)))));
		at(100, [=]() { cv->SetRotations(0, 0, 0); cv->SetSphereRot(false); drag(line); rot("wx trackball, centre drag right 30 px"); });
		at(200, [=]() { cv->SetRotations(0, 0, 0); cv->SetSphereRot(true); drag(line); rot("sphere, centre drag right 30 px"); });
		at(900, [=]() { cv->SetRotations(0, 0, 0); cv->SetSphereRot(false); drag(vline); rot("wx trackball, centre drag down 30 px"); });
		at(400, [=]() { cv->SetRotations(0, 0, 0); cv->SetSphereRot(true); drag(vline); rot("sphere, centre drag down 30 px"); });
		at(500, [=]() { cv->SetRotations(0, 0, 0); cv->RefreshView(); });
		at(1300, [=]() { cv->CaptureToFile(dir + "/sph_0.png"); });
		at(1900, [=]() { cv->SetSphereRot(true); drag(arc); rot("sphere, arc 45 deg clockwise near the edge"); cv->RefreshView(); });
		at(2700, [=]() { cv->CaptureToFile(dir + "/sph_1.png"); });
		// a drag near the left/right side of the view must still rotate
		std::vector<QPoint> side, sidev;
		for (int i = 0; i <= 10; i++) side.push_back(QPoint(w - 100 - 3 * i, h / 2));
		for (int i = 0; i <= 10; i++) sidev.push_back(QPoint(w - 100, h / 2 + 3 * i));
		at(3000, [=]() { cv->SetRotations(0, 0, 0); cv->SetSphereRot(true); drag(side); rot("sphere, drag left 30 px near the right side"); });
		at(3300, [=]() { cv->SetRotations(0, 0, 0); cv->SetSphereRot(true); drag(sidev); rot("sphere, drag down 30 px near the right side"); });
		// (QtMain grabs the window and quits 4 s after the test starts)
	}
	else if (name == "treescroll")
	{
		// delete a volume in the middle of a long tree; the list must not jump
		QTreeWidget* tw = m_tree->Tree();
		auto sb = [tw]() { return tw->verticalScrollBar()->value(); };
		auto pick = std::make_shared<QString>();
		auto before = std::make_shared<int>(0);
		at(300, [this, tw, sb, pick, before]() {
			tw->expandAll();
			tw->doItemsLayout();
			QScrollBar* v = tw->verticalScrollBar();
			v->setValue(v->maximum() / 2);
			// a volume row near the middle of the viewport
			QTreeWidgetItem* it = tw->itemAt(QPoint(40, tw->viewport()->height() / 2));
			while (it && it->childCount() > 0) it = tw->itemBelow(it);
			if (!it) { fprintf(stderr, "treescroll: no item\n"); return; }
			tw->setCurrentItem(it);
			*pick = it->text(0);
			*before = sb();
			fprintf(stderr, "treescroll: max=%d before=%d delete '%s'\n", v->maximum(), *before, pick->toUtf8().constData());
			m_tree->DeleteSelection();
		});
		at(1200, [tw, sb, pick, before]() {
			QTreeWidgetItem* cur = tw->currentItem();
			const QRect r = cur ? tw->visualItemRect(cur) : QRect();
			fprintf(stderr, "treescroll: after=%d (before %d) current='%s' visible=%d\n", sb(), *before,
				cur ? cur->text(0).toUtf8().constData() : "", int(cur && tw->viewport()->rect().intersects(r)));
		});
	}
	else if (name == "drop")
	{
		// VVD_QT_DROP=<file>[;<file>...]: drop these onto the render view
		QList<QUrl> urls;
		for (const QString& f : qEnvironmentVariable("VVD_QT_DROP").split(';', Qt::SkipEmptyParts))
			urls << QUrl::fromLocalFile(f);
		// VVD_QT_DROP_TARGET=frame: onto the main window instead (wx frame DnDFile)
		if (qEnvironmentVariable("VVD_QT_DROP_TARGET") == "frame")
			at(300, [this, urls]() {
				auto* mime = new QMimeData;
				mime->setUrls(urls);
				const QPointF pos(width() / 2.0, height() / 2.0);
				QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
				QCoreApplication::sendEvent(this, &enter);
				QDropEvent drop(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
				QCoreApplication::sendEvent(this, &drop);
				fprintf(stderr, "drop on main window accepted: %d\n", int(drop.isAccepted()));
				delete mime;
			});
		else
		at(300, [this, urls]() {
			bool ok = m_canvas->SimulateDrop(urls);
			fprintf(stderr, "drop on render view accepted: %d\n", int(ok));
		});
		// what the drop opened (a project loads synchronously after the drop;
		// before the VVD_QT_GRAB picture 7 s after start-up, 3 s before this)
		at(3500, [this]() {
			fprintf(stderr, "drop: %d volume(s), %d ruler(s)\n",
				(int)m_canvas->Volumes().size(), m_ruler_tool ? (int)m_ruler_tool->GetRulerList()->size() : -1);
			for (VolumeData* v : m_canvas->Volumes())
				fprintf(stderr, "drop:   volume %ls\n", v->GetName().c_str());
		});
	}
	else if (name == "capturedlg")
	{
		// press the render view's Capture button (dialog picture: VVD_QT_GRAB_CAPDLG)
		at(300, [this]() {
			for (QPushButton* b : m_view->findChildren<QPushButton*>())
				if (b->text() == tr("Capture")) { b->click(); break; }
		});
	}
	else if (name == "brush")
	{
		m_brush_tool->SelectBrushMode(2);
		QPoint p0(w * 30 / 100, h * 54 / 100);
		at(300, [=]() { cv->SimulateMouse(QEvent::MouseMove, p0, Qt::NoButton, Qt::NoButton);
			cv->SimulateMouse(QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton); });
		for (int i = 1; i <= 20; ++i)
		{
			QPoint p(w * (30 + 2 * i) / 100, h * 54 / 100);
			at(300 + 50 * i, [=]() { cv->SimulateMouse(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton); });
		}
		at(1500, [=]() { cv->SimulateMouse(QEvent::MouseButtonRelease, QPoint(w * 70 / 100, h * 54 / 100), Qt::LeftButton, Qt::NoButton); });
		// brush_extract: also extract the selection (Workspace "Extract")
		if (qEnvironmentVariableIsSet("VVD_QT_TEST_EXTRACT"))
			at(2300, [this]() { m_brush_tool->BrushCreate(); });
	}
}

int MainWindow::GetScaleBarUnit()
{
	return m_overlays ? m_overlays->GetSbUnitSel() : 1;
}
