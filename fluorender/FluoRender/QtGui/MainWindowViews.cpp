// MainWindow: several render views (wx VRenderFrame::m_vrv_list).
//
//   * wx CreateView: a new VRenderView pane right of the others (wxAUI
//     Layer(0).Centre().Right(), Dockable, no close button, floating size
//     600 x 400, min size 300 x 200), the Record/Export view list
//     (VMovieView::AddView), the Settings values every view takes (peeling
//     layers, micro blending, mouse interactivity, gradient background,
//     point volume mode, ruler transform / time dependence, wireframe test
//     mode), the Output Adjustments defaults, OrganizeVRenderViews(1) and
//     UpdateTree. wx creates further views only in OpenProject (/views/num
//     > 1); its "Open New VVDViewer" starts another process.
//   * wx DeleteVRenderView (the tree's "Close" on a view other than the
//     first): the view's layers leave it, the pane is removed, the movie
//     view list follows.
//   * every view has its own canvas (camera, layers, overlays, clipping
//     state), brush, ruler list, selector / calculator (wx: members of
//     VRenderVulkanView). The panes (Properties, Output Adjustments,
//     Clipping Planes, Measurement, the brush dialog, Components & Tracking)
//     follow the active view = the view of the tree selection (wx
//     OnSelection(vrv)) or the view last clicked; Record/Export records the
//     view of its own "Capture" combo (wx m_views_cmb).

#include "MainWindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include <QApplication>
#include <QMenu>
#include <QFileInfo>

#include "RenderCanvas.h"
#include "ViewPanel.h"
#include "TreePanel.h"
#include "AdjustPanel.h"
#include "ClipPanel.h"
#include "MoviePanel.h"
#include "RecorderPanel.h"
#include "VolumePropPanel.h"
#include "MeshPropPanel.h"
#include "SettingsDialog.h"
#include "RulerTool.h"
#include "ViewOverlays.h"
#include "BrushTool.h"
#include "BrushToolDialog.h"
#include "MeasureDialog.h"
#include "NoiseReductionDialog.h"
#include "CountingDialog.h"
#include "ColocalizationDialog.h"
#include "TraceDialog.h"
#include "Animator/Interpolator.h"
#include "DataManager.h"

// ---------------------------------------------------------------------------
// lookup
// ---------------------------------------------------------------------------

RenderCanvas* MainWindow::GetViewCanvas(int i) const
{
	return (i >= 0 && i < (int)m_views.size()) ? m_views[i]->canvas : nullptr;
}

ViewPanel* MainWindow::GetViewPanel(int i) const
{
	return (i >= 0 && i < (int)m_views.size()) ? m_views[i]->view : nullptr;
}

RulerTool* MainWindow::GetViewRulerTool(int i) const
{
	return (i >= 0 && i < (int)m_views.size()) ? m_views[i]->ruler : nullptr;
}

BrushTool* MainWindow::GetViewBrushTool(int i) const
{
	return (i >= 0 && i < (int)m_views.size()) ? m_views[i]->brush : nullptr;
}

ViewOverlays* MainWindow::GetViewOverlays(int i) const
{
	return (i >= 0 && i < (int)m_views.size()) ? m_views[i]->overlays : nullptr;
}

int MainWindow::ViewIndex(const RenderCanvas* c) const
{
	for (int i = 0; i < (int)m_views.size(); i++)
		if (m_views[i]->canvas == c) return i;
	return -1;
}

RenderCanvas* MainWindow::FindViewCanvas(const QString& name) const
{
	for (const auto& v : m_views)
		if (v->name == name) return v->canvas;
	return nullptr;
}

// ---------------------------------------------------------------------------
// creation
// ---------------------------------------------------------------------------

// wx VRenderView ctor: "Render View:<n>", the canvas and its bars; the
// caption bar of the pane (wxAUI Caption(vrv->GetName()))
MainWindow::ViewSlot* MainWindow::NewViewSlot(int id)
{
	auto slot = std::make_unique<ViewSlot>();
	slot->id = id;
	slot->name = QString("Render View:%1").arg(id);
	const bool first = m_views.empty();
	if (first)
	{
		slot->sel = &m_selector;
		slot->calc = &m_calculator;
	}
	else
	{
		slot->own_sel = std::make_unique<VolumeSelector>();
		slot->own_sel->SetDataManager(m_data_mgr);
		slot->own_calc = std::make_unique<VolumeCalculator>();
		slot->sel = slot->own_sel.get();
		slot->calc = slot->own_calc.get();
	}
	slot->frame = new QWidget(m_view_area);
	auto* v = new QVBoxLayout(slot->frame);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	if (first)
	{
		// view 0: the caption bar above the view (its pane title in wx)
		auto* cap = new QLabel(slot->name, slot->frame);
		cap->setStyleSheet("background: #e4e4e4; padding: 2px 4px; border-bottom: 1px solid #c8c8c8; font-size: 11px;");
		v->addWidget(cap);
	}
	slot->view = new ViewPanel(slot->frame);
	slot->canvas = slot->view->Canvas();
	slot->canvas->SetViewName(slot->name);
	v->addWidget(slot->view, 1);
	m_views.push_back(std::move(slot));
	return m_views.back().get();
}

// SettingDlg values every view takes (VRenderFrame ctor / CreateView /
// the SettingDlg handlers)
void MainWindow::ApplyViewSettings(RenderCanvas* c)
{
	if (!c || !m_settings) return;
	c->SetPeelingLayers(m_settings->GetPeelingLayers());
	c->SetBlendSlices(m_settings->GetMicroBlend());
	c->SetAdaptive(m_settings->GetMouseInt());
	c->SetAdaptiveRes(m_settings->GetMouseLv());
	c->SetUseFogMesh(m_settings->GetUseFogMesh());
	// wx DrawOLShadows / DrawOLShadowsMesh read SettingDlg::GetShadowDir;
	// VRenderFrame ctor / SettingDlg::OnGradBgCheck -> SetGradBg
	double sx = 0.0, sy = 0.0;
	m_settings->GetShadowDir(sx, sy);
	c->SetShadowDir(sx, sy);
	c->SetGradBg(m_settings->GetGradBg());
}

// The per-view part of the VRenderFrame ctor / CreateView: tools, overlays,
// providers and the signal connections of one view. Handlers that act on
// "the view" use the view of the signal (wx: the VRenderVulkanView itself);
// those that start from a user action in the view make it the active view
// first (wx: the clicked view is the tree's current view).
void MainWindow::WireView(ViewSlot& slot)
{
	RenderCanvas* c = slot.canvas;
	ViewPanel* vp = slot.view;
	// shared mask / label volumes are looked up by name (wx DrawOVER / DrawMIP)
	c->SetVolumeLookup([this](const VVDString& name) { return m_data_mgr ? m_data_mgr->GetVolumeData(name) : nullptr; });
	connect(qApp, &QCoreApplication::aboutToQuit, c, &RenderCanvas::Shutdown);
	connect(c, &RenderCanvas::filesDropped, this, [this, c](const QList<QUrl>& urls) {
		ActivateCanvas(c);
		OpenDroppedUrls(urls, true, c); });
	// a click in a view makes it the active one
	connect(c, &RenderCanvas::viewActivated, this, [this, c]() { ActivateCanvas(c); });
	// brush first: the first active tool receives the mouse (wx modes 7/8)
	slot.brush = new BrushTool(c, this, this);
	c->AddTool(slot.brush);
	slot.ruler = new RulerTool(c, this);
	slot.overlays = new ViewOverlays(c, slot.ruler, this);   // after the ruler tool
	vp->SetOverlays(slot.overlays);
	BrushTool* brush = slot.brush;
	RulerTool* ruler = slot.ruler;
	ViewOverlays* ov = slot.overlays;
	// "Capture" button (wx VRenderView::OnCapture -> VRenderFrame / SettingDlg)
	{
		CaptureDialog::Hooks hooks;
		hooks.get_proj_save = [this]() { return m_settings->GetProjSave(); };
		hooks.get_compress = [this]() { return m_settings->GetSaveCompress(); };
		hooks.set_compress = [this](bool b) { m_settings->SetSaveCompress(b); };
		hooks.get_embed = [this]() { return m_vrp_embed; };
		hooks.set_embed = [this](bool b) { m_vrp_embed = b; };
		hooks.save_project = [this](const QString& f) { return SaveProject(f); };
		vp->SetCaptureHooks(hooks);
	}
	// wx Set4DSeqFrame: m_run_script / m_script_file = SettingDlg GetRunScript / GetScriptFile
	c->SetScriptSettingsProvider([this](bool& run, QString& file) {
		run = m_settings->GetRunScript();
		file = m_settings->GetScriptFile();
	});
	connect(c, &RenderCanvas::clipMoveRequested, this, [this](int dir) { m_clip->MoveLinkedClippingPlanes(dir); });
	// wx StartLoopUpdate (_DARWIN) reads the clipping view's slab widths
	c->SetClipSlabProvider([this]() {
		RenderCanvas::ClipSlabInfo cs;
		if (!m_clip) return cs;
		cs.valid = true;
		cs.chann_link = m_clip->GetChannLink();
		for (int a = 0; a < 3; ++a) cs.dist[a] = m_clip->GetDist(a);
		return cs; });
	// wx VRenderView::OnAovSldrIdle reads the clipping view's hold / plane mode
	vp->SetClipStateProvider([this](int& plane_mode, bool& hold) {
		if (!m_clip) return false;
		plane_mode = m_clip->GetPlaneMode();
		hold = m_clip->GetHoldPlanes();
		return true; });
	// wx OnIdle Alt+V: CopyLevel(m_cur_vol) of a pyramid volume (the copy is
	// discarded: wx neither adds nor keeps it), then m_selector.SetVolume
	connect(c, &RenderCanvas::copyLevelRequested, this, [this, c]() {
		VolumeData* cur = c->GetCurrentVolume();
		if (!cur || !cur->isBrxml()) return;
		if (VolumeData* copy = cur->CopyLevel())
			delete copy;
		const int i = ViewIndex(c);
		if (i >= 0) m_views[i]->sel->SetVolume(cur);
	});
	// wx OnIdle Ctrl+B: WarpCurrentVolume (exports the rulers as a BigWarp CSV
	// and runs the warp in Fiji through the "Fiji Interface" plugin, which the
	// Qt port has no counterpart for); here the built-in WarpVolumeByRulers path
	// with the Measurement dialog's current transform settings (its "Apply
	// Transform" button)
	connect(c, &RenderCanvas::warpRequested, this, [this, c]() {
		ActivateCanvas(c);
		EnsureMeasureDialog()->ApplyTransform();
	});
	// wx SetManipParams: ClippingView::SetVolumeData(GetCurSelVol()), UpdateTree
	connect(c, &RenderCanvas::manipParamsApplied, this, [this]() {
		if (VolumeData* vd = CurrentVolume()) m_clip->SetVolume(vd);
		RefreshTree();
	});
	connect(c, &RenderCanvas::frameStepRequested, this, [this](int dir) { if (dir > 0) m_movie->UpFrame(); else m_movie->DownFrame(); });
	// wx OnIdle f / l (this == cur_glview): TraceDlg::CellFull / CellLink(false)
	connect(c, &RenderCanvas::cellFullRequested, this, [this, c]() {
		ActivateCanvas(c);
		if (m_trace_dlg) m_trace_dlg->CellFull(); });
	connect(c, &RenderCanvas::cellLinkRequested, this, [this, c]() {
		ActivateCanvas(c);
		if (m_trace_dlg) m_trace_dlg->CellLink(false); });
	connect(brush, &BrushTool::brushSelected, this, [this, brush](int m) { if (brush == m_brush_tool) m_tree->SelectBrush(m); });
	// clicks in the view (wx OnMouse: LeftUp -> Pick, LeftDClick -> SelSegVolume)
	connect(c, &RenderCanvas::pickRequested, this, [this, c](const QPoint& pos) { ActivateCanvas(c); PickAt(pos); });
	// wx OnMouse LeftDClick: a ruler point under the pointer is centred by
	// an animated move (StartManipulation with the object translation that
	// brings the point to the object centre)
	c->SetDoubleClickHook([c, ruler](const QPoint& pos) {
		FLIVR::Point* p = ruler ? ruler->GetEditingRulerPoint(pos.x(), pos.y()) : nullptr;
		if (!p) return false;
		double cx, cy, cz;
		c->GetObjCenter(cx, cy, cz);
		FLIVR::Point trans(cx - p->x(), -(cy - p->y()), -(cz - p->z()));
		c->StartManipulation(nullptr, nullptr, nullptr, &trans, nullptr);
		return true;
	});
	connect(c, &RenderCanvas::segmentPickRequested, this, [this, c](const QPoint& pos) {
		ActivateCanvas(c);
		// wx: NA mode -> SelLabelSegVolumeMax(0, NA-mode volumes), else SelSegVolume()
		std::vector<VolumeData*> ref;
		for (VolumeData* vd : c->PopVolumeList())
			if (vd && vd->GetDisp() && vd->GetNAMode()) ref.push_back(vd);
		if (!ref.empty()) SelLabelSegVolumeMax(0, ref, pos);
		else SelSegVolume(0, pos);
		c->RefreshView();
	});
	// wx BrushToolDlg::GetSettings (after every Segment / undo / redo) also
	// refreshes the Noise Reduction, Counting and Colocalization dialogs;
	// wx VRenderVulkanView::Segment then runs the tracking dialog's auto ID /
	// manual assist (TraceDlg::CellNewID / CellEraseID / CellUpdate / CellLink)
	connect(brush, &BrushTool::settingsChanged, this, [this, brush]() {
		if (brush != m_brush_tool) return;
		if (m_nr_dlg && m_nr_dlg->isVisible()) m_nr_dlg->GetSettings();
		if (m_count_dlg && m_count_dlg->isVisible()) m_count_dlg->GetSettings();
		if (m_coloc_dlg && m_coloc_dlg->isVisible()) m_coloc_dlg->GetSettings();
	});
	connect(brush, &BrushTool::segmented, this, [this, brush](int sel_mode) {
		if (brush != m_brush_tool || !m_trace_dlg) return;
		m_trace_dlg->OnViewSegmented(sel_mode);
	});
	// one interaction mode (wx m_int_mode is a single value of the view): the
	// ruler and brush tools keep their copies in step. Ruler creation (5)
	// puts the brush in 5 too, so Shift in a 2pt / 2+pt / locator ruler mode
	// paints in mode 7 (wx SetBrush 5 -> 7) while the ruler tool waits (7);
	// the end of the hold brings both back to 5 (wx UpdateBrushState).
	auto int_mode_sync = std::make_shared<bool>(false);
	connect(ruler, &RulerTool::intModeChanged, this, [brush, ruler, int_mode_sync](int m) {
		if (*int_mode_sync) return;
		*int_mode_sync = true;
		const int bm = brush->GetIntMode();
		if (m == 5)
		{
			brush->SetRulerType(ruler->GetRulerType());
			if (bm != 5) brush->SetIntMode(5);
		}
		else if (m == 6)
		{
			if (bm != 6) brush->SetIntMode(6);
		}
		else if (m == 1)
		{
			if (bm == 5 || bm == 6 || bm == 7 || bm == 8) brush->SetIntMode(1);
		}
		*int_mode_sync = false;
	});
	connect(brush, &BrushTool::intModeChanged, this, [ruler, int_mode_sync](int m) {
		if (*int_mode_sync) return;
		*int_mode_sync = true;
		const int rm = ruler->GetIntMode();
		if (m == 7 || m == 5)
		{
			if (rm != m) ruler->SetIntMode(m);
		}
		else if (rm == 5 || rm == 6 || rm == 7)
			ruler->SetIntMode(1);   // painting (2) or camera (1): the ruler mode ends
		*int_mode_sync = false;
	});
	// Measurement Auto-select: the picked volume is selected in the tree as a
	// user click on it would (Properties / Clipping follow, current volume of
	// every view); the paint-ruler stroke picks it at the press
	connect(ruler, &RulerTool::volumeAutoSelected, this, [this, c](VolumeData* vd) {
		if (m_tree) m_tree->SelectVolume(vd, c);
	});
	brush->SetRulerPressHook([ruler](int x, int y) { return ruler->AutoSelectForPaintRuler(x, y); });
	// wx mode 7 release: AddRulerPoint(x, y) for ruler type 3, else AddPaintRulerPoint
	connect(brush, &BrushTool::rulerPointRequested, this, [c, ruler](int x, int y) {
		ruler->AddRulerPoint(x, y);
		c->RefreshView();
	});
	connect(brush, &BrushTool::paintRulerPointRequested, this, [this, c, ruler]() {
		const int i = ViewIndex(c);
		ruler->AddPaintRulerPoint(i >= 0 ? m_views[i]->sel : m_sel);
		c->RefreshView();
	});
	// wx DrawGrid colour: VRenderVulkanView::GetTextColor (Settings text colour)
	c->SetTextColorProvider([ruler]() { return ruler->GetTextColor(); });
	ApplyViewSettings(c);
	// wx OnMouse RightUp -> OnContextMenu (MainWindowUI.cpp)
	connect(c, &RenderCanvas::contextMenuRequested, this, [this, c](const QPoint& pos) {
		ActivateCanvas(c);
		ShowViewContextMenu(pos); });
	// wx AddVolumeData / Move*Group / OrganizeLayers -> AdjustView
	connect(c, &RenderCanvas::groupLinkChanged, this, [this](VolumeData* vd, DataGroup* group) {
		if (vd) m_adjust->SetVolume(vd);
		m_adjust->SetGroupLink(group);
		m_adjust->UpdateSync();
	});
	// wx AddGroup: AdjustView::GetDefaults
	c->SetGroupDefaultsProvider([this](DataGroup* group) {
		FLIVR::Color gamma, brightness, hdr, level;
		bool sync_r, sync_g, sync_b;
		m_adjust->GetDefaults(gamma, brightness, hdr, level, sync_r, sync_g, sync_b);
		group->SetGamma(gamma);
		group->SetBrightness(brightness);
		group->SetHdr(hdr);
		group->SetLevels(level);
		group->SetSyncR(sync_r);
		group->SetSyncG(sync_g);
		group->SetSyncB(sync_b);
	});
	// Record/Export (wx VMovieView / RecorderDlg): SetParams / Set4DSeqFrame
	// side effects on the other panes (wx: ClippingView::SetVolumeData,
	// UpdateTree, RecorderDlg::SetSelection, MeasureDlg::UpdateList,
	// UpdateTreeFrames)
	connect(c, &RenderCanvas::paramsApplied, this, [this, c](double t) {
		VolumeData* vd = c->GetCurrentVolume();
		if (vd && c == m_canvas) m_clip->SetVolume(vd);
		m_tree->UpdateTree(vd ? vd->GetName().ToQt() : QString(), TreePanel::TypeVolume);
		if (m_interpolator)
			m_movie->GetRecorder()->SetSelection(m_interpolator->GetKeyIndexFromTime(t));
	});
	connect(c, &RenderCanvas::dataFramesChanged, this, [this, c, ruler, vp](int t) {
		ruler->SetCurTime(t);
		if (m_measure_dlg && c == m_canvas) m_measure_dlg->UpdateList();
		VolumeData* vd = c->GetCurrentVolume();
		m_tree->UpdateTree(vd ? vd->GetName().ToQt() : QString(), TreePanel::TypeVolume);
		vp->ReloadLegend();
	});
	// wx GetTraces -> TraceDlg::GetSettings(vrv)
	connect(c, &RenderCanvas::tracesUpdated, this, [this, c]() { if (m_trace_dlg) m_trace_dlg->GetSettings(c); });
	connect(c, &RenderCanvas::vulkanReady, this, [this, c]() {
		if (c->IsSecondarySurface()) return;
		m_settings->ApplyToEngine();
		SetStatus(tr("Vulkan ready.")); });
	connect(m_settings, &SettingsDialog::engineSettingsApplied, c, &RenderCanvas::RefreshView);
	connect(m_settings, &SettingsDialog::settingsChanged, c, &RenderCanvas::RefreshView);
	(void)ov;
}

// ---------------------------------------------------------------------------
// active view
// ---------------------------------------------------------------------------

void MainWindow::SetActiveView(int i)
{
	if (i < 0 || i >= (int)m_views.size()) return;
	ViewSlot& v = *m_views[i];
	const bool changed = m_canvas != v.canvas || m_brush_tool != v.brush;
	m_active_view = i;
	m_view = v.view;
	m_canvas = v.canvas;
	m_brush_tool = v.brush;
	m_ruler_tool = v.ruler;
	m_overlays = v.overlays;
	m_sel = v.sel;
	m_calc = v.calc;
	// keys forwarded to panes act once, for this view (one view: every
	// canvas, i.e. that one)
	RenderCanvas::SetKeyCanvas(m_views.size() > 1 ? m_canvas : nullptr);
	if (!changed)
		return;
	// the panes follow (wx OnSelection(vrv): AdjustView::SetRenderView,
	// ClippingView / VPropView / MeasureDlg / BrushToolDlg / TraceDlg
	// GetSettings(vrv))
	if (m_clip) m_clip->SetRenderCanvas(m_canvas);
	if (m_prop)
	{
		m_prop->SetRenderCanvas(m_canvas);
		m_prop->SetOverlays(m_overlays);
	}
	if (m_adjust) m_adjust->SetRenderCanvas(m_canvas);
	if (m_brush_dlg) m_brush_dlg->SetTool(m_brush_tool);
	if (m_measure_dlg) m_measure_dlg->SetTool(m_ruler_tool);
	if (m_trace_dlg) m_trace_dlg->GetSettings(m_canvas);
	if (m_tree) m_tree->SelectBrush(m_brush_tool ? m_brush_tool->GetPaintMode() : 0);
}

// ---------------------------------------------------------------------------
// wx CreateView / DeleteVRenderView / OrganizeVRenderViews
// ---------------------------------------------------------------------------

QString MainWindow::CreateView()
{
	// "Render View:<n>": one more than the highest number in use (wx counts
	// every view ever created; the port re-uses the numbers of closed views
	// so that a project opened again gets the same names)
	int id = 1;
	for (const auto& v : m_views) id = std::max(id, v->id + 1);
	ViewSlot* slot = NewViewSlot(id);
	// wxAUI pane: Dockable, no close button, floating size 600 x 400, min
	// size 300 x 200, Layer(0).Centre().Right()
	slot->dock = new QDockWidget(slot->name, m_view_area);
	slot->dock->setObjectName(QString("RenderViewDock%1").arg(id));
	slot->dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
	slot->dock->setMinimumSize(300, 200);
	slot->dock->setWidget(slot->frame);
	slot->dock->setStyleSheet(styleSheet());
	// the view right of the last one (left-right split)
	QDockWidget* last = nullptr;
	for (const auto& v : m_views)
		if (v->dock && v.get() != slot && !v->dock->isFloating()) last = v->dock;
	if (last)
		m_view_area->splitDockWidget(last, slot->dock, Qt::Horizontal);
	else
		m_view_area->addDockWidget(Qt::RightDockWidgetArea, slot->dock);
	connect(slot->dock, &QDockWidget::topLevelChanged, slot->dock, [d = slot->dock](bool floating) {
		if (floating) d->resize(600, 400); });
	WireView(*slot);
	// wx CreateView: test mode 3 (wireframe), the Output Adjustments defaults
	if (m_settings->GetTestMode(3))
		slot->canvas->SetTestWiref(true);
	if (m_adjust)
	{
		FLIVR::Color gamma, brightness, hdr, level;
		bool sync_r, sync_g, sync_b;
		m_adjust->GetDefaults(gamma, brightness, hdr, level, sync_r, sync_g, sync_b);
		slot->canvas->SetGamma(gamma);
		slot->canvas->SetBrightness(brightness);
		slot->canvas->SetHdr(hdr);
		slot->canvas->SetLevels(level);
		slot->canvas->SetSyncR(sync_r);
		slot->canvas->SetSyncG(sync_g);
		slot->canvas->SetSyncB(sync_b);
	}
	slot->ruler->SetPointVolumeMode(m_settings->GetPointVolumeMode());
	slot->ruler->SetRulerUseTransf(m_settings->GetRulerUseTransf());
	slot->ruler->SetRulerTimeDep(m_settings->GetRulerTimeDep());
	slot->ruler->SetRulerAutoSelect(m_settings->GetRulerAutoSelect());
	slot->ruler->SetTextColorMode(m_settings->GetTextColor());
	slot->ruler->SetFont(m_settings->GetFontPath(), m_settings->GetTextSize());
	// a view sharing the rotation link (Settings "Link all rendering views'
	// rotations") follows the others
	if (m_views[0]->canvas->GetLinkedRot())
	{
		double rx, ry, rz;
		m_views[0]->canvas->GetRotations(rx, ry, rz);
		slot->canvas->SetLinkedRot(true);
		slot->canvas->SetRotations(rx, ry, rz, false);
	}
	if (m_movie) m_movie->AddView(slot->name);
	RenderCanvas::SetKeyCanvas(m_canvas);
	OrganizeViews(1);
	SyncViewLists();
	RefreshTree();
	return slot->name;
}

void MainWindow::DeleteView(const QString& name)
{
	for (int i = 1; i < (int)m_views.size(); i++)
		if (m_views[i]->name == name) { DeleteView(i); return; }
}

void MainWindow::DeleteView(int i)
{
	if (i <= 0 || i >= (int)m_views.size()) return;   // the first view stays
	ViewSlot& v = *m_views[i];
	if (m_active_view == i || m_canvas == v.canvas)
		SetActiveView(0);
	else if (m_active_view > i)
		m_active_view--;
	RenderCanvas* c = v.canvas;
	// the view's data leave it (wx: ~VRenderVulkanView Clear(); the data
	// stay in the DataManager, as wx)
	if (m_trace_dlg) m_trace_dlg->ViewClosed(c);
	if (m_movie)
	{
		if (m_movie->GetRenderCanvas() == c) m_movie->SetRenderCanvas(m_canvas);
		m_movie->DeleteView(v.name);
	}
	c->Shutdown();
	c->SetCurrentMesh(nullptr);
	c->ClearMeshes();
	c->ClearVolumes();
	v.brush->ReleaseResources();
	delete v.brush;
	delete v.overlays;
	delete v.ruler;
	if (v.dock)
	{
		v.dock->setWidget(nullptr);
		m_view_area->removeDockWidget(v.dock);
	}
	delete v.frame;   // view panel + canvas (its surface goes back to the context)
	if (v.dock) v.dock->deleteLater();
	m_views.erase(m_views.begin() + i);
	RenderCanvas::SetKeyCanvas(m_views.size() > 1 ? m_canvas : nullptr);
	OrganizeViews(1);
	SyncViewLists();
	RefreshTree();
	m_canvas->RefreshView();
}

// OpenProject: the views of the previous project go (wx keeps them -- its
// "delete the other views" code is commented out, so wx adds views on every
// open and fills the last one with view 0 -- the port starts from one view)
void MainWindow::ClearExtraViews()
{
	while (m_views.size() > 1)
		DeleteView((int)m_views.size() - 1);
}

// wx OrganizeVRenderViews: equal minimum sizes, top-bottom (0) or
// left-right (1)
void MainWindow::OrganizeViews(int mode)
{
	const int n = (int)m_views.size();
	if (n <= 1 || !m_view_area) return;
	QList<QDockWidget*> docks;
	QList<int> sizes;
	const QSize area = m_view_area->size();
	const int each = (mode == 0 ? area.height() : area.width()) / n;
	for (const auto& v : m_views)
		if (v->dock && !v->dock->isFloating()) { docks << v->dock; sizes << each; }
	if (!docks.isEmpty())
		m_view_area->resizeDocks(docks, sizes, mode == 0 ? Qt::Vertical : Qt::Horizontal);
}

void MainWindow::SyncViewLists()
{
	std::vector<RenderCanvas*> list;
	for (const auto& v : m_views) list.push_back(v->canvas);
	if (m_tree) m_tree->SetCanvases(list);
}
